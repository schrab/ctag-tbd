#!/usr/bin/env python3
"""Read the BBA2's mp-Elements/mp-PolyPad/mp-TBDings, strip the SEQ demo
mappings that rode in via bench auto-flushes, and save corrected local
artifacts for spiffs_push_file.py (no writes to the device here).

Elements: frequency cv=104 -> -1, play/gate trig=45 -> -1 (the VARG bench
demo mapping baked into every patch — made Elements play the ARP with no
user modulation). PolyPad: enableEG trig=0 -> -1 (stale pre-01a2583e
mapping; dead MIDI slot makes every load silent). Everything else —
currents, user patches, the user's own strength cv=102 mapping — untouched.
"""
import json
import sys

sys.path.insert(0, "/home/schrab/Projects/ctag-tbd/uimcu/tools")
from screenshot import open_port, wait_ready  # noqa: E402
import spiffs_push_file as spf  # noqa: E402

OUT = "/home/schrab/Projects/ctag-tbd/spiffs_image/data/sp/"


def strip(patch):
    changed = []
    for q in patch.get("params", []):
        pid = q.get("id")
        if pid == "frequency" and q.get("cv") == 104:
            q["cv"] = -1
            changed.append("frequency.cv")
        if pid in ("play", "gate") and q.get("trig") == 45:
            q["trig"] = -1
            changed.append(pid + ".trig")
        if pid == "enableEG" and q.get("trig") == 0:
            q["trig"] = -1
            changed.append("enableEG.trig")
    return changed


def fix(s, dev_path, out_name):
    data, err = spf.read_device_file(s, dev_path)
    assert err is None, f"{dev_path}: {err}"
    d = json.loads(data)
    print(f"{dev_path}: {len(data)} B, patches "
          f"{[p['name'] for p in d['patches']]}", flush=True)
    total = []
    for p in d["patches"]:
        ch = strip(p)
        if ch:
            print(f"  {p['name']}: stripped {ch}", flush=True)
        total += ch
    out = OUT + out_name
    with open(out, "w") as f:
        json.dump(d, f, separators=(",", ":"))
    # verify the artifact parses and is clean
    d2 = json.load(open(out))
    for p in d2["patches"]:
        m = [(q["id"], q.get("cv"), q.get("trig")) for q in p["params"]
             if q.get("cv", -1) != -1 or q.get("trig", -1) != -1]
        flag = f" STILL MAPPED: {m}" if m else ""
        print(f"  artifact {p['name']}: "
              f"{len(p['params'])} params{flag}", flush=True)
    print(f"-> wrote {out} (stripped: {total or 'nothing needed'})", flush=True)
    return total


def main():
    with open_port("/dev/ttyACM0") as s:
        assert wait_ready(s, 25), "no DSP link"
        fix(s, "/spiffs/data/sp/mp-PolyPad.jsn", "mp-PolyPad.bba2-fixed.jsn")
        fix(s, "/spiffs/data/sp/mp-Elements.jsn", "mp-Elements.bba2-fixed.jsn")
        # report-only: does device mp-TBDings carry the demo pattern too?
        data, err = spf.read_device_file(s, "/spiffs/data/sp/mp-TBDings.jsn")
        if err is None:
            d = json.loads(data)
            for p in d["patches"]:
                m = [(q["id"], q.get("cv"), q.get("trig")) for q in p["params"]
                     if q.get("cv", -1) not in (-1, None)
                     or q.get("trig", -1) not in (-1, None)]
                if m:
                    print(f"mp-TBDings {p['name']}: {m}", flush=True)
        else:
            print(f"mp-TBDings read failed: {err}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
