#!/usr/bin/env python3
"""Elements performance sweep on the A1S (classic ESP32, helper on core 0).

One C3 console session (ttyACM0-class, auto-detected) drives:
  load Elements ch0 -> for each resolution: /debug/setElementsMode, settle,
  sample getAudioStats (helper per-block cycles, audio-task cycles, overload
  counters, loopHist) -> /debug/enablePerf windows -> restore auto ladder.

In parallel a raw listener on the A1S UART (ttyUSB0, 115200, DTR/RTS held —
NO reset) captures the 5 s PERF_MONITOR windows (Elements_Helper /
Elements_Core1) that the A1S prints on its own console.

Budgets (AGENTS): block = 1000 µs @ 32 kHz (240k cycles @ 240 MHz); 85% =
the ladder's step-down line. Documented reference (2026-11-14): helper avg
324.8 µs / max 484.9 µs, Core1 avg 23.5 µs @ 52 modes.
"""
import argparse
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screenshot import open_port, request, wait_ready  # noqa: E402

CYCLES_PER_US = 240.0  # ESP32 @ 240 MHz
STAT_KEYS = ["audioCycles", "cpuOverload", "diagLoopOver", "i2sErrR", "i2sErrW",
             "elementsHelperCycles", "elementsHelperMaxCycles",
             "elementsHelperOverload", "elementsHelperResolution",
             "elementsHelperMisses", "stackHighWater"]


def stats(s):
    r = request(s, {"cmd": "/debug/getAudioStats"}, timeout=5.0)
    return {k: r.get(k) for k in STAT_KEYS}


def loop_hist(s, n=16):
    r = request(s, {"cmd": "/debug/getLoopHist", "len": n}, timeout=5.0)
    return r.get("hist") or r.get("data") or []


class UartCapture:
    """Raw A1S console listener (separate port, never resets the chip)."""

    def __init__(self, path, logfile):
        self.path, self.logfile = path, logfile
        self.lines = []
        self._stop = threading.Event()
        self._t = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._t.start()

    def _run(self):
        import serial
        with open(self.logfile, "ab", buffering=0) as f:
            while not self._stop.is_set():
                try:
                    s = serial.Serial(port=self.path, baudrate=115200,
                                      timeout=0.3)
                    s.dtr = True
                    s.rts = True
                except Exception:
                    time.sleep(0.3)
                    continue
                try:
                    while not self._stop.is_set():
                        data = s.read(1024)
                        if data:
                            f.write(data)
                            for ln in data.decode(errors="replace").splitlines():
                                if "Elements" in ln or "PERF" in ln:
                                    self.lines.append(ln.strip())
                except Exception:
                    try:
                        s.close()
                    except Exception:
                        pass
                    time.sleep(0.2)

    def stop(self):
        self._stop.set()
        self._t.join(timeout=2.0)

    def perf_lines(self):
        out, self.lines = self.lines, []
        return out


