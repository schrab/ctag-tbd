#!/usr/bin/env python3
"""Strum bench final poll (2026-11-20): the ARP sits on VARG Chord
(encoded 20 = algo 6 range 3, PROG 10 i-bVII, RHYTHM 7 = TRM8) with
TBDings strumStyle=Pendulum, strumSpread=900. One session: live-word
poll, audio stats, loopHist."""
import sys
import time

sys.path.insert(0, "/home/ubuntu/ctag-tbd/uimcu/tools")
from screenshot import open_port, request, wait_ready  # noqa: E402


def req(s, cmd, timeout=6.0):
    r = request(s, cmd, timeout=timeout, retries=1)
    time.sleep(0.12)
    return r


def main():
    with open_port("/dev/ttyACM0") as s:
        print("waiting for DSP link...")
        if not wait_ready(s, 30):
            print("FATAL: no DSP link")
            return 1
        time.sleep(1.0)

        st = req(s, {"cmd": "/debug/getProxyState"}, timeout=8.0)
        print(f"proxy: active0={st.get('active0')} "
              f"seqstate={st.get('seqstate')}")

        print("== live word (25 Hz snapshot), 8 polls ==")
        for i in range(8):
            st = req(s, {"cmd": "/debug/getProxyState"}, timeout=8.0)
            sq = st.get("sq")
            if sq is not None:
                g = (sq >> 2) & 3
                acc = (sq >> 4) & 3
                step0 = (sq >> 8) & 0xF
                step1 = (sq >> 12) & 0xF
                n0 = (sq >> 16) & 0xFF
                print(f"  poll{i}: gate={g:02b} acc={acc:02b} "
                      f"a1step={step0:2d} a1note={n0}")
            time.sleep(0.35)

        stats = req(s, {"cmd": "/debug/getAudioStats"}, timeout=6.0)
        print(f"audioStats: {stats}")

        hist = req(s, {"cmd": "/debug/getLoopHist", "len": 64}, timeout=6.0)
        h = hist.get("hist")
        if h:
            print(f"loopHist[{len(h)}] first 12: {h[:12]}")
        ui = req(s, {"cmd": "/debug/getUiState"})
        print(f"uiState: {ui}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
