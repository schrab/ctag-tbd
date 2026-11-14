#!/usr/bin/env python3
"""Dump ALL stored preset files (mp-*.jsn) from the DSP's SPIFFS,
byte-exact, WITHOUT loading any plugin. Uses /debug/listFiles +
/debug/readFileChunk (hex chunks, relay-safe).

Output: a directory of raw .jsn files + an index JSON.
Restore companion: spiffs_preset_restore.py (writes the files back, then
reloads the affected plugins so their models re-read the restored state).
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


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    index = {}
    with open_port("/dev/ttyACM1") as s:
        assert wait_ready(s, 25), "no DSP link"
        r = request(s, {"cmd": "/debug/listFiles",
                        "dir": DATA_DIR}, timeout=8.0)
        files = r.get("files", [])
        print(f"{len(files)} files in {DATA_DIR}")
        for ent in files:
            name, size = ent["name"], ent["size"]
            if not name.startswith("mp-"):
                continue  # the preset files
            path = f"{DATA_DIR}/{name}"
            data = bytearray()
            off = 0
            while off < size:
                r = request(s, {"cmd": "/debug/readFileChunk", "path": path,
                                "offset": off, "len": CHUNK}, timeout=8.0)
                hx = r.get("hex", "")
                data += bytes.fromhex(hx)
                off += len(hx) // 2
            with open(os.path.join(OUT_DIR, name), "wb") as f:
                f.write(data)
            index[name] = {"size": size, "patches": None}
            # count patches from the JSON (best effort)
            try:
                j = json.loads(data.decode(errors="replace"))
                index[name]["patches"] = len(j.get("patches", []))
            except Exception:
                pass
            print(f"  {name}: {size} B, {index[name]['patches']} patches")
    with open(os.path.join(OUT_DIR, "index.json"), "w") as f:
        json.dump(index, f, indent=1)
    print(f"saved {len(index)} preset files -> {OUT_DIR}/")


if __name__ == "__main__":
    main()
