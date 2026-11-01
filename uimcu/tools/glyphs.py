#!/usr/bin/env python3
"""Capture the norns 6x7 glyph chart from the C3 panel, one page per PNG.

Renders the full glyph inventory of the single UI font (norns 6x7) on the
OLED via /debug/drawGlyphs and screenshots each page:
  page 0 = ASCII 0x20..0x7F, pages 1..2 = extended range (138 glyphs),
  page 3 = unused (blank). Output: panel_glyphs_{ascii,ext1,ext2,ext3}.png
next to this script.

IMPORTANT: everything runs in ONE USB session — opening the console resets
the C3 (rst:0x15 USB_UART_CHIP_RESET), so a separate screenshot.py call per
page would wipe the held chart between captures.

Usage:  python3 glyphs.py [--port /dev/ttyACM0]
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screenshot import open_port, request, wait_ready, fb_to_png

PAGES = [("ascii", 0), ("ext1", 1), ("ext2", 2), ("ext3", 3)]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default="/dev/ttyACM0")
    here = os.path.dirname(os.path.abspath(__file__))
    args = ap.parse_args()

    with open_port(args.port) as s:
        if not wait_ready(s):
            print("warning: DSP link not ready", file=sys.stderr)
        for name, page in PAGES:
            r = request(s, {"cmd": "/debug/drawGlyphs", "page": page})
            if not r.get("ok"):
                print("drawGlyphs failed:", r, file=sys.stderr)
                return 1
            time.sleep(0.3)
            fb = request(s, {"cmd": "/debug/getDisplayFramebuffer"}, timeout=5.0)
            if "data" not in fb:
                print("fb failed:", fb, file=sys.stderr)
                return 1
            out = os.path.join(here, f"panel_glyphs_{name}.png")
            fb_to_png(fb["data"], out)
            print(f"wrote {out}")
        request(s, {"cmd": "/debug/drawGlyphs", "page": -1})
        print("released")
    return 0


if __name__ == "__main__":
    sys.exit(main())
