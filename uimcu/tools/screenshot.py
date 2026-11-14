#!/usr/bin/env python3
"""C3 panel bench console client: /debug commands + framebuffer screenshots.

The C3's USB-Serial-JTAG port is the bench channel since the P6 cutover
(A1S UART0 bench task removed). Framing is STX/ETX JSON, same as the link.

Usage:
  screenshot.py --out shot.png                 # grab the panel framebuffer
  screenshot.py --cmd '{"cmd":"/debug/getAudioStats"}'
  screenshot.py --port /dev/ttyACM0 --out shot.png

DSP-side commands (/debug/getAudioStats, /debug/setElementsMode, ...) are
forwarded by the C3's DebugLink over the panel link; replies carry a
"cmd" echo. Errors come back as {"cmd":...,"error":"dsp offline"|"timeout"}.
"""
import argparse
import json
import sys
import time

import serial

STX, ETX = 0x02, 0x03


def open_port(path: str):
    # Bench rule (2026-11-14, measured, bench_openreset_matrix.py): HOLD
    # DTR/RTS asserted from before open through the session. pyserial's
    # open() applies the stored dtr/rts states explicitly (TIOCMBIS/BIC),
    # and deasserting creates the DTR falling edge the C3's USB-Serial-JTAG
    # decodes as USB_UART_CHIP_RESET (rst:0x15) — the old pre-set-False open
    # reset the C3 on EVERY open and lost the live UI state. Held high there
    # are zero transitions: opens and closes are state-preserving (verified
    # via getUiState across close/reopen cycles). dsrdtr/rtscts stay False
    # so no tty flow-control wiring engages.
    s = serial.Serial()
    s.port = path
    s.baudrate = 115200
    s.dsrdtr = False
    s.rtscts = False
    s.dtr = True
    s.rts = True
    s.timeout = 0.1
    s.open()
    s.dtr = True
    s.rts = True
    time.sleep(0.3)
    s.reset_input_buffer()
    return s


def request(s, cmd_obj: dict, timeout: float = 3.0, retries: int = 2) -> dict:
    """One bench request; on a no-reply timeout, retry the SAME command a
    couple of times with fresh sequence numbers (the C3 DebugLink shows a
    transient reply stall under sustained traffic — a retry recovers it;
    a wedged task never answers and the retries exhaust)."""
    last_err = None
    for attempt in range(retries + 1):
        seq = int(time.time() * 1000) & 0x7FFFFFFF
        cmd_obj = dict(cmd_obj, seq=seq)
        payload = json.dumps(cmd_obj, separators=(",", ":")).encode()
        try:
            s.write(bytes([STX]) + payload + bytes([ETX]))
        except Exception as e:  # port died (device re-enumerated)
            raise TimeoutError(f"port write failed: {e}") from e
        end = time.time() + timeout
        buf = bytearray()
        in_frame = False
        while time.time() < end:
            chunk = s.read(256)
            for ch in chunk or b"":
                if ch == STX:
                    in_frame = True
                    buf.clear()
                elif ch == ETX:
                    if not in_frame:
                        continue
                    reply = json.loads(bytes(buf).decode(errors="replace"))
                    if reply.get("seq") == seq:
                        return reply
                    in_frame = False
                elif in_frame:
                    buf.append(ch)
        last_err = TimeoutError(f"no reply to {cmd_obj['cmd']}")
        time.sleep(0.3)
    raise last_err


def fb_to_png(hexdata: str, out_path: str):
    from PIL import Image

    raw = bytes.fromhex(hexdata)
    if len(raw) != 1024:
        raise ValueError(f"framebuffer is {len(raw)} B, expected 1024")
    img = Image.new("1", (128, 64), 0)
    px = img.load()
    for page in range(8):
        for x in range(128):
            bits = raw[page * 128 + x]
            for bit in range(8):
                if (bits >> bit) & 1:
                    px[x, page * 8 + bit] = 1
    img.save(out_path)


def wait_ready(s, timeout: float = 15.0) -> bool:
    """Wait for the DSP link, returning as soon as it is live.

    With the current open_port (dtr/rts held high) opens are
    state-preserving and this returns essentially instantly when the
    device is already running. The wait only matters after a real reboot
    (esptool flash, power cycle): getProxyState's "active0" only becomes
    non-empty once the DSP's boot metadata has landed, which makes it the
    readiness probe; the command itself is C3-local so it answers during
    the wait.
    """
    end = time.time() + timeout
    while time.time() < end:
        try:
            r = request(s, {"cmd": "/debug/getProxyState"}, timeout=1.0)
            if r.get("active0"):
                return True
        except (TimeoutError, json.JSONDecodeError):
            pass
        time.sleep(0.5)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--out", default="panel_shot.png")
    ap.add_argument("--cmd", help='raw /debug command JSON, e.g. \'{"cmd":"/debug/getAudioStats"}\'')
    ap.add_argument("--no-wait", action="store_true", help="skip the link-ready poll")
    args = ap.parse_args()

    with open_port(args.port) as s:
        if not args.no_wait and not wait_ready(s):
            print("warning: DSP link did not come up; continuing anyway", file=sys.stderr)
        if args.cmd:
            reply = request(s, json.loads(args.cmd))
            print(json.dumps(reply, indent=1))
            return 0
        reply = request(s, {"cmd": "/debug/getDisplayFramebuffer"}, timeout=5.0)
        if "data" not in reply:
            print(json.dumps(reply, indent=1), file=sys.stderr)
            return 1
        fb_to_png(reply["data"], args.out)
        print(f"wrote {args.out} ({reply.get('w')}x{reply.get('h')})")
        return 0


if __name__ == "__main__":
    sys.exit(main())
