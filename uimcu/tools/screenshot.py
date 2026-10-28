#!/usr/bin/env python3
"""C3 panel bench console client: /debug commands + framebuffer screenshots.

The C3's USB-Serial-JTAG port is the bench channel since the P6 cutover
(A1S UART0 bench task removed). Framing is STX/ETX JSON, same as the link.

Usage:
  screenshot.py --out shot.png                 # grab the panel framebuffer
  screenshot.py --cmd '{"cmd":"/debug/getAudioStats"}'
  screenshot.py --port /dev/ttyACM0 --out shot.png

DSP-side commands (/debug/getAudioStats, /debug/setElementsMode, ...) are
forwarded by the C3's DebugLink over the panel link; replies carry a
"cmd" echo. Errors come back as {"cmd":...,"error":"dsp offline"|"timeout"}.
"""
import argparse
import json
import sys
import time

import serial

STX, ETX = 0x02, 0x03


def open_port(path: str):
    # Bench rule: set dtr/rts BEFORE open — line transitions reset the chip.
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
                reply = json.loads(bytes(buf).decode(errors="replace"))
                if reply.get("seq") == seq:
                    return reply
                in_frame = False
            elif in_frame:
                buf.append(ch)
    raise TimeoutError(f"no reply to {cmd_obj['cmd']}")


def fb_to_png(hexdata: str, out_path: str):
    from PIL import Image

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


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--out", default="panel_shot.png")
    ap.add_argument("--cmd", help='raw /debug command JSON, e.g. \'{"cmd":"/debug/getAudioStats"}\'')
    args = ap.parse_args()

    with open_port(args.port) as s:
        if args.cmd:
            reply = request(s, json.loads(args.cmd))
            print(json.dumps(reply, indent=1))
            return 0
        reply = request(s, {"cmd": "/debug/getDisplayFramebuffer"}, timeout=5.0)
        if "data" not in reply:
            print(json.dumps(reply, indent=1), file=sys.stderr)
            return 1
        fb_to_png(reply["data"], args.out)
        print(f"wrote {args.out} ({reply.get('w')}x{reply.get('h')})")
        return 0


if __name__ == "__main__":
    sys.exit(main())
