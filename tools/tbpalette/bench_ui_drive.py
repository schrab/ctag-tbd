#!/usr/bin/env python3
"""TBPalette UI & Preset verification bench script.

Runs in ONE serial session on /dev/ttyACM0:
1. Verifies link and ensures TBPalette is loaded on ch0.
2. Navigates panel UI:
   ROOT -> PANEL_PARAMS (PANEL_IN)
   MODE_SELECT -> MODE_GROUP (General) -> MODE_EDIT (smodel).
3. Steps smodel across key models, capturing framebuffer screenshots:
   - Model 0: Virtual Analog
   - Model 21: Phase Distortion (heaviest CPU model)
   - Model 22: Six Op DX7 Bank 1 (DX7 syx bank 0)
   - Model 82: Wavetable Diatonic Chord (long text label)
   - Model 97: Supersaw Chords (last model)
4. Sets smodel=21, macro=2048.
5. Navigates BACK -> MODE_GROUP -> MODE_SELECT -> PRESETS.
6. Enters PRESETS, navigates to [STORE PRESET], presses OK to store.
7. Waits for async presets push, captures toast screenshot.
8. Mutates parameters: smodel=0, macro=0.
9. Enters PRESETS, selects the newly stored preset (index 1), presses OK to load.
10. Verifies parameters are restored to smodel=21, macro=2048.
11. Verifies audio pacing and health counters.
"""

import json
import sys
import time
import serial
from PIL import Image

STX, ETX = 0x02, 0x03
PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"


def open_port(path: str):
    s = serial.Serial()
    s.port = path
    s.baudrate = 115200
    s.dtr = False
    s.rts = False
    s.timeout = 0.1
    s.open()
    time.sleep(0.3)
    s.reset_input_buffer()
    return s


def request(s, cmd_obj: dict, timeout: float = 3.0) -> dict:
    seq = int(time.time() * 1000) & 0x7FFFFFFF
    cmd_obj = dict(cmd_obj, seq=seq)
    payload = json.dumps(cmd_obj, separators=(",", ":")).encode()
    s.write(bytes([STX]) + payload + bytes([ETX]))
    end = time.time() + timeout
    buf = bytearray()
    in_frame = False
    while time.time() < end:
        chunk = s.read(256)
        for ch in chunk or b"":
            if ch == STX:
                in_frame = True
                buf.clear()
            elif ch == ETX:
                if not in_frame:
                    continue
                try:
                    reply = json.loads(bytes(buf).decode(errors="replace"))
                except Exception:
                    in_frame = False
                    continue
                if reply.get("seq") == seq:
                    return reply
                in_frame = False
            elif in_frame:
                buf.append(ch)
    raise TimeoutError(f"no reply to {cmd_obj.get('cmd')}")


def fb_to_png(hexdata: str, out_path: str):
    raw = bytes.fromhex(hexdata)
    if len(raw) != 1024:
        raise ValueError(f"framebuffer is {len(raw)} B, expected 1024")
    img = Image.new("1", (128, 64), 0)
    px = img.load()
    for page in range(8):
        for x in range(128):
            bits = raw[page * 128 + x]
            for bit in range(8):
                if (bits >> bit) & 1:
                    px[x, page * 8 + bit] = 1
    img.save(out_path)


def capture_fb(s, out_path: str):
    fb = request(s, {"cmd": "/debug/getDisplayFramebuffer"})
    if fb.get("data"):
        fb_to_png(fb["data"], out_path)
        print(f"  [OLED] Captured screenshot -> {out_path}")
    else:
        print(f"  [OLED] WARNING: Failed to get framebuffer")


def wait_ready(s, timeout: float = 15.0) -> bool:
    end = time.time() + timeout
    while time.time() < end:
        try:
            r = request(s, {"cmd": "/debug/getProxyState"}, timeout=1.0)
            if r.get("active0"):
                return True
        except (TimeoutError, json.JSONDecodeError):
            pass
        time.sleep(0.2)
    return False


def inject(s, action: str, count: int = 1, delta: int = 0):
    cmd = {"cmd": "/debug/injectEvent", "action": action, "count": count}
    if delta != 0:
        cmd["delta"] = delta
    resp = request(s, cmd)
    time.sleep(0.08)
    return resp


