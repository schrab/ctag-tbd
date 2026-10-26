# uimcu bring-up (P2+P3): ESP32-C3 panel controller + link to the A1S DSP

Status: **P2 validated on hardware 2026-10-27** (stub mode, full menu
navigation); **P3 implemented** — both firmwares build and flash, link
endpoints run, C3 offline path verified ("DSP OFF" on HOME, empty cache,
bench still navigable). The live handshake needs the 3 link wires below.
Design: `doc/ui-split-sdd.md` §4/§5/§8 (P3), §11 (bench channels).

## Hard-won gotchas (all hit and fixed during bring-up)

1. **RapidJSON allocations must not be SPIRAM-only.** The repo's RapidJSON
   routes `CrtAllocator` through `heap_caps_malloc(MALLOC_CAP_SPIRAM)`;
   on the PSRAM-less C3 every allocation returned NULL and the first parse
   asserted (`Stack::PushUnsafe` boot-loop). Fixed with an explicit
   SPIRAM→internal fallback in `components/rapidjson/rapidjson/allocators.h`
   (A1S behavior unchanged — PSRAM still wins there).
2. **Bench transport = usb_serial_jtag DRIVER, not the console VFS.** The
   VFS path only flowed while the boot-log burst kept the wire busy, then
   stalled to a ~600 ms one-reply cadence and worse. DebugLink installs
   `usb_serial_jtag_driver_install()` (4 KB RX/TX rings) and routes ESP_LOG
   through `esp_log_set_vprintf` — the A1S DebugUI pattern applied to USJ.
3. **DTR/RTS transitions reset the C3** (rst:0x15 USB_UART_CHIP_RESET).
   Host scripts must set `dtr`/`rts` *before* `serial.open()` and never
   toggle them afterwards; `cat /dev/ttyACM0` sees nothing because it never
   asserts the lines.
4. **Floating ADC/button pin floods the event queue.** GPIO0 gets the weak
   internal pull-up in `UserInput::Init()` so the bare board idles at
   BTN_NONE; the wired ladder overdrives it.
5. **Injected `enc` deltas >1 are halved** (A1S ROOT encoder acceleration,
   faithfully ported) — a host script sending `delta:2` moves ONE panel, and
   that is correct.

## What runs on the C3 (P2)

- The full menu port: UIMenu core, 5 panels (MIX/TAPE/HOME/MOD/PARAMS),
  ported pages calling `DspProxy` instead of the A1S engines.
- `DspProxy` **stub**: fake plugin bundle (Elements + 5 more), fake params
  with groups/bool/enum/play, config, presets, favorites, an animated
  Marbles output snapshot, fake VU meters, and a fake SD tree
  (`/sd/drums/kick.wav` …). No link, no DSP needed.
- Display: SSD1309 over SPI (10 MHz — raise to 20 MHz once wiring is proven).
- Input: 2 GPIO-ISR quadrature encoders + A1S-style ADC-ladder buttons.
- Bench channel: STX/ETX JSON on the USB console (see below).

## Wiring (SDD §2.1 — verify against the physical board revision first)

| Function | Pins | Notes |
|---|---|---|
| OLED SSD1309 SPI | SCK=6, MOSI=7, CS=2, DC=3 | RST tied 3V3 via RC |
| Encoder E1 (value) | A=8, B=9 | **strapping pins**: external pull-ups, never hold at reset |
| Encoder E2 (panel) | A=4, B=5 | |
| BTN_A + BTN_B | GPIO0 (ADC1_CH0) | resistor ladder mirroring the A1S BTN design; calibrate thresholds in `main/input/UserInput.cpp` |
| Link to A1S | RX=20, TX=21 | **C3-side pins** (UART0, 921600 8N1) — the A1S end is on the A1S's MIDI pins 18/19, see below |
| TRS MIDI IN | TIP→opto→GPIO10 | UART1 RX, 31250 — wire now; firmware reads it from P4 |
| TRS MIDI OUT | GPIO1→TIP | UART1 TX, 31250 — same, P4 |
| USB | GPIO18/19 | console + flashing |

## TRS MIDI jacks (P4 hardware — wire now, firmware follows)

MIDI TRS **Type A** (RP-054: Tip = data, Ring = +V, Sleeve = GND). The
A1S's old MIDI DIN jacks move here; electrically identical, just 3.5 mm.

