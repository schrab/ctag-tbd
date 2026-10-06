#!/usr/bin/env python3
"""
Drive the CTAG TBD UI over the SerialAPI UART and capture OLED screenshots.

The device exposes two debug commands on UART0 (the CP2102, /dev/ttyUSB0 by
default), using the existing STX/ETX JSON framing:

  {"cmd":"/debug/getDisplayFramebuffer"}
      -> {"screenshot":1,"w":128,"h":64,"data":"<2048 hex chars>"}

  {"cmd":"/debug/injectEvent","action":"ok"|"mod"|"back"|"enc","delta":n,"count":n}
      -> {"sent":n}

Actions are semantic UI events, not raw button transitions:
  ok   = BTN2_SHORT  -> page->onButton(2, false)
  mod  = BTN2_LONG   -> page->onButton(2, true)
  back = BTN1_SHORT  -> page->onBack()
  enc  = ENC_DELTA   -> page->onEncoder(delta)

Because the event handlers repaint synchronously, injecting an action and then
grabbing a framebuffer gives a deterministic before/after pair.

Examples
--------
  bin/dev_ui.py shot out.png                 one screenshot of the current screen
  bin/dev_ui.py shot out.png --wait 0.4      ... after letting the UI settle
  bin/dev_ui.py seq out/ --plan mix          scripted walkthrough, one PNG per step
  bin/dev_ui.py enc 3 --shot out.png         turn encoder +3, then capture
  bin/dev_ui.py ok --shot out.png            press OK, then capture

Framebuffer layout: 128x64 mono, page-major, byte = (y >> 3) * 128 + x,
bit (y & 7) set means the pixel at row y is lit. This is exactly the byte
layout Display::Flush() writes to the SSD1309, so no guessing is involved.
"""

import argparse
import json
import os
import re
import select
import subprocess
import sys
import time

STX = 0x02
ETX = 0x03
FB_W, FB_H = 128, 64
FB_BYTES = FB_W * FB_H // 8

DEFAULT_PORT = "/dev/ttyUSB0"
DEFAULT_BAUD = 115200


# --------------------------------------------------------------------------
# framing helpers
# --------------------------------------------------------------------------

def stx_frame(payload: bytes) -> bytes:
    return bytes([STX]) + payload + bytes([ETX])


def iter_frames(buf: bytes):
    """Yield every STX..ETX delimited chunk in buf, keeping the tail."""
    while True:
        start = buf.find(bytes([STX]))
        if start < 0:
            return b""
        end = buf.find(bytes([ETX]), start + 1)
        if end < 0:
            return buf[start:]
        frame = buf[start + 1:end]
        buf = buf[end + 1:]
        yield frame


# --------------------------------------------------------------------------
# device link
# --------------------------------------------------------------------------

