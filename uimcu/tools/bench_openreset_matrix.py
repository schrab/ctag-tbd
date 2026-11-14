#!/usr/bin/env python3
"""Serial-open reset experiment matrix.

Detector: navigate the panel wheel (inject enc2 x2), then reopen the port
with each open variant and read /debug/getUiState. If 'panel' still shows
the navigated value -> no reset; back to default -> the open (or close)
reset the C3. The wait_ready duration is itself a detector (a reset forces
a ~7 s reboot + re-handshake).
"""
import sys
import time

import serial

sys.path.insert(0, "/home/ubuntu/ctag-tbd/uimcu/tools")
from screenshot import request  # noqa: E402

PORT = "/dev/ttyACM0"
DEFAULT_PANEL = 0  # MIX


def open_variant(pre):
    s = serial.Serial()
    s.port = PORT
    s.baudrate = 115200
    s.dsrdtr = False
    s.rtscts = False
    if pre is not None:
        s.dtr = pre[0]
        s.rts = pre[1]
    t0 = time.time()
    s.timeout = 0.1
    s.open()
    open_t = time.time() - t0
    if pre is not None:
        s.dtr = pre[0]
        s.rts = pre[1]
    return s, open_t


def ready_timed(s):
    t0 = time.time()
    end = t0 + 20
    while time.time() < end:
        try:
            r = request(s, {"cmd": "/debug/getProxyState"}, timeout=1.0)
            if r.get("active0"):
                return time.time() - t0
        except Exception:
            pass
        time.sleep(0.4)
    return -1


def ui(s):
    return request(s, {"cmd": "/debug/getUiState"}, timeout=5.0)


def report(tag, ui_state):
    print(f"  {tag}: panel={ui_state.get('panel')} "
          f"({ui_state.get('panelName')}) nav={ui_state.get('nav')}")


def main():
    print("== P1 baseline: variant A (pre dtr/rts False, no flags) ==")
    s, ot = open_variant((False, False))
    w = ready_timed(s)
    print(f"  open={ot*1000:.0f} ms, link-ready wait={w:.1f}s "
          f"({'RESET (long wait)' if w > 3 else 'no reset (instant)'})")
    u = ui(s); report("after open", u)
    r = request(s, {"cmd": "/debug/injectEvent", "action": "enc2",
                    "delta": 2, "count": 2}, timeout=3.0)
    time.sleep(0.5)
    u = ui(s); report("after wheel nav", u)
    s.close()
    print("  closed")

    print("== P2 variant C: dtr/rts held TRUE from pre-open, never dropped ==")
    s, ot = open_variant((True, True))
    w = ready_timed(s)
    print(f"  open={ot*1000:.0f} ms, link-ready wait={w:.1f}s "
          f"({'RESET (long wait)' if w > 3 else 'no reset (instant)'})")
    u = ui(s); report("after reopen", u)
    print("  close + reopen once more to test the CLOSE path:")
    s.close()
    s, ot = open_variant((True, True))
    w = ready_timed(s)
    print(f"  open={ot*1000:.0f} ms, link-ready wait={w:.1f}s "
          f"({'RESET (long wait)' if w > 3 else 'no reset (instant)'})")
    u = ui(s); report("after close+reopen", u)

    print("== P3 control: variant A again (should reset back to default) ==")
    s.close()
    s, ot = open_variant((False, False))
    w = ready_timed(s)
    print(f"  open={ot*1000:.0f} ms, link-ready wait={w:.1f}s "
          f"({'RESET (long wait)' if w > 3 else 'no reset (instant)'})")
    u = ui(s); report("after reopen-A", u)
    s.close()


if __name__ == "__main__":
    main()
