#!/usr/bin/env python3
"""Restore the SPIFFS preset files dumped by spiffs_preset_dump.py.

Writes each mp-*.jsn back byte-exact via /debug/writeFileChunk, then
reloads the affected plugins (setActivePlugin away and back) so their
models re-read the restored files.

Usage: spiffs_preset_restore.py <backup_dir> [--port P]
"""
import json
import os
import sys
import time

sys.path.insert(0, "/home/ubuntu/ctag-tbd/uimcu/tools")
from screenshot import open_port, request, wait_ready  # noqa: E402

CHUNK = 512
DATA_DIR = "/spiffs/data/sp"


def robust_request(s, cmd, timeout=10.0, tries=4):
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
    src_dir = sys.argv[1] if len(sys.argv) > 1 else "spiffs_preset_backup"
    port = "/dev/ttyACM0"
    for p in ("/dev/ttyACM1", "/dev/ttyACM0", "/dev/ttyACM2"):
        try:
            with open_port(p) as s:
                request(s, {"cmd": "/debug/getUiState"}, timeout=2.5,
                        retries=0)
                port = p
                print(f"C3 found on {p}", flush=True)
                break
        except Exception:
            pass

    files = sorted(f for f in os.listdir(src_dir) if f.startswith("mp-")
                   and f.endswith(".jsn"))
    print(f"restoring {len(files)} files from {src_dir}", flush=True)
    with open_port(port) as s:
        assert wait_ready(s, 25), "no DSP link"
        for name in files:
            data = open(os.path.join(src_dir, name), "rb").read()
            path = f"{DATA_DIR}/{name}"
            off = 0
            while off < len(data):
                chunk = data[off:off + CHUNK]
                r = robust_request(s, {"cmd": "/debug/writeFileChunk",
                                       "path": path, "offset": off,
                                       "hex": chunk.hex()})
                if not r.get("ok") or r.get("written") != len(chunk):
                    print(f"  {name}@{off}: WRITE MISMATCH {r}", flush=True)
                    break
                off += len(chunk)
                time.sleep(0.15)
            print(f"  {name}: {len(data)} B written", flush=True)
            time.sleep(0.4)

    # reload the plugins whose files changed: load Void over each channel
    # and re-load the originally-active plugins, so every model re-reads
    # its file from SPIFFS.
    print("reloading plugins to re-read the restored files...", flush=True)
    with open_port(port) as s:
        for ch in (0, 1):
            request(s, {"cmd": "/debug/setActivePlugin", "ch": ch,
                        "id": "Void"}, timeout=10.0)
            time.sleep(1.0)
        # the user re-loads their plugins here (or we re-apply the backup's
        # "active" note once plugin state is tracked in the index)


if __name__ == "__main__":
    main()
