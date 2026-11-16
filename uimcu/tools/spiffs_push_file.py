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
        if fsize is None:
            return None, f"reply missing size: {r}"
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
        ok = False
        for attempt in range(3):
            # Writes are idempotent (fixed offset) — a lost or cross-matched
            # reply just means re-send the same chunk.
            r = robust_request(s, {"cmd": "/debug/writeFileChunk", "path": path,
                                   "offset": off, "hex": chunk.hex()})
            if r.get("ok") and r.get("written") == len(chunk):
                ok = True
                break
            print(f"    write @{off} attempt {attempt + 1}/3: {r.get('error', r)}",
                  flush=True)
            time.sleep(1.0)
        if not ok:
            raise RuntimeError(f"write mismatch @{off}: {r}")
        off += len(chunk)
        time.sleep(0.15)
    return len(data)


def load_and_confirm(s, plugin, tries=60):
    """Load a plugin and VERIFY it actually landed — the load burst wedges
    the bench relay for ~1-3 min and the request/reply can be lost outright;
    a lost Void load leaves the previous plugin (and its auto-flush) running
    over the pushed files."""
    robust_request(s, {"cmd": "/debug/setActivePlugin", "ch": 0,
                       "id": plugin}, timeout=15.0)
    for i in range(tries):
        try:
            st = request(s, {"cmd": "/debug/getProxyState"}, timeout=1.5,
                         retries=0)
            if st.get("active0") == plugin:
                return True
        except Exception:
            pass
        time.sleep(3.0)
        if i % 8 == 7:
            # re-send the load in case the first one never dispatched
            try:
                request(s, {"cmd": "/debug/setActivePlugin", "ch": 0,
                            "id": plugin}, timeout=15.0, retries=0)
            except Exception:
                pass
    return False


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
                # mp preset files carry user patches on-device; a repo
                # (factory) copy has fewer. Guard: this bit the bench TWICE
                # on 2026-11-17 — each push of the repo mp clobbered the 4
                # user Elements presets (recovered from the backup both
                # times). Merge instead of overwrite.
                try:
                    if old and len(json.loads(old).get("patches", [])) > \
                            len(json.loads(open(local, "rb").read()).get("patches", [])):
                        print(f"  WARNING: device {dev} holds MORE patches "
                              f"than {local} — merge user patches into the "
                              f"pushed file or they will be lost",
                              flush=True)
                except Exception:
                    pass
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
            if not load_and_confirm(s, "Void"):
                print("VOID LOAD NEVER CONFIRMED — aborting (auto-flush "
                      "would eat the push)", flush=True)
                return 1
            print("ch0 = Void confirmed", flush=True)

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
        confirmed = load_and_confirm(s, plugin)
        print("active0 =", plugin if confirmed else "LOAD UNCONFIRMED",
              flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
