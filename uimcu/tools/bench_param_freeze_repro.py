#!/usr/bin/env python3
"""Bench repro: audio freezes/silences WHEN a parameter is changed (BBA2,
TBDings reported; 2026-11-15). Panel stays alive in the wild, so the DSP
link task never wedged — hunt the audio-task/output path instead.

One console session. Modes:
  poll   — user drives the panel (loads plugin, plays, edits params);
           script only polls discriminators. Enter in a terminal forces
           the post-freeze snapshot; Ctrl-C ends it.
  sweep  — bench-driven structure sweep on the ACTIVE ch0 plugin (no load
           unless --load). USER MUST excite the plugin (MIDI/input) and is
           asked to park the panel on the MIX page: that arms the peak
           metering pass, so vuOut* goes live and render-silence is
           observable. Auto-snapshots when a freeze detector fires.

Detectors (getAudioStats/getLoopHist only — muteBlocks rides getAudioCapture,
which is bench-unreachable through the relay):
  vuOut collapse      render silent or muted (needs MIX page armed)
  i2sErrR/W growth    driver-timeout class (dead bus, loop continues)
  diagStallRd/Wr      blocking-call stalls
  loopHist pattern    anomalous periods (informational)
Snapshot = loopHist 64 + codec regs 5/6 (DAC mute) + 52/53 (out volume) +
getProxyState + the swept param, then a 1 s delta re-read.
"""
import argparse
import glob
import json
import os
import select
import signal
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screenshot import open_port, request, wait_ready  # noqa: E402


def detect_c3_port():
    """Port roles flip across sessions — the C3 is whichever ACM port
    answers a framed getUiState within ~0.3 s (2026-11-15: ttyACM0 = S3,
    whose USJ console never drains RX; a write into it can BLOCK past
    write_timeout once the kernel tty buffer fills — the SIGALRM cap is
    the only reliable guard). Probe the last-known-good port first."""
    import serial
    STX, ETX = 0x02, 0x03

    class _ProbeTimeout(Exception):
        pass

    def _alarm(signum, frame):
        raise _ProbeTimeout()

    known_good = "/dev/ttyACM1"
    paths = sorted(glob.glob("/dev/ttyACM*"))
    if known_good in paths:
        paths.remove(known_good)
        paths.insert(0, known_good)
    old = signal.signal(signal.SIGALRM, _alarm)
    try:
        for path in paths:
            print(f"  probing {path}...", flush=True)
            try:
                s = serial.Serial(port=path, baudrate=115200, timeout=0.1,
                                  write_timeout=0.5)
                s.dtr = True
                s.rts = True
            except Exception as e:
                print(f"  {path}: open failed ({e})")
                continue
            time.sleep(0.2)
            s.reset_input_buffer()
            payload = json.dumps({"cmd": "/debug/getUiState", "seq": 77},
                                 separators=(",", ":")).encode()
            ok = False
            buf = bytearray()
            in_frame = False
            signal.alarm(3)  # hard cap: a wedged port can block the write
            try:
                s.write(bytes([STX]) + payload + bytes([ETX]))
                deadline = time.time() + 0.6
                while time.time() < deadline:
                    for ch in s.read(256) or b"":
                        if ch == STX:
                            in_frame, buf = True, bytearray()
                        elif ch == ETX and in_frame:
                            ok = b"panelName" in buf or b"nav" in buf
                            break
                        elif in_frame:
                            buf.append(ch)
                    if ok:
                        break
            except (_ProbeTimeout, Exception) as e:
                print(f"  {path}: {'write blocked (alarm)' if isinstance(e, _ProbeTimeout) else e}")
            finally:
                signal.alarm(0)
                try:
                    s.close()
                except Exception:
                    pass
            print(f"  {path}: {'C3 panel' if ok else 'no C3 reply'}")
            if ok:
                return path
    finally:
        signal.signal(signal.SIGALRM, old)
    return None

STAT_KEYS = ["vuInL", "vuInR", "vuOutL", "vuOutR", "audioCycles",
             "cpuOverload", "diagStallRd", "diagStallWr", "diagLoopOver",
             "i2sErrR", "i2sErrW", "stackHighWater"]
CODEC_REGS = [5, 6, 52, 53]  # DAC mute L/R, output volume L/R (shadow)