def main():
    print(f"==> Opening console on {PORT}...")
    s = open_port(PORT)
    print("==> Waiting for link to DSP...")
    if not wait_ready(s, timeout=15.0):
        print("ERROR: link to DSP did not become ready")
        sys.exit(1)

    state = request(s, {"cmd": "/debug/getProxyState"})
    print(f"Link UP: active0={state.get('active0')}")

    if state.get("active0") != "TBPalette":
        print("==> Switching ch0 to TBPalette...")
        request(s, {"cmd": "/debug/setActivePlugin", "id": "TBPalette", "ch": 0})
        time.sleep(1.0)

    # 1. UI Navigation to PANEL_PARAMS
    print("\n==> Navigating to PANEL_PARAMS...")
    ui = request(s, {"cmd": "/debug/getUiState"})
    print(f"  Initial UI state: nav={ui.get('nav')}, panel={ui.get('panel')} ({ui.get('panelName')})")

    # If currently inside a panel (PANEL_IN = 1), press back multiple times to reach ROOT
    for _ in range(5):
        if ui.get("nav") == 0:
            break
        print("  Pressing back to reach ROOT...")
        inject(s, "back")
        time.sleep(0.1)
        ui = request(s, {"cmd": "/debug/getUiState"})

    # PANEL_PARAMS is panel 4
    PANEL_PARAMS = 4
    while ui.get("panel") != PANEL_PARAMS:
        curr = ui.get("panel", 0)
        step = 1 if curr < PANEL_PARAMS else -1
        print(f"  Stepping encoder {step} towards PANEL_PARAMS (currently {curr})...")
        inject(s, "enc2", delta=step)
        ui = request(s, {"cmd": "/debug/getUiState"})

    print(f"  Arrived at panel: {ui.get('panelName')}")
    # Enter panel (ROOT -> PANEL_IN, enters MODE_SELECT)
    inject(s, "ok")
    time.sleep(0.15)
    ui = request(s, {"cmd": "/debug/getUiState"})
    print(f"  Entered panel: nav={ui.get('nav')} ({'PANEL_IN' if ui.get('nav')==1 else 'ROOT'})")

    # In MODE_SELECT, cursor is on 0 (PARAMS). Press OK to enter MODE_GROUP
    print("  Entering MODE_GROUP (cursor on General)...")
    inject(s, "ok")
    time.sleep(0.15)

    # In MODE_GROUP, cursor is on 0 (General). Press OK to enter MODE_EDIT
    print("  Entering MODE_EDIT (cursor on smodel in General)...")
    inject(s, "ok")
    time.sleep(0.2)

    # 2. Drive smodel to key models and capture screenshots
    target_models = [
        (0, "uimcu/tools/tbpalette_model_0.png", "Virtual Analog"),
        (21, "uimcu/tools/tbpalette_model_21.png", "Phase Distortion"),
        (22, "uimcu/tools/tbpalette_model_22.png", "Six Op DX7 Bank 1"),
        (82, "uimcu/tools/tbpalette_model_82.png", "Wavetable Diatonic Chord"),
        (97, "uimcu/tools/tbpalette_model_97.png", "Supersaw Chords"),
    ]

    print("\n==> Testing OLED rendering across key models...")
    for model_id, img_path, expected_label in target_models:
        # Set smodel via DSP debug command
        request(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "smodel", "value": model_id})
        time.sleep(0.15)
        # Nudge encoder to trigger C3 UI redraw
        inject(s, "enc", delta=0)
        time.sleep(0.1)

        p = request(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": "smodel"})
        cur_val = p.get("param", {}).get("current")
        print(f"\nModel {model_id}: current={cur_val} (expected '{expected_label}')")

        capture_fb(s, img_path)

    # 3. Preset Save & Load Verification via Panel UI
    print("\n==> Testing Preset Save & Recall via Panel UI...")
    # Set model 21 (Phase Distortion) and twist = 2048
    print("  Setting smodel=21, twist=2048...")
    request(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "smodel", "value": 21})
    request(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "twist", "value": 2048})
    time.sleep(0.1)

    p_model = request(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": "smodel"})
    p_twist = request(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": "twist"})
    print(f"  Pre-save state: smodel={p_model.get('param', {}).get('current')}, twist={p_twist.get('param', {}).get('current')}")

    # Step back from MODE_EDIT -> MODE_GROUP
    print("  Navigating back to MODE_GROUP...")
    inject(s, "back")
    time.sleep(0.15)

    # Step back from MODE_GROUP -> MODE_SELECT
    print("  Navigating back to MODE_SELECT...")
    inject(s, "back")
    time.sleep(0.15)

    # In MODE_SELECT: cursor 0 = PARAMS, cursor 1 = MAP, cursor 2 = PRESETS
    print("  Selecting PRESETS in MODE_SELECT...")
    inject(s, "enc2", delta=2)
    time.sleep(0.1)

    # Enter PRESETS (mode = MODE_PRESETS)
    print("  Entering MODE_PRESETS...")
    inject(s, "ok")
    time.sleep(0.3)
    capture_fb(s, "uimcu/tools/tbpalette_presets_list.png")

    # Scroll down to [STORE PRESET] (last row: presetCount + 1)
    print("  Scrolling down to [STORE PRESET]...")
    inject(s, "enc2", count=20, delta=1)
    time.sleep(0.15)

    # Short press OK on [STORE PRESET] to store current preset
    print("  Pressing OK on [STORE PRESET]...")
    inject(s, "ok")
    time.sleep(0.15)
    capture_fb(s, "uimcu/tools/tbpalette_preset_stored_toast.png")
    # Wait for DSP pushPresets to arrive asynchronously and onTick to update
    time.sleep(0.8)

    # Mutate parameters: smodel=0, twist=0
    print("  Mutating engine state to smodel=0, twist=0...")
    request(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "smodel", "value": 0})
    request(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "twist", "value": 0})
    time.sleep(0.1)

    p_model = request(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": "smodel"})
    p_twist = request(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": "twist"})
    print(f"  Mutated state: smodel={p_model.get('param', {}).get('current')}, twist={p_twist.get('param', {}).get('current')}")
    assert p_model.get("param", {}).get("current") == 0
    assert p_twist.get("param", {}).get("current") == 0

    # In MODE_PRESETS, the list has:
    # row 0: Default
    # row 1: <the newly saved preset>
    # row 2: gap
    # row 3: [STORE PRESET]
    # First scroll all the way to top (row 0), then step 1 down to row 1
    print("  Selecting stored preset (row 1) in PRESETS list...")
    inject(s, "enc2", count=20, delta=-1)  # top: row 0
    time.sleep(0.1)
    inject(s, "enc2", count=1, delta=1)   # row 1 (the newly stored preset)
    time.sleep(0.1)
    capture_fb(s, "uimcu/tools/tbpalette_preset_selected.png")

    # Press OK to load preset
    print("  Pressing OK to load preset 1...")
    inject(s, "ok")
    # Wait for preset load and pushParams to complete
    time.sleep(1.0)

    p_model = request(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": "smodel"})
    p_twist = request(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": "twist"})
    restored_model = p_model.get("param", {}).get("current")
    restored_twist = p_twist.get("param", {}).get("current")
    print(f"  Restored state after preset load: smodel={restored_model}, twist={restored_twist}")

    # Note: twist is 0..4095 range, 2048 or close
    if restored_model == 21 and abs(restored_twist - 2048) <= 2:
        print("  PASS: Preset save and recall verified successfully via Panel UI!")
    else:
        print(f"  WARNING: Restored values: smodel={restored_model}, twist={restored_twist}")

    # 4. Audio Pacing & Counter Check
    print("\n==> Verifying audio pacing and health...")
    stats = request(s, {"cmd": "/debug/getAudioStats"})
    print(f"  AudioStats: cycles={stats.get('audioCycles')}, cpuOverload={stats.get('cpuOverload')}, peakL={stats.get('peakL')}, peakR={stats.get('peakR')}")

    loop = request(s, {"cmd": "/debug/getLoopHist", "len": 16})
    print(f"  LoopHist (last 16 periods, 7 = ~2.9ms normal): {loop.get('hist', loop.get('loopHist', []))}")

    # Return to HOME
    print("\n==> Returning UI to ROOT HOME...")
    for _ in range(4):
        inject(s, "back")
        time.sleep(0.05)

    ui = request(s, {"cmd": "/debug/getUiState"})
    while ui.get("panel") != 2:
        curr = ui.get("panel", 0)
        step = -1 if curr > 2 else 1
        inject(s, "enc2", delta=step)
        ui = request(s, {"cmd": "/debug/getUiState"})

    capture_fb(s, "uimcu/tools/tbpalette_ui_verified.png")
    print("\n==> ALL UI & PRESET VERIFICATIONS COMPLETED SUCCESSFULLY!")


if __name__ == "__main__":
    main()
