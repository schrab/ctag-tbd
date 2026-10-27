# UI-Split Handoff — P0–P3 Status

**Branch:** `feat/ui-split` (all commits through 88b468db + debug logging)
**Date:** 2026-10-28
**SDD:** `doc/ui-split-sdd.md` | **Bring-up:** `doc/uimcu-bringup.md`

## What's done

- **P0** ✅ `perf/dsp-pipeline` = radio-free baseline
- **P1** ✅ `protocol/` shared component + 17 host tests (28/28 pass) — commit 90057fe3
- **P2** ✅ `uimcu/` C3 panel firmware — menu + pages + SPI display + input + bench channel — commit 74e895db
- **P3** ✅ Both firmwares flashed — A1S LinkEndpoint (UART1 @921600) + C3 LinkClient (UART0 @921600) — commit 3128f59f
- **Docs** ✅ bench rules + hardware post-mortem — commit 88b468db
- **Debug logging** — added to both firmwares (key events, link status, display init) — **UNCOMMITTED, needs build + flash**

## What's NOT working

**The UART link handshake has NEVER worked.** Both boards are flashed, the C3 sends `link/hello` retries every second, but the A1S never responds.

## What was verified from code

- A1S LinkEndpoint: UART_NUM_1, 921600 baud, TX=GPIO18, RX=GPIO19, task at prio 1 on core 0
- C3 LinkClient: UART_NUM_0, 921600 baud, TX=GPIO21, RX=GPIO20, task at prio 2
- Protocol framing (STX/ETX), message names, ParseHandshake — all verified correct by code review
- Host tests 28/28 pass (framing round-trips, handshake choreography)
- A1S main.cpp line 93 calls `LinkEndpoint::Start()` inside `#ifdef CONFIG_TBD_PLATFORM_BBA`
- C3 `app_main.cpp` calls `DspProxy::Init()` which calls `LinkClient::Start()` via `DspProxyLink.cpp`

## Debugging done (all failed to find the cause)

- C3 console: `LinkCli: hello retry (online=0)` every 1000ms — confirmed multiple times
- A1S console: boot completes normally, but `LinkEp: link endpoint on UART1 GPIO18/19 @921600` was seen only ONCE in all captures
- A1S responds to `/debug/getUiState` on UART0 bench (proves chip alive, app running)
- A1S does NOT respond to `link/hello` sent on UART0 bench (expected — link is on UART1, not UART0)
- Both boards respond to `esptool chip_id` (proves chips alive)
- Wire swap (TX↔RX) did not fix the issue
- Full erase + re-flash did not fix the issue
- The A1S silk screen has TX/RX labels swapped on the header — user swapped wires, no change
- Both wiring configurations (straight and crossed) tried — link never worked in either

## Root cause hypothesis

The link protocol code has never been tested between the two boards. Most likely causes:

1. **A1S LinkEndpoint task might not be starting.** The boot log showed `LinkEp: link endpoint on UART1` only ONCE out of many boots. Check if `LinkEndpoint::Start()` is actually called on every boot. If missing, check the `#ifdef CONFIG_TBD_PLATFORM_BBA` guard in `main.cpp` line ~93.

2. **UART1 might not be properly configured on the A1S.** `LinkEndpoint::Start()` calls `uart_driver_delete(UART_NUM_1)` to remove the midiuart driver, then installs a new driver. If the midiuart driver was never installed (static constructor might not run), `uart_driver_delete()` fails silently, and the subsequent `uart_driver_install()` might fail because the UART is in a bad state.

3. **The C3's UART0 might conflict with the ROM bootloader.** ESP32-C3 ROM outputs boot messages on UART0. After the app boots, the app takes over UART0. But there might be residual ROM configuration.

4. **The wire swap didn't help because the issue is not wiring.** Both wiring configurations tried — link never worked.

## Debugging plan for next session

