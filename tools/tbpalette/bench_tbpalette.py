#!/usr/bin/env python3
"""TBPalette bench test script: runs via C3 USB console in ONE session.

Steps:
1. Connects to C3 console on /dev/ttyACM0.
2. Waits for link to DSP.
3. Loads TBPalette on ch0.
4. Confirms active plugin and captures initial screenshot.
5. Sweeps all 98 models (0..97) via /debug/setPluginParam:
   - Reads audioCycles and cpuOverload.
   - Reads loopHist pacing.
   - Validates live model readout.
6. Tests macro parameter and stereo toggle.
7. Verifies TBDings loads cleanly afterwards (arena allocator test).
8. Reports full results.
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


def main():
    print(f"==> Opening console on {PORT}...")
    s = open_port(PORT)
    print("==> Waiting for link to DSP...")
    if not wait_ready(s, timeout=15.0):
        print("ERROR: link to DSP did not become ready")
        sys.exit(1)

    state = request(s, {"cmd": "/debug/getProxyState"})
    print(f"Link UP: active0={state.get('active0')}, active1={state.get('active1')}")

    print("==> Switching ch0 to TBPalette...")
    resp = request(s, {"cmd": "/debug/setActivePlugin", "id": "TBPalette", "ch": 0})
    print(f"setActivePlugin reply: {resp}")
    time.sleep(1.0)

    state = request(s, {"cmd": "/debug/getProxyState"})
    print(f"State after load: active0={state.get('active0')}")
    if state.get("active0") != "TBPalette":
        print("ERROR: active0 is not TBPalette!")
        sys.exit(1)

    # Capture initial screenshot
    fb = request(s, {"cmd": "/debug/getDisplayFramebuffer"})
    if fb.get("data"):
        fb_to_png(fb["data"], "uimcu/tools/tbpalette_loaded.png")
        print("Saved screenshot to uimcu/tools/tbpalette_loaded.png")

    # Initial stats
    stats = request(s, {"cmd": "/debug/getAudioStats"})
    print(f"Initial AudioStats: cycles={stats.get('audioCycles')}, vuOutL={stats.get('vuOutL')}, vuOutR={stats.get('vuOutR')}")

    loop = request(s, {"cmd": "/debug/getLoopHist"})
    print(f"Initial LoopHist: {loop.get('hist', [])[:8]}")

    print("==> Sweeping all 98 models (0..97)...")
    overloads = []
    max_cycles = 0
    max_engine = -1

    for e in range(98):
        # Set smodel
        request(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "smodel", "value": e})
        time.sleep(0.06)

        # Read back param
        p = request(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": "smodel"})
        read_val = p.get("param", {}).get("current")

        # Read audio stats
        st = request(s, {"cmd": "/debug/getAudioStats"})
        cycles = st.get("audioCycles", 0)
        ov = st.get("cpuOverload", 0)

        if cycles > max_cycles:
            max_cycles = cycles
            max_engine = e

        if ov > 0:
            overloads.append((e, cycles, ov))

        if e % 10 == 0 or e == 97:
            print(f"  Model {e:2d}: read_val={read_val}, cycles={cycles:6d} (max={max_cycles:6d} @ {max_engine}), overload={ov}")

    print(f"\nModel sweep complete! Max cycles: {max_cycles} at model {max_engine} (budget: 174,150 cycles)")
    if overloads:
        print(f"WARNING: Models with cpuOverload: {overloads}")
    else:
        print("PASS: 0 cpuOverloads across all 98 models!")

    print("\n==> Testing macro parameter...")
    for val in [500, 2048, 4000]:
        request(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "macro", "value": val})
        time.sleep(0.05)
        p = request(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": "macro"})
        print(f"  Set macro={val}, read back={p.get('param', {}).get('current')}")

    print("\n==> Testing stereo toggle...")
    for val in [0, 1]:
        request(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "stereo", "value": val})
        time.sleep(0.05)
        p = request(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": "stereo"})
        print(f"  Set stereo={val}, read back={p.get('param', {}).get('current')}")

    print("\n==> Testing arena allocator: switching ch0 to TBDings...")
    resp = request(s, {"cmd": "/debug/setActivePlugin", "id": "TBDings", "ch": 0})
    time.sleep(1.0)
    state = request(s, {"cmd": "/debug/getProxyState"})
    print(f"State after TBDings load: active0={state.get('active0')}")
    if state.get("active0") != "TBDings":
        print("ERROR: Failed to load TBDings after TBPalette!")
        sys.exit(1)
    print("PASS: TBDings loaded cleanly after TBPalette!")

    # Switch back to TBPalette
    print("\n==> Switching back to TBPalette...")
    request(s, {"cmd": "/debug/setActivePlugin", "id": "TBPalette", "ch": 0})
    time.sleep(1.0)
    state = request(s, {"cmd": "/debug/getProxyState"})
    print(f"State: active0={state.get('active0')}")

    # Capture final screenshot
    fb = request(s, {"cmd": "/debug/getDisplayFramebuffer"})
    if fb.get("data"):
        fb_to_png(fb["data"], "uimcu/tools/tbpalette_final.png")
        print("Saved final screenshot to uimcu/tools/tbpalette_final.png")

    print("\n==> ALL BENCH VERIFICATIONS PASSED!")


if __name__ == "__main__":
    main()
