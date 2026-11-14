#!/usr/bin/env python3
"""Restore a preset backup produced by preset_backup.py.

For each channel: loads the plugin (setActivePlugin), waits for the params
push, then re-applies every saved patch — sets each param (current + cv/
trig mappings) via /debug/setPluginParam and stores it under its original
name/number via /debug/presetSave. Finally restores the originally-active
patch per channel.

Usage: preset_restore.py presets_full_backup.json
"""
import json
import sys
import time

sys.path.insert(0, "/home/ubuntu/ctag-tbd/uimcu/tools")
from screenshot import open_port, request, wait_ready  # noqa: E402


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "presets_full_backup.json"
    dump = json.load(open(src))

    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM1")
    args, _ = ap.parse_known_args()
    with open_port(args.port) as s:
        assert wait_ready(s, 25), "no DSP link"
        for ch_s, ch_data in dump.get("channels", {}).items():
            ch = int(ch_s)
            plugin = ch_data.get("plugin")
            if not plugin or plugin == "Void":
                continue
            r = request(s, {"cmd": "/debug/setActivePlugin", "ch": ch,
                            "id": plugin}, timeout=10.0)
            print(f"ch{ch}: load {plugin} -> {r.get('ok')}")
            time.sleep(1.5)  # params push lands

            for pat in ch_data.get("patches", []):
                num, name = pat["number"], pat["name"]
                # new format: params = flat {id: {current, cv, trig}};
                # old-format pluginSpec (nested, incomplete) is not restored.
                params = pat.get("params", {})
                applied = 0
                for pid, p in params.items():
                    r = request(s, {"cmd": "/debug/setPluginParam",
                                    "ch": ch, "id": pid,
                                    "key": "current",
                                    "value": p["current"]}, timeout=5.0)
                    if not r.get("ok"):
                        print(f"  WARN: {pid} current rejected")
                    cv = p.get("cv")
                    if cv is not None and cv != -1:
                        request(s, {"cmd": "/debug/setPluginParam",
                                    "ch": ch, "id": pid, "key": "cv",
                                    "value": cv}, timeout=5.0)
                    trig = p.get("trig")
                    if trig is not None and trig != -1:
                        request(s, {"cmd": "/debug/setPluginParam",
                                    "ch": ch, "id": pid, "key": "trig",
                                    "value": trig}, timeout=5.0)
                    applied += 1
                # settle before the store (the params flush runs at 5 s)
                time.sleep(0.2)
                r = request(s, {"cmd": "/debug/presetSave", "ch": ch,
                                "num": num, "name": name}, timeout=8.0)
                print(f"  #{num} {name}: {applied} params -> saved "
                      f"({r.get('ok')})")

            active = ch_data.get("activePresetNumber", 0)
            request(s, {"cmd": "/debug/presetLoad", "ch": ch, "num": active},
                    timeout=6.0)
            print(f"ch{ch}: restored active preset #{active}")


if __name__ == "__main__":
    main()
