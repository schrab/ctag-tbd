#!/usr/bin/env python3
"""Subbotnik Pitch-jump reproduction: set pitch_A via /link/paramSet exactly
like the panel UI does, read the DSP truth back via /debug/getPluginParams
after every step. If the DSP value jumps by itself the bug is DSP-side; if
it holds, the jump lives in the C3 UI/table path."""
import json
import sys
import time

sys.path.insert(0, "/home/ubuntu/ctag-tbd/uimcu/tools")
from screenshot import open_port, request, wait_ready  # noqa: E402


def params_of(s, ch):
    r = request(s, {"cmd": "/debug/getPluginParams", "ch": ch}, timeout=5.0)
    if "params" not in r:
        return None
    return {p["id"]: p for p in r["params"] if isinstance(p, dict) and "id" in p}


def pitch(ps):
    p = ps.get("pitch_A")
    if p is None:
        return "MISSING"
    return f"cur={p.get('current')} min={p.get('min')} max={p.get('max')}"


with open_port("/dev/ttyACM0") as s:
    if not wait_ready(s, 20):
        print("link not ready", file=sys.stderr)
        sys.exit(1)
    st = request(s, {"cmd": "/debug/getProxyState"})
    print("state:", st.get("active0"), "|", st.get("active1"))

    if "Subbotnik" not in str(st.get("active0", "")):
        print("loading Subbotnik ch0 ...")
        request(s, {"cmd": "/debug/setActivePlugin", "ch": 0, "id": "Subbotnik"})
        time.sleep(2.0)

    ps = params_of(s, 0)
    print("after load :", pitch(ps))

    # The user's scenario: pitch sits at ~1500, ONE change -> ~3000.
    for v in (1500, 1550, 1600, 1650):
        request(s, {"cmd": "/link/paramSet", "ch": 0, "id": "pitch_A",
                    "key": "current", "value": v})
        time.sleep(0.15)  # let the edit land + the echo push drain
        ps = params_of(s, 0)
        print(f"set {v:5d} ->", pitch(ps))