def sweep_mode(s, uart, mode, samples, dwell):
    r = request(s, {"cmd": "/debug/setElementsMode", "value": mode}, timeout=5.0)
    time.sleep(1.5)  # settle: helper re-init renders one silent block
    vals = {k: [] for k in STAT_KEYS}
    prev = stats(s)
    for _ in range(samples):
        time.sleep(dwell)
        cur = stats(s)
        for k in STAT_KEYS:
            vals[k].append(cur.get(k))
        prev = cur
    h = loop_hist(s)
    res = vals["elementsHelperResolution"][-1]
    hc = [v / CYCLES_PER_US for v in vals["elementsHelperCycles"] if v]
    ac = [v / CYCLES_PER_US for v in vals["audioCycles"] if v]
    ov = sum(1 for v in vals["elementsHelperOverload"] if v)
    lo = [v for v in vals["diagLoopOver"] if v is not None]
    lo_d = (max(lo) - min(lo)) if lo else 0
    print(f"mode {mode:>2} -> applied {res:>2} | "
          f"helper µs avg {sum(hc)/len(hc):6.1f} max {max(hc):6.1f} | "
          f"audioTask µs avg {sum(ac)/len(ac):6.1f} max {max(ac):6.1f} | "
          f"helperOv {ov}/{samples} loopOver+{lo_d} | "
          f"hist {h[:8]}")
    return {"mode": mode, "res": res, "helper_us": hc, "audio_us": ac}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modes", default="12,16,20,24,32,52")
    ap.add_argument("--samples", type=int, default=8)
    ap.add_argument("--dwell", type=float, default=0.4)
    ap.add_argument("--dsp-port", default="/dev/ttyUSB0")
    ap.add_argument("--no-load", action="store_true")
    args = ap.parse_args()

    uart = UartCapture(args.dsp_port, "/tmp/a1s_perf.log")
    uart.start()

    with open_port(_detect_c3()) as s:
        s.write_timeout = 2.0
        print("link ready:", wait_ready(s, 20))
        if not args.no_load:
            r = request(s, {"cmd": "/debug/setActivePlugin", "ch": 0,
                            "id": "Elements"}, timeout=12.0)
            print("load Elements ch0 ->", r)
            time.sleep(3.0)
        boot = stats(s)
        print(f"boot: resolution={boot['elementsHelperResolution']} "
              f"helperCycles={boot['elementsHelperCycles']} "
              f"({(boot['elementsHelperCycles'] or 0)/CYCLES_PER_US:.1f} µs) "
              f"max={boot['elementsHelperMaxCycles']} "
              f"misses={boot['elementsHelperMisses']} "
              f"audioCycles={boot['audioCycles']}")

        results = []
        for m in [int(x) for x in args.modes.split(",")]:
            results.append(sweep_mode(s, uart, m, args.samples, args.dwell))

        # PERF windows: 15 s capture at the current mode, then restore.
        print("-- enablePerf 1 (15 s UART capture) --")
        request(s, {"cmd": "/debug/enablePerf", "value": 1}, timeout=5.0)
        time.sleep(15.0)
        request(s, {"cmd": "/debug/enablePerf", "value": 0}, timeout=5.0)
        for ln in uart.perf_lines()[-12:]:
            print("  UART:", ln[:170])

        # Restore the production auto ladder.
        r = request(s, {"cmd": "/debug/setElementsMode", "value": -1},
                    timeout=5.0)
        print("restore auto ladder:", r)

    print("== summary (decayed-state; excited state needs strikes) ==")
    for r in results:
        hc, ac = r["helper_us"], r["audio_us"]
        print(f"  mode {r['mode']:>2} (applied {r['res']:>2}): "
              f"helper {sum(hc)/len(hc):6.1f} µs avg / {max(hc):6.1f} µs max "
              f"| audioTask {sum(ac)/len(ac):6.1f} µs avg / {max(ac):6.1f} µs max "
              f"| budget 1000 µs @ 32 kHz")
    uart.stop()
    return 0


def _detect_c3():
    import glob
    import json
    import serial
    for path in sorted(glob.glob("/dev/ttyACM*")):
        try:
            s = serial.Serial(port=path, baudrate=115200, timeout=0.1,
                              write_timeout=0.5)
            s.dtr = True
            s.rts = True
        except Exception:
            continue
        time.sleep(0.2)
        s.reset_input_buffer()
        payload = json.dumps({"cmd": "/debug/getUiState", "seq": 77},
                             separators=(",", ":")).encode()
        ok = False
        buf = bytearray()
        in_frame = False
        try:
            s.write(bytes([0x02]) + payload + bytes([0x03]))
            deadline = time.time() + 0.6
            while time.time() < deadline:
                for ch in s.read(256) or b"":
                    if ch == 0x02:
                        in_frame, buf = True, bytearray()
                    elif ch == 0x03 and in_frame:
                        ok = b"panelName" in buf
                        break
                    elif in_frame:
                        buf.append(ch)
                if ok:
                    break
        except Exception:
            pass
        s.close()
        if ok:
            return path
    raise SystemExit("FATAL: no C3 console found")


if __name__ == "__main__":
    sys.exit(main())