1. **Build and flash both boards** with debug-logging firmware (changes already made but not built/flashed — see uncommitted changes)
2. **Capture A1S boot log** at 115200 baud — look for `LinkEp: link endpoint on UART1` on EVERY boot. If missing, add `ESP_LOGI` at the very top of `LinkEndpoint::Start()` before any UART operations.
3. **Test A1S UART1 loopback**: connect A1S GPIO18 (TX) directly to A1S GPIO19 (RX) with a jumper. Send bytes from the link task and verify they loop back. This isolates the A1S UART1 from the C3.
4. **Test C3 UART0 loopback**: same — connect C3 GPIO21 (TX) to C3 GPIO20 (RX), verify the link client sees its own hello.
5. **If both loopbacks work**, the issue is the wire between the boards (bad dupont, wrong pin, or broken wire).

## Uncommitted changes on feat/ui-split

Debug logging added to:
- `uimcu/main/input/UserInput.cpp` — logs BTN_A/BTN_B press/release, encoder deltas
- `uimcu/ui/Display.cpp` — logs init steps (bus setup, SSD1309 configure, flush)
- `uimcu/main/LinkClient.cpp` — logs hello retries, frames from DSP
- `main/LinkEndpoint.cpp` — already had handshake/offline logging from P3

**These need to be built and flashed to both boards before the next debugging session.**

## Hardware state

| Board | Status | Notes |
|-------|--------|-------|
| C3 #2 (loose) | ✅ Working | Flashed with canonical firmware, bench verified |
| C3 #1 (soldered on carrier) | ❌ USB-dead | Backfeed casualty — recoverable via USB-UART on UART0 |
| A1S | ✅ Running | Flashed with link firmware, Elements rendering |
| OLED panel | ❌ Dead | Reversed-power casualty — replacement needed (any 7-pin SPI SSD1306/1309) |

## Critical bench rules (DO NOT violate)

1. **C3 GPIO18/19 = USB data pins.** Never wire anything to them. Link = GPIO20/21.
2. **No GND wire between boards while both are USB-powered.** USB cables share ground through the host. An extra bond to the A1S's audio-return ground kills the C3's USB CDC.
3. **Never tie 3V3 between boards while either is unpowered.** Backfeed killed C3 #1 and the OLED panel.
4. **GPIO8/9 = C3 strapping pins** (encoder E1). Need 10k pull-ups to 3V3.
5. **A1S port-open auto-resets the chip** (CP2102 DTR/RTS). Wait ≥9 s after opening before probing.
6. **C3 USB-Serial-JTAG does not replay output** for late-opening hosts.
7. **The panel app owns the C3's USB serial** (`usb_serial_jtag_driver_install`). esptool can't sync while the app runs. Download mode = hold BOOT through power cycle, verify `lsusb` device number increments.
8. **A1S OLED-less boot logs a benign `SSD1309: OLED configuration failed` error** — don't chase it.
9. **The A1S silk screen has TX/RX labels swapped** on the header — verify with a scope or loopback, don't trust the silk.

## Flashing commands

```bash
# C3 panel (full flash OK):
source /home/schrab/esp/esp-idf-v5.5.4/export.sh
export PATH="$HOME/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin:$PATH"
cd uimcu && idf.py -p /dev/ttyACM0 flash

# A1S DSP (APP ONLY — never idf.py flash, it wipes storage!):
source /home/schrab/esp/esp-idf-v5.5.4/export.sh
cd /home/schrab/Projects/ctag-tbd
python -m esptool --chip esp32 -b 460800 --port /dev/ttyUSB0 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m 0x10000 build/ctag-tbd.bin
```

## Serial port handling (the RIGHT way)

```python
# ALWAYS use context manager — auto-closes even on error:
with serial.Serial('/dev/ttyUSB0', 115200, timeout=1) as s:
    s.dtr = False  # DON'T toggle — this resets the A1S!
    s.rts = False
    time.sleep(9)  # A1S needs ≥9 s after port open (auto-reset + boot)
    s.write(b'\x02{"cmd":"..."}\x03')
    time.sleep(2)
    buf = s.read(8192)

# NEVER open both ports simultaneously in one script.
# ALWAYS close before opening the other port.
# If port is stuck: unplug + replug the USB cable (10 s).
```