def stats(s):
    r = request(s, {"cmd": "/debug/getAudioStats"}, timeout=5.0)
    return {k: r.get(k) for k in STAT_KEYS}


def loop_hist(s, n=16):
    r = request(s, {"cmd": "/debug/getLoopHist", "len": n}, timeout=5.0)
    return r.get("hist") or r.get("data") or []


def delta_print(tag, prev, cur):
    parts = []
    for k in STAT_KEYS:
        p, c = prev.get(k), cur.get(k)
        if p is None or c is None:
            continue
        if k.startswith("vu"):
            parts.append(f"{k}={c}")
        elif c != p:
            parts.append(f"{k}:{p}->{c}")
    print(f"[{tag}] {' '.join(parts) if parts else '(counters unchanged)'}")


def snapshot(s, param_id):
    print("  ==== FREEZE SNAPSHOT ====")
    h = loop_hist(s, 64)
    print(f"  loopHist64={h}")
    for reg in CODEC_REGS:
        r = request(s, {"cmd": "/debug/getCodecReg", "reg": reg}, timeout=5.0)
        print(f"  codec R{reg}={r.get('value')} (ok={r.get('ok')})")
    st = request(s, {"cmd": "/debug/getProxyState"}, timeout=6.0)
    print(f"  proxyState: active0={st.get('active0')} active1={st.get('active1')} "
          f"rate={st.get('rate')} sq={st.get('sq')}")
    try:
        cfg = request(s, {"cmd": "/debug/getConfig"}, timeout=6.0)
        raw = json.dumps(cfg)
        print(f"  config: {raw[:400]}")
    except Exception as e:
        print(f"  config read failed: {e}")
    if param_id:
        p = request(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": param_id},
                    timeout=5.0)
        print(f"  param ch0 {param_id}: {p.get('param', p)}")
    a2 = stats(s)
    print(f"  stats+1s: {a2}")
    print("  ==== verdict hints ====")
    print("  loopHist frozen/stale          -> audio task stuck (Read/Write/render)")
    print("  vuOut dancing while silent     -> render alive, TX/electrical path dead")
    print("  vuOut flat + loopHist alive    -> render produces silence (state poison)")
    print("  i2sErr growth in deltas above  -> driver timeout loop")
    print("  R5/R6 mute bit set or R52/53=0 -> codec register mute (shadow)")


def poll_loop(s, stop_check, tag="poll"):
    # Poll cadence is a C3-safety knob: getAudioStats replies are ~900 B and
    # sustained big-reply relay traffic crashes the C3 at its heap ceiling
    # (2026-11-15: one UI reboot during 3 Hz polling). ~1.5 Hz is the pace.
    prev = stats(s)
    print(f"[{tag}] baseline: {prev}")
    prev_hist = loop_hist(s)
    last_hist_t = time.time()
    armed = any(prev.get(k, 0) for k in ("vuInL", "vuInR"))
    print(f"[{tag}] metering {'ARMED' if armed else 'not armed'} "
          f"(park the panel on MIX page to arm vuOut)")
    while not stop_check():
        time.sleep(0.65)
        cur = stats(s)
        delta_print(tag, prev, cur)
        prev = cur
        now = time.time()
        if now - last_hist_t >= 2.0:
            h = loop_hist(s)
            if h != prev_hist:
                print(f"[{tag}] loopHist16={h}")
                prev_hist = h
            last_hist_t = now
        yield cur


class FreezeWatcher:
    """vuOut collapse while the metering pass is armed. NOTE: a natural
    decay to silence (excitation stopped) also collapses vuOut — verify
    every trigger by ear; a false snapshot is harmless."""

    def __init__(self):
        self.zero_run = 0
        self.armed = False

    def feed(self, a):
        out = (a.get("vuOutL") or 0) + (a.get("vuOutR") or 0)
        if out > 0:
            self.armed = True
            self.zero_run = 0
            return None
        if not self.armed:
            return None
        self.zero_run += 1
        if self.zero_run >= 5:  # ~2 s of dead output while armed
            self.zero_run = 0
            return "vuOut collapsed (render silent, muted, or excitation stopped)"
        return None


