#!/usr/bin/env python3
"""Push repo files into device SPIFFS via /debug/writeFileChunk (no reflash).

Backs up each current on-device file first (readFileChunk, 512 B hex slices),
loads Void on ch0 so the model auto-flush cannot overwrite pushed files with
stale in-RAM state, writes the new content, verifies byte-exact read-back,
then reloads the plugin. Plugin-load bursts temporarily saturate the bench
relay (the 2026-11-14 wedge class) — retries are generous and tolerate the
burst settling on its own.

Usage:
  spiffs_push_file.py <plugin_id> <local1> <dev1> [local2 dev2 ...]
Backup lands next to each local file as <name>.device-backup.jsn.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screenshot import open_port, request, wait_ready  # noqa: E402

# 256 B, not 512: the C3's LinkClient sendFrame has a 768 B frame buffer and
# SILENTLY DROPS bigger requests (uimcu/main/LinkClient.cpp) — a 512 B chunk
# never dispatches and the write times out with no trace. 256 B chunks
# (610 B frames) round-trip.
CHUNK = 256


def robust_request(s, cmd, timeout=12.0, tries=10):
    last = None
    for attempt in range(tries):
        try:
            r = request(s, cmd, timeout=timeout, retries=0)
            if "error" in r and r["error"] in ("timeout", "dsp offline"):
                print(f"    link blip ({r['error']}), attempt {attempt + 1}/{tries}",
                      flush=True)
                time.sleep(2.5)
                last = r["error"]
                continue
            return r
        except Exception as e:
            print(f"    no reply, attempt {attempt + 1}/{tries} ({e})", flush=True)
            time.sleep(2.5)
            last = str(e)
    raise RuntimeError(f"DSP link never recovered ({last}) for {cmd.get('cmd')}")


def read_device_file(s, path):
    data = b""
    off = 0
    while True:
        r = robust_request(s, {"cmd": "/debug/readFileChunk", "path": path,
                               "offset": off, "len": CHUNK})
        if "error" in r:
            return None, r["error"]
        fsize = r.get("size")
        data += bytes.fromhex(r.get("hex", ""))
        got = r.get("got", 0)
        off += got
        if off >= fsize or got == 0:
            break
        time.sleep(0.05)
    return data, None


def write_device_file(s, path, data):
    off = 0
    while off < len(data):
        chunk = data[off:off + CHUNK]
        r = robust_request(s, {"cmd": "/debug/writeFileChunk", "path": path,
                               "offset": off, "hex": chunk.hex()})
        if not r.get("ok") or r.get("written") != len(chunk):
            raise RuntimeError(f"write mismatch @{off}: {r}")
        off += len(chunk)
        time.sleep(0.15)
    return len(data)


def main():
    args = sys.argv[1:]
    if len(args) < 3 or len(args) % 2 == 0:
        print(__doc__)
        return 2
    plugin = args[0]
    pairs = [(args[i], args[i + 1]) for i in range(1, len(args), 2)]

    with open_port("/dev/ttyACM0") as s:
        assert wait_ready(s, 25), "no DSP link"

        st = robust_request(s, {"cmd": "/debug/getProxyState"}, timeout=8.0)
        already_void = st.get("active0") == "Void"

        backups = {}
        for local, dev in pairs:
            old, err = read_device_file(s, dev)
            if err is None:
                bpath = local + ".device-backup.jsn"
                open(bpath, "wb").write(old)
                backups[dev] = (bpath, old)
                print(f"backup: {dev} -> {bpath} ({len(old)} B)", flush=True)
            else:
                print(f"backup FAILED for {dev} ({err}) — aborting, nothing written",
                      flush=True)
                return 1

        if already_void:
            print("ch0 already Void — auto-flush parked, pushing directly",
                  flush=True)
        else:
            print("loading Void on ch0 (park the model auto-flush)...",
                  flush=True)
            robust_request(s, {"cmd": "/debug/setActivePlugin", "ch": 0,
                               "id": "Void"}, timeout=15.0)
            # The load burst wedges the bench relay for ~1-3 min; poll until
            # it recovers instead of burning the retry budget.
            for i in range(60):
                try:
                    request(s, {"cmd": "/debug/getProxyState"}, timeout=1.0,
                            retries=0)
                    break
                except Exception:
                    time.sleep(3.0)
            time.sleep(1.0)

        for local, dev in pairs:
            data = open(local, "rb").read()
            json.loads(data)  # refuse to push non-JSON
            if dev in backups and backups[dev][1] == data:
                print(f"{dev}: device already matches, skipping write",
                      flush=True)
                continue
            n = write_device_file(s, dev, data)
            print(f"pushed: {dev} ({n} B)", flush=True)
            back, err = read_device_file(s, dev)
            if err is not None or back != data:
                print(f"{dev}: READ-BACK MISMATCH ({err})", flush=True)
                return 1
            print(f"{dev}: read-back byte-exact", flush=True)
            time.sleep(0.3)

        print(f"loading {plugin} on ch0...", flush=True)
        robust_request(s, {"cmd": "/debug/setActivePlugin", "ch": 0,
                           "id": plugin}, timeout=15.0)
        # The load burst wedges the bench relay for ~1-3 min (the reply is
        # lost but the load completes DSP-side) — wait it out and confirm.
        active = None
        for i in range(60):
            try:
                st = request(s, {"cmd": "/debug/getProxyState"}, timeout=1.0,
                             retries=0)
                active = st.get("active0")
                if active == plugin:
                    break
            except Exception:
                pass
            time.sleep(3.0)
        print("active0 =", active, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
