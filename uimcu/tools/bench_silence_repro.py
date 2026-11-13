#!/usr/bin/env python3
"""Bench repro: total silence after loading a second plugin (all plugins,
until reboot). One console session. Reads the wedge discriminators after
each load: muteBlocks (mutex stuck -> ~1378/s growth), loopHist (frozen ->
audio task stuck), i2sErr/diag counters (codec path), vuOut (render alive).
"""
import sys
import time

sys.path.insert(0, "/home/ubuntu/ctag-tbd/uimcu/tools")
from screenshot import open_port, request, wait_ready  # noqa: E402

STAT_KEYS = ["muteBlocks", "i2sErrR", "i2sErrW", "diagStallRd", "diagStallWr",
             "diagLoopOver", "cpuOverload", "audioCycles", "vuOutL", "vuOutR"]


def stats(s, tag):
    r = request(s, {"cmd": "/debug/getAudioStats"}, timeout=5.0)
    vals = {k: r.get(k) for k in STAT_KEYS}
    print(f"[{tag}] " + " ".join(f"{k}={v}" for k, v in vals.items()))
    return vals


def loop_hist(s, tag):
    r = request(s, {"cmd": "/debug/getLoopHist", "len": 64}, timeout=5.0)
    h = r.get("hist") or r.get("data") or r
    print(f"[{tag}] loopHist={h}")


def param(s, ch, pid, tag):
    r = request(s, {"cmd": "/debug/getPluginParams", "ch": ch, "id": pid},
                timeout=5.0)
    print(f"[{tag}] ch{ch} {pid}: {r.get('param', r)}")


def load(s, ch, pid, tag):
    r = request(s, {"cmd": "/debug/setActivePlugin", "ch": ch, "id": pid},
                timeout=10.0)
    print(f"[{tag}] load ch{ch} {pid} -> {r}")
    time.sleep(1.5)


def main():
    with open_port("/dev/ttyACM0") as s:
        print("waiting for DSP link...")
        if not wait_ready(s, 30):
            print("FATAL: DSP link did not come up")
            return 1
        print("link up.")

        print("== baseline ==")
        b = stats(s, "base")
        loop_hist(s, "base")

        print("== load TBD03 ch0 ==")
        load(s, 0, "TBD03", "ch0")
        a = stats(s, "after ch0")
        loop_hist(s, "after ch0")

        print("== load TBD03 ch1 (the repro step) ==")
        load(s, 1, "TBD03", "ch1")
        c = stats(s, "after ch1")
        loop_hist(s, "after ch1")

        print("== wait 3 s, re-probe (growth check) ==")
        time.sleep(3.0)
        d = stats(s, "+3s")
        loop_hist(s, "+3s")

        print("== stereo load (TBDings ch0) ==")
        load(s, 0, "TBDings", "st")
        e = stats(s, "after stereo")
        loop_hist(s, "after stereo")

        print("== trigger mapping probes ==")
        param(s, 0, "trigger", "map")
        param(s, 1, "trigger", "map")

        print("== delta summary ==")
        for name, prev, cur in [("ch0 load", b, a), ("ch1 load", a, c),
                                ("+3s", c, d), ("stereo", d, e)]:
            mb = (cur["muteBlocks"] or 0) - (prev["muteBlocks"] or 0)
            ew = (cur["i2sErrW"] or 0) - (prev["i2sErrW"] or 0)
            lo = (cur["diagLoopOver"] or 0) - (prev["diagLoopOver"] or 0)
            print(f"  {name}: dMuteBlocks={mb} dI2sErrW={ew} dLoopOver={lo}")


if __name__ == "__main__":
    sys.exit(main())