class Device:
    def __init__(self, port: str, baud: int, verbose: bool = False):
        self.port = port
        self.baud = baud
        self.verbose = verbose
        self.pending = b""
        self._tty = None

    def open(self):
        # stty for raw mode, mirroring the documented log-capture recipe
        subprocess.run(
            ["stty", "-F", self.port, str(self.baud), "raw", "-echo"],
            check=True,
        )
        self._tty = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        # drain anything the device said before we attached
        self._drain()

    def close(self):
        if self._tty is not None:
            os.close(self._tty)
            self._tty = None

    def _drain(self, seconds=0.35):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([self._tty], [], [], 0.05)
            if r:
                try:
                    chunk = os.read(self._tty, 65536)
                except BlockingIOError:
                    continue
                if not chunk:
                    break
                self.pending += chunk

    def _read_some(self, timeout):
        r, _, _ = select.select([self._tty], [], [], timeout)
        if not r:
            return b""
        try:
            return os.read(self._tty, 65536)
        except BlockingIOError:
            return b""

    def command(self, cmd: str, timeout: float = 3.0):
        """Send one command (the bare cmd value, e.g. '/debug/getDisplayFramebuffer')."""
        return self.command_doc({"cmd": cmd}, timeout)

    def command_doc(self, doc: dict, timeout: float = 3.0):
        """Send a full command document, e.g. with extra fields.

        Log lines from ESP_LOG share this UART, so frames are filtered: a
        response is either parseable JSON carrying one of the keys we expect.
        """
        payload = json.dumps(doc, separators=(",", ":")).encode()
        os.write(self._tty, stx_frame(payload))
        deadline = time.time() + timeout
        collected = b""

        while time.time() < deadline:
            data = self._read_some(0.25)
            if not data:
                continue
            self.pending += data
            for frame in iter_frames(self.pending):
                collected += frame + b"\x00"
            self.pending = self._consumed(self.pending)

            text = collected.decode("utf-8", "replace")
            for cand in re.findall(r"\{[^{}]*\}", text):
                try:
                    obj = json.loads(cand)
                except json.JSONDecodeError:
                    continue
                # Responses are STX/ETX framed, but ESP_LOG shares the wire, so
                # require a known response key rather than trusting any frame.
                if any(k in obj for k in ("screenshot", "sent", "error",
                                          "nav", "panel", "panelName",
                                          "cursor", "edit", "values", "max")):
                    if self.verbose:
                        print(f"  <- {cand[:90]}", file=sys.stderr)
                    return obj
        raise TimeoutError(f"no response to {doc.get('cmd')} within {timeout}s")

    @staticmethod
    def _consumed(buf: bytes) -> bytes:
        """Keep only a possibly-incomplete trailing frame."""
        tail = b""
        for frame in iter_frames(buf):
            pass
        # re-run to capture remainder: iter_frames consumes as it yields
        rest = buf
        while True:
            start = rest.find(bytes([STX]))
            if start < 0:
                return rest if len(rest) < 4096 else b""
            end = rest.find(bytes([ETX]), start + 1)
            if end < 0:
                return rest[start:]
            rest = rest[end + 1:]


# --------------------------------------------------------------------------
# framebuffer -> image
# --------------------------------------------------------------------------

def fb_to_image(hexdata: str, scale: int = 4, label: str = None):
    """Convert hex framebuffer bytes to a PIL image, nearest-neighbour upscaled."""
    from PIL import Image

    raw = bytes.fromhex(hexdata)
    if len(raw) != FB_BYTES:
        raise ValueError(f"expected {FB_BYTES} bytes, got {len(raw)}")

    img = Image.new("1", (FB_W, FB_H), 0)
    px = img.load()
    for y in range(FB_H):
        page = y >> 3
        bit = y & 7
        row = page * FB_W
        for x in range(FB_W):
            if raw[row + x] & (1 << bit):
                px[x, y] = 1

    img = img.convert("L").resize((FB_W * scale, FB_H * scale), Image.NEAREST)

    if label:
        from PIL import ImageDraw
        canvas = Image.new("L", (img.width, img.height + 14), 0)
        canvas.paste(img, (0, 14))
        ImageDraw.Draw(canvas).text((2, 2), label, fill=192)
        img = canvas

    return img


# --------------------------------------------------------------------------
# scripted walkthroughs
# --------------------------------------------------------------------------

def build_plan(name: str):
    """Return a list of (step_label, [actions], settle_seconds).

    Panels are ordered PANEL_MIX=0 PANEL_TAPE=1 PANEL_HOME=2 PANEL_MOD=3
    PANEL_PARAMS=4 (+PANEL_MIDI=5), so enc-N from HOME(2) wraps around.
    """
    plans = {
        # Walk to the mixer, enter it, open edit mode, move the cursor, turn a fader.
        # NOTE: UIMenu halves encoder deltas with magnitude > 1 in ROOT, so a
        # 2-panel move (HOME=2 -> MIX=0) needs delta -4, not -2.
        "mix": [
            ("root", [("enc", -4)], 0.6),
            ("mix-panel", [("ok", 0)], 0.5),
            ("mix-edit", [("ok", 0)], 0.5),
            ("mix-edit-cursor", [("enc", 2)], 0.5),
            ("mix-fader-turn", [("enc", 4)], 0.6),
        ],
        # Sweep every panel. ROOT step from HOME(2) to panel N: delta = 2*(N-2),
        # except panel 0/1 which wrap negative.
        "panels": [
            ("panel-0-mix", [("enc", -4), ("ok", 0)], 0.6),
            ("panel-1-tape", [("back", 0), ("enc", -2), ("ok", 0)], 0.6),
            ("panel-2-home", [("back", 0), ("enc", 2), ("ok", 0)], 0.6),
            ("panel-3-mod", [("back", 0), ("enc", 2), ("ok", 0)], 0.6),
            ("panel-4-params", [("back", 0), ("enc", 2), ("ok", 0)], 0.6),
        ],
    }
    if name not in plans:
        raise SystemExit(f"unknown plan '{name}', have: {', '.join(plans)}")
    return plans[name]


