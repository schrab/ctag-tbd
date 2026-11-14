#!/usr/bin/env python3
"""Mic verification: sample the input-peak meter (vuInL/R) while the user
taps the mic with input source = MIC. MIX page must be visible (arms the
peak pass)."""
import sys
import time

sys.path.insert(0, "/home/ubuntu/ctag-tbd/uimcu/tools")
from screenshot import open_port, request, wait_ready  # noqa: E402


def main():
    with open_port("/dev/ttyACM0") as s:
        if not wait_ready(s, 30):
            print("FATAL: no link")
            return 1
        print("link up. TAP THE MIC NOW (input source = MIC, MIX page open)")
        max_l = max_r = 0
        for i in range(24):
            r = request(s, {"cmd": "/debug/getAudioStats"}, timeout=5.0)
            l, rr = r.get("vuInL", -1), r.get("vuInR", -1)
            max_l, max_r = max(max_l, l), max(max_r, rr)
            print(f"  {i*0.5:4.1f}s vuInL={l} vuInR={rr}")
            time.sleep(0.5)
        print(f"== peaks: L={max_l} R={max_r} (0..1000 scale; "
              f"taps should reach hundreds) ==")


if __name__ == "__main__":
    sys.exit(main())
