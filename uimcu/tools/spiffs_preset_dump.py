#!/usr/bin/env python3
"""Dump ALL stored preset files (mp-*.jsn) from the DSP's SPIFFS,
byte-exact, WITHOUT loading any plugin. Uses /debug/listFiles +
/debug/readFileChunk (hex chunks, relay-safe).

Output: a directory of raw .jsn files + an index JSON.
Restore companion: spiffs_preset_restore.py (writes the files back, then
reloads the affected plugins so their models re-read the restored state).

SPIFFS directory/chunk ops can stall the DSP link task past the C3's 1 s
offline watchdog — the C3 answers 'timeout'/'dsp offline' and re-attaches
seconds later. robust_request() waits through those blips instead of
failing, and pacing lets the link breathe between SPIFFS ops.
"""
import json
import os
import sys
import time

sys.path.insert(0, "/home/ubuntu/ctag-tbd/uimcu/tools")
from screenshot import open_port, request, wait_ready  # noqa: E402

OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else "spiffs_preset_backup"
CHUNK = 512
DATA_DIR = "/spiffs/data/sp"


def robust_request(s, cmd, timeout=8.0, tries=6):
    for attempt in range(tries):
        try:
            r = request(s, cmd, timeout=timeout, retries=0)
            if "error" in r and r["error"] in ("timeout", "dsp offline"):
                print(f"    link blip ({r['error']}), waiting...", flush=True)
                time.sleep(3.0)
                continue
            return r
        except Exception as e:
            print(f"    no reply, waiting... ({e})", flush=True)
            time.sleep(3.0)
    raise RuntimeError(f"DSP link never recovered for {cmd}")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    index = {}
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM1")
    args, _ = ap.parse_known_args()
    with open_port(args.port) as s:
        assert wait_ready(s, 25), "no DSP link"
        r = robust_request(s, {"cmd": "/debug/listFiles", "dir": DATA_DIR})
        names = [n for n in r.get("files", []) if n.startswith("mp-")]
        print(f"{len(names)} preset files", flush=True)
        for name in names:
            path = f"{DATA_DIR}/{name}"
            data = bytearray()
            off = 0
            size = None
            while True:
                r = robust_request(s, {"cmd": "/debug/readFileChunk",
                                       "path": path, "offset": off,
                                       "len": CHUNK})
                size = r.get("size", size)
                hx = r.get("hex", "")
                data += bytes.fromhex(hx)
                off += len(hx) // 2
                time.sleep(0.15)  # pace between SPIFFS reads
                if size is not None and off >= size:
                    break
                if not hx:
                    break
            with open(os.path.join(OUT_DIR, name), "wb") as f:
                f.write(data)
            patches = "?"
            try:
                j = json.loads(data.decode(errors="replace"))
                patches = len(j.get("patches", []))
            except Exception:
                pass
            index[name] = {"size": size, "patches": patches}
            print(f"  {name}: {size} B, {patches} patches", flush=True)
            time.sleep(0.5)
    with open(os.path.join(OUT_DIR, "index.json"), "w") as f:
        json.dump(index, f, indent=1)
    print(f"saved {len(index)} preset files -> {OUT_DIR}/")


if __name__ == "__main__":
    main()