# --------------------------------------------------------------------------
# cli
# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action",
                    choices=["shot", "seq", "enc", "ok", "mod", "back", "state"])
    ap.add_argument("arg", nargs="?", default=None,
                    help="output png (shot/seq) | delta (enc) | plan name (seq)")
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--wait", type=float, default=0.35,
                    help="seconds to let the UI settle before capturing")
    ap.add_argument("--count", type=int, default=1, help="enc repeat count")
    ap.add_argument("--plan", default=None, help="scripted walkthrough name")
    ap.add_argument("--shot", default=None, metavar="PNG",
                    help="capture to PNG after performing the action")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    dev = Device(args.port, args.baud, args.verbose)
    dev.open()
    try:
        if args.action == "state":
            print(json.dumps(ui_state(dev)))

        elif args.action == "shot":
            out = args.arg or args.shot or "screenshot.png"
            time.sleep(args.wait)
            save_shot(dev, out, "current", args.scale)
            print(out)

        elif args.action == "seq":
            plan_name = args.plan or args.arg or "mix"
            outdir = args.arg if args.plan else "."
            os.makedirs(outdir, exist_ok=True)
            for i, (step, actions, settle) in enumerate(build_plan(plan_name)):
                for kind, delta in actions:
                    perform(dev, kind, delta)
                time.sleep(settle)
                st = ui_state(dev)
                out = os.path.join(outdir, f"{plan_name}_{i:02d}_{step}.png")
                save_shot(dev, out, f"{plan_name} {i:02d} {step} [{st['panelName']}]",
                          args.scale)
                print(f"{out}  state={st}")

        else:
            if args.action == "enc":
                delta = int(args.arg) if args.arg else 0
                perform(dev, "enc", delta, args.count)
            else:
                perform(dev, args.action, 0)
            time.sleep(args.wait)
            out = args.shot or (args.arg if os.path.splitext(args.arg or "")[1] else None)
            if out:
                save_shot(dev, out, f"after {args.action}", args.scale)
                print(out)
    finally:
        dev.close()


def ui_state(dev: Device):
    return dev.command("/debug/getUiState")


def wait_ui_state(dev: Device, nav=None, panel=None, tries=25, delay=0.12):
    """Poll /debug/getUiState until it matches.

    Physical encoder/button input races with injected events (someone touching
    the device mid-run), so every scripted step asserts rather than assumes.
    """
    last = None
    for _ in range(tries):
        last = ui_state(dev)
        ok = (nav is None or last.get("nav") == nav) and \
             (panel is None or last.get("panel") == panel)
        if ok:
            return last
        time.sleep(delay)
    raise SystemExit(f"UI state never became nav={nav} panel={panel}; last={last}")


def perform(dev: Device, kind: str, delta: int = 0, count: int = 1):
    doc = {"cmd": "/debug/injectEvent", "action": kind}
    if kind == "enc":
        doc["delta"] = delta
        doc["count"] = count
    resp = dev.command_doc(doc)
    if "error" in resp:
        raise SystemExit(f"inject {kind} failed: {resp}")
    # a single loop iteration is 20ms; give the UI task room to consume it
    time.sleep(0.12)


def save_shot(dev: Device, path: str, label: str, scale: int):
    resp = dev.command("/debug/getDisplayFramebuffer", timeout=4.0)
    if "error" in resp:
        raise SystemExit(f"screenshot failed: {resp}")
    img = fb_to_image(resp["data"], scale=scale, label=label)
    img.save(path)
    print(f"{path}  ({resp.get('w')}x{resp.get('h')} @ {scale}x)", file=sys.stderr)


if __name__ == "__main__":
    main()
