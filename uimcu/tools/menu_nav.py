#!/usr/bin/env python3
"""Verified menu navigation for bench screenshot staging.

PROTOCOL (2026-11-14): menu navigation exists ONLY for staging UI
screenshots. Parameter reads/writes and plugin loads must NOT menu-dive —
use /debug/getPluginParams, /debug/setPluginParam (key=current|cv|trig),
/debug/setActivePlugin, /debug/getConfig directly.

Pitfalls this module encodes (learned the hard way):
- injectEvent actions are INVERTED from what the names suggest:
  'enc'  = VALUE encoder (right encoder)
  'enc2' = LIST encoder (left/wheel encoder)
- A count>1 burst arrives faster than the pages' 100 ms momentum window
  -> encoderAccel multiplies the steps -> overshoot ("wrong menu
  direction"). Always send ONE event per step with >=150 ms spacing.
- OK = BTN2_SHORT ('ok'), long = BTN2_LONG ('mod'), double = BACK ('back').
- Transitions are consumed on the 20 ms UI tick; wheel steps are read
  back from /debug/getUiState instead of assumed.
"""
import sys
import time

sys.path.insert(0, "/home/ubuntu/ctag-tbd/uimcu/tools")
from screenshot import request  # noqa: E402

# Panel wheel order (getUiState panel indices; names may vary per branch).
PANELS = ["MIX", "TAPE", "HOME", "MOD", "SEQ", "PARAMS"]


def step(s, action, delta=0, wait=0.18):
    """One injected event, paced past the momentum window."""
    r = request(s, {"cmd": "/debug/injectEvent", "action": action,
                    "delta": delta, "count": 1}, timeout=3.0)
    time.sleep(wait)
    return r


def ui_state(s):
    return request(s, {"cmd": "/debug/getUiState"}, timeout=5.0)


def ensure_root(s, max_tries=4):
    """Leave PANEL_IN (nav=1) if we are inside a panel. The wheel only
    moves at ROOT; at PANEL_IN the encoders drive the page and 'back'
    (BTN1_SHORT) exits — pages may consume it via onBack(), so retry."""
    for _ in range(max_tries):
        st = ui_state(s)
        if int(st.get("nav", 0)) == 0:
            return st
        step(s, "back")
    st = ui_state(s)
    if int(st.get("nav", 0)) != 0:
        raise RuntimeError("could not return to ROOT")
    return st


def wheel(s, target):
    """Navigate the top-level panel wheel to a panel by name. Read-back
    driven; the wheel CLAMPS at the ends (no wrap-around) and only moves
    at ROOT — exits PANEL_IN first."""
    target = target.upper()
    if target not in PANELS:
        raise ValueError(f"unknown panel {target}; known: {PANELS}")
    ensure_root(s)
    tgt_idx = PANELS.index(target)
    for _ in range(2 * len(PANELS)):
        st = ui_state(s)
        cur = int(st.get("panel", -1))
        name = str(st.get("panelName", "")).upper()
        if (name == target or cur == tgt_idx) and int(st.get("nav", 0)) == 0:
            return st
        if cur < tgt_idx:
            step(s, "enc2", 1)
        elif cur > tgt_idx:
            step(s, "enc2", -1)
        else:
            break
    raise RuntimeError(f"wheel never reached {target}")


def enter(s, wait=0.25):
    return step(s, "ok", wait=wait)


def back(s, wait=0.25):
    return step(s, "back", wait=wait)


def value_step(s, delta, wait=0.18):
    """VALUE encoder (right) — live value edits."""
    return step(s, "enc", delta, wait=wait)


def cursor_step(s, delta, wait=0.18):
    """LIST encoder (left/wheel) — cursor movement inside a page."""
    return step(s, "enc2", delta, wait=wait)
