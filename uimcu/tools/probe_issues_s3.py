#!/usr/bin/env python3
"""Probe the three 2026-11-18 S3 test issues in ONE console session.

1. Elements "plays SEQ with no modulation" — read the DEVICE mp-Elements.jsn
   cv/trig members + the live model state (factory Default carries a demo
   mapping frequency<-cv104 / play<-trig45 that rode in with 236f850b).
2. PolyPad silent when patched to MIDI/SEQ — read device mp-PolyPad.jsn
   (stale pre-fix file?), then drive enableEG's trig to MB.T1 (always
   gating) and ARP1 gate; audioCycles/VU + seqstate tell whether the
   trigger path fires at all on the BBA2.
3. Warps "menu obsolete" — A/B audioCycles (and VU if metering armed) with
   the warps stage bypassed vs engaged (algorithm 3, mix full) to prove
   whether it renders on the BBA2 at all.

Read-only EXCEPT: clears the Elements demo mapping (the issue-2 fix, left
to the auto-flush), the warps A/B (restored), and the PolyPad trig probes
(restored). Ends by reloading Elements (the persisted plugin).
"""
import json
import sys
import time

sys.path.insert(0, "/home/schrab/Projects/ctag-tbd/uimcu/tools")
from screenshot import open_port, wait_ready, request  # noqa: E402
import spiffs_push_file as spf  # noqa: E402


def cmd(s, c, timeout=8.0):
    return spf.robust_request(s, c, timeout=timeout)


def stats_cycles(s):
    r = cmd(s, {"cmd": "/debug/getAudioStats"}, timeout=6.0)
    return (r.get("audioCycles"), r.get("vuOutL"), r.get("vuOutR"),
            r.get("muteBlocks"), r.get("cpuOverload"))


def sample_cycles(s, label, n=3, gap=0.8):
    for i in range(n):
        cy, vl, vr, mb, ov = stats_cycles(s)
        print(f"    {label} #{i}: cycles={cy} vuOut=({vl},{vr}) "
              f"muteBlocks={mb} cpuOv={ov}", flush=True)
        time.sleep(gap)


def dump_mappings(s, dev_path):
    data, err = spf.read_device_file(s, dev_path)
    if err is not None:
        print(f"  READ FAILED {dev_path}: {err}", flush=True)
        return None
    print(f"  {dev_path}: {len(data)} B", flush=True)
    try:
        d = json.loads(data)
    except Exception as e:
        print(f"  PARSE FAILED: {e}", flush=True)
        return None
    for p in d.get("patches", []):
        mapped = [(q["id"], q.get("cv", -1), q.get("trig", -1))
                  for q in p.get("params", [])
                  if q.get("cv", -1) != -1 or q.get("trig", -1) != -1]
        print(f"  patch '{p.get('name', '?')}': {len(p.get('params', []))} params"
              + (f" MAPPED: {mapped}" if mapped else " (no cv/trig)"),
              flush=True)
    return d


def param_probe(s, pid):
    r = cmd(s, {"cmd": "/debug/getPluginParams", "ch": 0, "id": pid})
    print(f"  {pid}: {json.dumps({k: r.get(k) for k in
          ('id', 'current', 'cv', 'trig', 'min', 'max') if k in r})}",
          flush=True)
    return r


def main():
    with open_port("/dev/ttyACM0") as s:
        assert wait_ready(s, 25), "no DSP link"
        st = cmd(s, {"cmd": "/debug/getProxyState"}, timeout=8.0)
        print(f"active0={st.get('active0')} active1={st.get('active1')} "
              f"params0={st.get('params0')}", flush=True)
        print(f"seqstate: {json.dumps(st.get('seqstate', {}))[:300]}", flush=True)

        print("== ARP activity (seqstate x3 @1.2 s) ==", flush=True)
        prev = None
        for i in range(3):
            st = cmd(s, {"cmd": "/debug/getProxyState"}, timeout=6.0)
            sq = json.dumps(st.get("seqstate", {}))
            print(f"  sq#{i}: {sq[:260]}", flush=True)
            if prev is not None and sq != prev:
                print("  -> seqstate CHANGES (ARP advancing)", flush=True)
            prev = sq
            time.sleep(1.2)

        print("== [1] DEVICE mp-Elements.jsn mappings ==", flush=True)
        dump_mappings(s, "/spiffs/data/sp/mp-Elements.jsn")

        print("== [3] DEVICE mp-PolyPad.jsn mappings ==", flush=True)
        dump_mappings(s, "/spiffs/data/sp/mp-PolyPad.jsn")

        if st.get("active0") != "Elements":
            print("loading Elements...", flush=True)
            if not spf.load_and_confirm(s, "Elements"):
                print("ELEMENTS LOAD UNCONFIRMED — aborting", flush=True)
                return 1

        print("== [1] Elements live model state (frequency / play) ==",
              flush=True)
        param_probe(s, "frequency")
        param_probe(s, "play")

        print("== [1] FIX: clear demo mapping (cv=-1 / trig=-1) ==", flush=True)
        cmd(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "frequency",
                "key": "cv", "value": -1})
        cmd(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "play",
                "key": "trig", "value": -1})
        param_probe(s, "frequency")
        param_probe(s, "play")
        print("  waiting 7 s for model auto-flush to persist...", flush=True)
        time.sleep(7)

        print("== [W] Warps render A/B on BBA2 ==", flush=True)
        param_probe(s, "warps_algorithm")
        param_probe(s, "warps_mix")
        sample_cycles(s, "warps bypass")
        cmd(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "warps_algorithm",
                "value": 2458})  # normalized 0..1 -> digital/xor zone
        cmd(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "warps_mix",
                "value": 4095})
        sample_cycles(s, "warps ENGAGED")
        cmd(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "warps_algorithm",
                "value": 0})
        cmd(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "warps_mix",
                "value": 0})
        print("  warps restored to bypass", flush=True)

        print("== [2] PolyPad trigger path ==", flush=True)
        if not spf.load_and_confirm(s, "PolyPad"):
            print("POLYPAD LOAD UNCONFIRMED", flush=True)
            return 1
        time.sleep(2)
        param_probe(s, "enableEG")
        sample_cycles(s, "poly self-sustaining (trig=-1)")
        cmd(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "enableEG",
                "key": "trig", "value": 40})  # MB.T1 random gates, always on
        print("  enableEG -> trig 40 (MB.T1)", flush=True)
        sample_cycles(s, "poly T1-gated", n=4, gap=1.5)
        cmd(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "enableEG",
                "key": "trig", "value": 45})  # ARP1 gate
        print("  enableEG -> trig 45 (ARP1 gate)", flush=True)
        sample_cycles(s, "poly ARP-gated", n=4, gap=1.5)
        st = cmd(s, {"cmd": "/debug/getProxyState"}, timeout=6.0)
        print(f"  seqstate during ARP-gate probe: "
              f"{json.dumps(st.get('seqstate', {}))[:260]}", flush=True)
        cmd(s, {"cmd": "/debug/setPluginParam", "ch": 0, "id": "enableEG",
                "key": "trig", "value": -1})
        print("  enableEG restored to trig=-1", flush=True)
        print("  waiting 7 s for PolyPad auto-flush...", flush=True)
        time.sleep(7)

        print("== restoring Elements ==", flush=True)
        if spf.load_and_confirm(s, "Elements"):
            print("Elements back active", flush=True)
        param_probe(s, "frequency")
    return 0


if __name__ == "__main__":
    sys.exit(main())
