#!/usr/bin/env python3
"""TBDings silence forensics: timeline the live SEQ word (gate level, step,
note) against output peaks. 30 s at ~10 Hz. Bit map (SeqEngine.cpp):
bit0/1 g1/g2, bit2 a1 level, bit4 a1 acc, bit6 a1 slide,
bits 8-11 a1 step, bits 16-23 a1 note."""
import sys
import time

sys.path.insert(0, "/home/ubuntu/ctag-tbd/uimcu/tools")
from screenshot import open_port, request  # noqa: E402


def main():
    with open_port("/dev/ttyACM0") as s:
        print("playing... timeline (t | a1lvl acc step note | vuOutL)")
        prev = None
        rows = []
        t0 = time.time()
        while time.time() - t0 < 30:
            st = request(s, {"cmd": "/debug/getProxyState"}, timeout=4.0)
            sq = int(st.get("sq", 0))
            lvl = (sq >> 2) & 1
            acc = (sq >> 4) & 1
            step = (sq >> 8) & 0xF
            note = (sq >> 16) & 0xFF
            a = request(s, {"cmd": "/debug/getAudioStats"}, timeout=4.0)
            ol = a.get("vuOutL", -1)
            t = time.time() - t0
            rows.append((t, lvl, acc, step, note, ol))
            # compact print on transitions + every 10th row
            changed = prev is None or (lvl, step, note) != prev
            if changed or int(t * 10) % 10 == 0:
                mark = ""
                if prev is not None and note != prev[2]:
                    mark = "  <- NOTE CHANGE"
                if prev is not None and step != prev[1]:
                    mark = "  <- STEP"
                print(f"  {t:5.1f}s lvl={lvl} acc={acc} step={step:2d} "
                      f"note={note:3d} outL={ol:4d}{mark}")
            prev = (lvl, step, note)
        # summary: how long silent (outL < 10) and what the note did
        silent = [r for r in rows if r[5] < 10]
        print(f"== rows={len(rows)} silent_rows={len(silent)}")
        if silent:
            print(f"== silence spans {silent[0][0]:.1f}s .. {silent[-1][0]:.1f}s")
            notes = sorted(set(r[4] for r in silent))
            print(f"== notes seen during silence: {notes}")


if __name__ == "__main__":
    main()
