#!/usr/bin/env python3
"""Full preset backup over the bench console.

For each channel: reads the stored patch list, loads each patch
(/debug/presetLoad), dumps every parameter (current + cv/trig mappings)
via the reliable /debug/getPluginParams per-param path, and saves a
portable JSON. The originally-active patch is restored at the end.

Companion restore tool: preset_restore.py (re-applies the JSON after a
storage reflash).
"""
import json
import sys
import time

sys.path.insert(0, "/home/ubuntu/ctag-tbd/uimcu/tools")
from screenshot import open_port, request, wait_ready  # noqa: E402


def flatten_params(items, out):
    """Collect leaf params recursively — grouped params (e.g. TBDings' 'eg'
    and 'mod' groups) nest inside group objects and a flat scan misses 9 of
    TBDings' 21 params."""
    for it in items:
        if not isinstance(it, dict):
            continue
        if "current" in it:
            out.append(it)
        for k in ("params", "children", "items"):
            if k in it:
                flatten_params(it[k], out)


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "presets_full_backup.json"
    dump = {"channels": {}}

    with open_port("/dev/ttyACM1") as s:
        assert wait_ready(s, 25), "no DSP link"
        st = request(s, {"cmd": "/debug/getProxyState"}, timeout=8.0)
        for ch in (0, 1):
            plugin = st.get(f"active{ch}")
            nparams = st.get(f"params{ch}", 0)
            if not plugin or plugin == "Void" or not nparams:
                print(f"ch{ch}: {plugin} — nothing to back up")
                continue
            lst = request(s, {"cmd": "/debug/getPresetsCached", "ch": ch},
                          timeout=6.0).get("presets", {})
            patches = lst.get("presets", []) if isinstance(lst, dict) else []
            active = lst.get("activePresetNumber", 0)
            print(f"ch{ch}: {plugin}, {len(patches)} patches "
                  f"(active #{active})")

            ch_data = {"plugin": plugin, "activePresetNumber": active,
                       "patches": []}
            for pat in patches:
                num, name = pat["number"], pat["name"]
                request(s, {"cmd": "/debug/presetLoad", "ch": ch,
                            "num": num}, timeout=6.0)
                time.sleep(0.35)  # SPIFFS storeJSON + params settle
                # ONE call returns the full param spec with LIVE current/cv
                # values (verified: a set round-trips into it). Flatten the
                # group nesting — grouped params carry live values too.
                r = request(s, {"cmd": "/debug/getPluginParams", "ch": ch},
                            timeout=8.0)
                spec = r.get("params", {})
                leaves = []
                if isinstance(spec, dict):
                    flatten_params(spec.get("params", []), leaves)
                entry = {"name": name, "number": num,
                         "plugin": spec.get("id") if isinstance(spec, dict) else None,
                         "params": {p["id"]: {"current": p["current"],
                                              "cv": p.get("cv"),
                                              "trig": p.get("trig")}
                                    for p in leaves}}
                ch_data["patches"].append(entry)
                print(f"  #{num} {name}: {len(entry['params'])} params")
            dump["channels"][ch] = ch_data
            # restore the patch that was active before the dump
            request(s, {"cmd": "/debug/presetLoad", "ch": ch,
                        "num": active}, timeout=6.0)
            time.sleep(0.3)

    with open(out_path, "w") as f:
        json.dump(dump, f, indent=1)
    total = sum(len(c["patches"]) for c in dump["channels"].values())
    print(f"saved {out_path}: {total} patches")


if __name__ == "__main__":
    main()
