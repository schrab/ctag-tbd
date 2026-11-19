#!/usr/bin/env python3
"""Strum-engine bench verification, take 2 (2026-11-20 session).

One console session (the open resets the C3 = the param-table rebuild the
mui push needs). /link/seqSet does NOT forward through the bench relay
(the DebugLink forwards /debug/* only), so the ARP algo is driven through
the panel UI by getUiState/getProxyState read-back.

Verifies:
  1. the new TBDings params exist (strumStyle/strumSpread)
  2. VARG Chord auto-route drives the ARP (algo 6 = encoded 18, TRM16)
  3. SEQ live word moves (gates firing), audio stats clean, loopHist flat
"""
import sys
import time

sys.path.insert(0, "/home/ubuntu/ctag-tbd/uimcu/tools")
from screenshot import open_port, request, wait_ready  # noqa: E402

OUT = "/home/ubuntu/ctag-tbd/uimcu/tools"


def req(s, cmd, timeout=5.0):
    r = request(s, cmd, timeout=timeout, retries=1)
    time.sleep(0.12)   # relay pacing
    return r


def ev(s, action, delta=0, count=1):
    request(s, {"cmd": "/debug/injectEvent", "action": action,
                "delta": delta, "count": count}, timeout=3.0)
    time.sleep(0.15)


def set_param(s, pid, val, key="current"):
    r = req(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": pid,
                "key": key, "value": int(val)})
    if "error" in r:
        print(f"  setPluginParam {pid}={val} -> {r}")


def arp_algo_from_proxy(s):
    st = req(s, {"cmd": "/debug/getProxyState"}, timeout=8.0)
    ss = st.get("seqstate") or {}
    return ss.get("a1algo", -1), ss.get("a1p", []), st


def main():
    with open_port("/dev/ttyACM0") as s:
        print("waiting for DSP link (C3 rebooting from the open)...")
        if not wait_ready(s, 30):
            print("FATAL: no DSP link")
            return 1
        time.sleep(1.0)

        print("== 1. new params present ==")
        for pid in ("strumStyle", "strumSpread"):
            r = req(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": pid})
            p = r.get("param", {})
            print(f"  {pid}: max={p.get('max')} cur={p.get('current')} "
                  f"texts={p.get('texts')}")

        print("== 2. configure TBDings strum ==")
        set_param(s, "vargChord", 1)      # auto-route ON
        set_param(s, "strumStyle", 1)     # pendulum
        set_param(s, "strumSpread", 900)  # ~9.9 ms
        set_param(s, "chords", 3)         # manual fallback voicing (m)

        algo, p, _ = arp_algo_from_proxy(s)
        print(f"== 3. ARP1 algo currently {algo} p={p} ==")
        target = 18                       # encoded VARG Chord range 1
        if algo != target:
            print(f"  wheel to SEQ panel, open ARP 1, algo -> {target}")
            for _ in range(8):
                ui = req(s, {"cmd": "/debug/getUiState"})
                if ui.get("panelName") == "SEQ":
                    break
                ev(s, "enc2", 1)          # wheel right
            else:
                print("FATAL: never reached SEQ panel")
                return 1
            ev(s, "ok")                   # SEQ MAIN
            ev(s, "enc2", 1)
            ev(s, "enc2", 1)              # cursor Gate1 -> ARP 1
            ev(s, "ok")                   # ARP 1 page
            time.sleep(0.3)
            steps = (target - algo) % 21
            for _ in range(steps):        # one detent at a time
                ev(s, "enc", 1)
            # leave the page: back to MAIN, back to ROOT
            ev(s, "back")
            ev(s, "back")
            algo2, p2, _ = arp_algo_from_proxy(s)
            print(f"  algo now {algo2} p={p2}")
            if algo2 != target:
                print("FATAL: algo did not land on 18")
                return 1

        print("== 4. live state ==")
        time.sleep(2.0)
        for i in range(3):
            st = req(s, {"cmd": "/debug/getProxyState"}, timeout=8.0)
            sq = st.get("sq")
            if sq is not None:
                a1g = (sq >> 2) & 1
                a1acc = (sq >> 4) & 1
                step = (sq >> 8) & 0xF
                note = (sq >> 16) & 0xFF
                print(f"  poll{i}: a1gate={a1g} acc={a1acc} step={step} "
                      f"note={note}")
            time.sleep(0.4)

        stats = req(s, {"cmd": "/debug/getAudioStats"}, timeout=6.0)
        print(f"  audioStats: {stats}")

        hist = req(s, {"cmd": "/debug/getLoopHist", "len": 64}, timeout=6.0)
        h = hist.get("hist")
        if h:
            print(f"  loopHist[{len(h)}] first 12: {h[:12]}")
        ui = req(s, {"cmd": "/debug/getUiState"})
        print(f"  uiState: {ui}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