def read_param_range(s, param_id):
    st = request(s, {"cmd": "/debug/getProxyState"}, timeout=6.0)
    print(f"  active: ch0={st.get('active0')} ch1={st.get('active1')}")
    r = request(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": param_id},
                timeout=5.0)
    if r.get("error"):
        print(f"  FATAL: {param_id} not on ch0 ({r.get('error')}) — load the "
              f"right plugin (--load) or edit --param")
        return None
    p = r.get("param") or {}
    lo, hi = p.get("min"), p.get("max")
    if not isinstance(lo, int) or not isinstance(hi, int) or hi <= lo:
        print(f"  no usable min/max in reply ({p}) — assuming 0..4095")
        return 0, 4095
    print(f"  {param_id} range {lo}..{hi} current={p.get('current')}")
    return lo, hi


def sweep(s, args):
    param_id = args.param
    watcher = FreezeWatcher()
    if args.load:
        r = request(s, {"cmd": "/debug/setActivePlugin", "ch": 0, "id": args.load},
                    timeout=10.0)
        print(f"  load ch0 {args.load} -> {r}")
        time.sleep(2.0)
    rng_pair = read_param_range(s, param_id)
    if rng_pair is None:
        return False
    lo, hi = rng_pair
    print("  READY: excite the plugin (MIDI/input), park panel on MIX page.")
    print(f"  Sweeping {param_id} in {args.steps} steps x {args.rounds} rounds, "
          f"dwell {args.dwell}s. Ctrl-C to stop.")
    cur_val = None
    for rnd in range(args.rounds):
        rng = list(range(args.steps + 1))
        if rnd % 2:
            rng = list(reversed(rng))
        for step in rng:
            val = lo + (hi - lo) * step // args.steps
            if val == cur_val:
                continue
            cur_val = val
            request(s, {"cmd": "/debug/setPluginParam", "ch": 0,
                        "id": param_id, "value": val}, timeout=5.0)
            t_end = time.time() + args.dwell
            while time.time() < t_end:
                time.sleep(0.35)
                a = stats(s)
                verdict = watcher.feed(a)
                delta_print(f"r{rnd} {param_id}={val}", LAST[0], a)
                LAST[0] = a
                if verdict:
                    print(f"  FREEZE DETECTED: {verdict}")
                    snapshot(s, param_id)
                    return True
    return False


LAST = [{}]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None,
                    help="C3 console port (default: auto-detect)")
    ap.add_argument("--mode", choices=["poll", "sweep"], default="sweep")
    ap.add_argument("--param", default="structure")
    ap.add_argument("--load", default=None,
                    help="load this plugin on ch0 first (default: none)")
    ap.add_argument("--steps", type=int, default=16)
    ap.add_argument("--rounds", type=int, default=4)
    ap.add_argument("--dwell", type=float, default=0.8)
    args = ap.parse_args()

    port = args.port
    if port is None:
        print("detecting C3 console port...")
        port = detect_c3_port()
        if port is None:
            print("FATAL: no ACM port answered getUiState — is the panel powered?")
            return 1
        print(f"using {port}")

    with open_port(port) as s:
        # A wedged C3 stops draining RX mid-session; a blocked write must
        # surface as an exception, not hang the poll loop forever.
        s.write_timeout = 2.0
        print("waiting for DSP link...")
        if not wait_ready(s, 30):
            print("FATAL: DSP link did not come up")
            return 1
        print("link up.")
        if args.mode == "sweep":
            frozen = sweep(s, args)
            if not frozen:
                print("  no freeze detected — sweep finished. "
                      "Escalate to user-driven panel edits (mode poll).")
        else:
            print("POLL MODE: drive the panel; press Enter at the freeze "
                  "for the snapshot, Ctrl-C to stop.")
            watcher = FreezeWatcher()
            interactive = sys.stdin.isatty()
            if not interactive:
                print("(headless: Enter-snapshot disabled, auto-detect only)")
            try:
                for a in poll_loop(s, lambda: False):
                    verdict = watcher.feed(a)
                    if verdict:
                        print("  AUTO-SNAPSHOT: {v} (verify by ear!)".format(v=verdict))
                        snapshot(s, args.param)
                    if interactive and sys.stdin in \
                            select.select([sys.stdin], [], [], 0)[0]:
                        sys.stdin.readline()
                        snapshot(s, args.param)
            except KeyboardInterrupt:
                print("  stopped.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