**TRS IN** (receiver side, opto-isolated per SDD §2.1 — H11L1, PC900V or
6N138; check the part's datasheet for its pin numbers):

```
jack RING ──220Ω── opto LED anode
jack TIP  ───────── opto LED cathode     (1N4148 anti-parallel across the
jack SLEEVE ─────── GND                    LED for polarity safety)
opto output (open collector) ──┬── GPIO10 (UART1 RX)
                               └──10kΩ── 3.3V pull-up
opto VCC = 3.3V, opto GND = GND
```

The current loop (sender 33Ω + receiver 220Ω) is sized for 3.3 V — about
6–8 mA through the opto LED. A 5 V sender still isolates fine; keep the
receiver side at 3.3 V.

**TRS OUT** (driver side, direct drive is fine at 31250):

```
GPIO1 (UART1 TX) ──33Ω── jack TIP
3.3V ──33Ω── jack RING
GND ────────── jack SLEEVE
```

Idle-high TX = no loop current; a start bit sinks the loop exactly like a
DIN out. Type B gear (old Roland) needs a tip/ring swap adapter.

Firmware status: GPIO1/10 are reserved in the SDD pin map, but the C3
firmware does not read them until P4 (UART1 @31250 + forwarding over the
link into the A1S parser). Wiring the jacks now is safe — the pins idle.

Pin-ownership sanity check, since the link crosses two different chips:
on the **C3**, GPIO20/21 are plain header pins (silk RX/TX) — the USB-C
console/flash runs on GPIO18/19 (native USB-Serial-JTAG), so 20/21 are
free. On the **A1S** the link deliberately reuses the **MIDI DIN UART
pins** — GPIO18 (ex-DIN out, TX) and GPIO19 (ex-DIN in, RX); the link
firmware just retunes that same UART1 from 31250 to 921600. (A1S
GPIO20/21 would be the wrong choice anyway: on the classic ESP32 GPIO20
isn't bonded out, and GPIO21 is the OLED I²C SDA.)

## Link wiring (P3) — three wires, crossover

| From | To | Signal |
|---|---|---|
| A1S GPIO18 (TX, ex-MIDI DIN out pin) | C3 GPIO20 (RX) | DSP → panel |
| A1S GPIO19 (RX, ex-MIDI DIN in pin) | C3 GPIO21 (TX) | panel → DSP |
| A1S GND | C3 GND | common ground |

921600 8N1. Both ends bring themselves up: the C3 retries `link/hello`
every second until the A1S answers `hello_ack`, then the A1S pushes boot
metadata (plugin list, active plugins + params, config, presets,
favorites, full Marbles state, SD-mounted flag) and starts the 10 Hz
stream + heartbeat. Until then the C3 runs on an empty cache and shows
**DSP OFF** at the top-right of the HOME panel (verified via framebuffer
shot). Any change made on the A1S side (its own UI until P6, bench
scripts) re-pushes within one stream tick — the link diffs the model's
cached JSON blobs at 10 Hz.

## Flashing

```bash
# C3 panel (full flash is safe — nothing valuable in its storage):
cd uimcu && idf.py -p /dev/ttyACM0 flash

# A1S DSP — APP ONLY (never idf.py flash: it would overwrite the storage
# partition with the repo's factory SPIFFS image):
source /home/schrab/esp/esp-idf-v5.5.4/export.sh
python -m esptool --chip esp32 -b 460800 --port /dev/ttyUSB0 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m 0x10000 build/ctag-tbd.bin
```

The A1S link endpoint takes over UART1 at boot (the dormant midiuart
driver is replaced with link-sized rings; `Midi::Update()` stays dormant
until P4 wires DIN to the panel side). Watch for `task_wdt: link_ep` in
the A1S log — it must stay quiet (a zero-tick `vTaskDelay` busy-spin bug
was fixed; both link tasks now delay one full tick).

If the OLED image is shifted 2 px, set `OLED_COLUMN_OFFSET 2` in
`uimcu/ui/Display.cpp`.

## Build + flash

```bash
source /home/schrab/esp/esp-idf-v5.5.4/export.sh
# export.sh only exports toolchains for the target of the project you were
# in; the RISC-V toolchain must be added by hand (already installed):
export PATH="$HOME/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin:$PATH"
cd uimcu && idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Console = USB-Serial-JTAG only (UART0/GPIO20/21 is reserved for the link;
`sdkconfig.defaults` pins this).

## Bench validation without any panel hardware wired

The USB console serves the same STX/ETX JSON as the A1S UART0 bench
(`DebugUI`), so host scripts only change their port. **Open the port with
`dtr=False, rts=False` set before `open()`** (line transitions reset the
chip). Every command may carry an integer `"seq"` which the reply echoes —
use it to correlate responses; replies can interleave with ESP_LOG bytes.

```
{"cmd":"/debug/getUiState","seq":N}
{"cmd":"/debug/getDisplayFramebuffer","seq":N}
{"cmd":"/debug/injectEvent","action":"ok"|"mod"|"back"|"enc"|"enc2",
                "delta":n,"count":n,"seq":N}
{"cmd":"/debug/setActivePlugin","ch":0,"id":"...","seq":N}
{"cmd":"/debug/getProxyState","seq":N}
```

Verified-on-hardware smoke sequence (see session log for the full script):
getUiState → `enc2` steps move the panel (deltas >1 halve) → `ok` sets
nav=1 → `back` returns → `setActivePlugin` reflects in `getProxyState` →
`getDisplayFramebuffer` returns 1024 B with real content.

## Expected behavior on the bare board (nothing wired)

- Boots, draws the fw-version splash to the (unconnected) SPI bus without
  erroring, then the HOME panel.
- Encoders/buttons float — use `injectEvent` for navigation.
- ADC floats may inject random button events; harmless for bench work.

## Known bring-up risks (SDD §10)

- E1 on strapping pins 8/9: power-on with encoders in random states must
  still boot (download-mode entry needs GPIO9 sampled with pull-up).
- ADC ladder thresholds are the A1S values — recalibrate to the panel's
  actual resistors when wired (`uimcu/main/input/UserInput.cpp`).
- SSD1309 init sequence/column offset varies by module; adjust
  `OLED_COLUMN_OFFSET` / init commands in `Display.cpp` if the image looks
  wrong.

## What changed vs. the A1S UI sources (single copy lives in `uimcu/ui/`)

- Pages call `DspProxy` (one class cataloguing every DSP-side call, SDD §6)
  instead of SPManager/MarblesEngine/Codec/SDAudio/Favorites/FileSystem; SD
  listing goes through `DspProxy::ListSdDir` (link proxy from P3).
- `InputEvent` gained `ENC2_DELTA`; UIMenu accepts it (and ENC_DELTA) for
  panel switching in ROOT.
- Display: same framebuffer API, direct SSD1309 SPI backend.
- The A1S `main/` copies stay until P6 cutover deletes them; after that
  `uimcu/ui/` is the single source.
