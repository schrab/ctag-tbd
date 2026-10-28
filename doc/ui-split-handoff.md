# UI-Split Handoff — P0–P3 Status

**Branch:** `feat/ui-split` (all commits through 76f642c1 + link fixes)
**Date:** 2026-10-28 (evening — LINK RESOLVED)
**SDD:** `doc/ui-split-sdd.md` | **Bring-up:** `doc/uimcu-bringup.md`

## RESOLVED: the link works (2026-10-28)

**The P3 handshake now completes on a clean boot with zero console
intervention**: `panel handshake ok` → `boot metadata pushed` on the A1S,
`DSP link online (proto 1.0)` on the C3, and the C3's DspProxy cache
reflects the DSP's real state (active0=Mbeads, active1=Void).

**TRUE root cause (found via the self-loopback): the A1S board's GPIO19
input trace is defective above ~250 kBd.** The ex-MIDI-IN path on the A1S
distorts any signal entering GPIO19 — proven with a TX→RX jumper loopback
on the A1S ALONE (no C3, no cable, no ground loop): clean at 115200/250k,
structurally garbled at 460800/921600, identical on UART1 AND UART2 (not a
peripheral issue), identical with the DSP idle (not load-related), while
GPIO18's own TX decodes cleanly on the panel at 460800. Link RX moved to
**GPIO23** — the same loopback is clean there to 1 Mbps. The earlier
theories (wire between boards, ground return, UART clocks, driver
leftovers, Kconfig) were all systematically disproven; sdkconfig has no
UART rate/clock options at all.

**Fix applied:** `main/LinkEndpoint.cpp` — `uart_set_pin(kUart, 18, 23)`,
`kLinkBaud` = 921600 (SDD rate restored), back on UART1;
`uimcu/main/LinkClient.cpp` — `kLinkBaud` = 921600. Acceptance at 921600
PASSED: clean boot → `panel handshake ok` → `boot metadata pushed`, no
quiet events, C3 online. The encoder note is moot: encoder + OLED live on
the C3; the A1S header's GPIO5/23 are physically free.

Physical consequence: the C3's TX wire lands on **A1S GPIO23** (not
GPIO19). GPIO19 stays unwired — do not "fix" this back to the SDD pin.

**Debug helper kept (both firmwares):** `{"cmd":"/debug/setLinkBaud",
"value":N}` retunes the link UART at runtime (A1S via DebugUI on UART0,
C3 via DebugLink on USB console). The hunt instrumentation (raw hex
logging, loopback TX stimulus, RX reroute, panel button/display logs) was
removed after the fix — re-add from git history (75445843) if needed.

## Known residue

- The C3 link task intermittently went silent for tens of seconds under
  garbage bombardment during the mismatched-baud hunts; it recovers on its
  own and was not observed at a clean matched rate. If it recurs, look at
  DspProxy::OnLinkFrame garbage-frame handling (the C3 calls it on every
  non-hello frame, even offline — unlike the A1S which gates on s_online).

## Bandwidth measured (2026-10-28) — 921600 has huge headroom

- Params JSON, biggest plugin (Mbeads): **1464 B**; Void: 20 B; config: 555 B.
- Whole boot-metadata push measured **< 460 ms even at 115200** (≈5 KB).
- The OLED does NOT ride the link: the C3 renders locally over SPI, 30 fps
  costs the link nothing. Link load: 10 Hz stream ≈ 1 KB/s, ~1.5 KB per
  param change (coalesced by the 100 ms hash-diff). At 921600 = 92 KB/s.

## What's done

- **P0** ✅ `perf/dsp-pipeline` = radio-free baseline
- **P1** ✅ `protocol/` shared component + 17 host tests (28/28 pass) — commit 90057fe3
- **P2** ✅ `uimcu/` C3 panel firmware — menu + pages + SPI display + input + bench channel — commit 74e895db
- **P3** ✅ **live handshake VERIFIED at 921600** (RX on A1S GPIO23)
- **P4 stages 1+2** ✅ TRS DIN MIDI on the C3 (UART1, TX=GPIO1/RX=GPIO10
  @31250) relayed as `link/midiIn`; A1S dormant-MIDI fix (link task calls
  `Midi::Update`, CV/trig buffers wired into the audio path);
  `/link/midiFlags` set/query; C3 MIDI panel page (6th panel)
- **P5 stage 1** ✅ C3 softAP **TBD-Panel** + REST bridge over the DspProxy
  cache (getPlugins/getActivePlugin/getPluginParams/getPresets/
  getConfiguration/getIOCaps + setActivePlugin/setPluginParam/savePreset/
  loadPreset/setConfiguration mutations as link commands)
- **P4 BLE stage** ✅ **working** — NimBLE central with DEFAULT buffers
  initializes fine on the bench C3 (chip rev v0.4 = revision number 4,
  which HAS the ECO3 ROM — REV_MIN_3 = "v0.3" was always correct; the
  earlier crash was RAM, not silicon: with the WiFi AP resident only
  ~66KB internal heap remained at BT init, and NimBLE defaults need
  ~90KB. **BLE-MIDI required, WiFi optional → NetRest::Start() is
  commented out**; without WiFi there is 160KB free / 114KB contiguous
  at BT init). Never trim NimBLE pool buffers aggressively — that is
  espressif/esp-idf#15398's exact crash. BtMidiReceiver::Init still
  skips chips with revision < 3 (pre-ECO3 ROM). Buttons: the state
  machine is verbatim-A1S and fine, but the C3 thresholds are uncalibrated
  guesses for the actual panel ladder — `/debug/getBtnAdc` returns the
  averaged raw ADC1_CH0 value for calibration (idle = 4095).
- **P4 end-to-end MIDI** ✅ **verified live (2026-10-29)** — SMC-PAD over
  BLE: scan → connect (persistent peer, auto-reconnect) → subscribe →
  notes/aftertouch byte-perfect into the DSP parser; CC learn captures.

## MIDI over the panel link — the complete picture (2026-10-29)

**Chain:** pad/keys → C3 (BLE via BtMidiReceiver, TRS DIN via MidiLink
UART1 @31250) → `{"cmd":"/link/midiIn","d":[bytes]}` frames → A1S
`Midi::FeedLinkBytes` → `Midi::Update()` (called from the link task every
10 ms) → parser → `distribute.*` → midi CV/trig arrays → audio task
(`pd.cv`/`pd.trig` = `Midi::GetCVTrig()`).

**THE channel map (what the DSP listens to):**
- **CH1** = global: CCs (the SMC-PAD's encoders send CC 30–45 here),
  CC learn captures here. Global CC→CV/trigger tables in Midi.cpp.
- **CH2** = Voice A: notes feed **A.NOTE/A_VELO** (+ A_PB, A_AT...).
  **A.NOTE is fed by CH2 notes — pad notes on CH10 will NEVER drive it.**
- **CH10–13** = percussion (`IS_PERCUSSION`): pad notes 36–51 map to
  percussion velocity CVs (`noteToCVid_perc`: 36→A_SC6_75, 37→A_SC7_76,
  ... 16 pads across the A/B/C bank SC slots) + percussion trigs.
- Mapping guide: to pitch-track pad hits, map to the pad's percussion
  velocity CV (or send pads on CH2); A.NOTE needs a CH2 source.
- Aftertouch on CH10 arrives (0xD9); the SMC-PAD also emits M-VAVE SysEx
  (F0 35 59 10...) — parser skips it.

**Fixes that made it work (all in f06172bc/d75db269):**
1. **The protocol bug — THE root cause:** kMsgMidiIn/kMsgMidiOut were
   defined "link/midiIn" (no slash) while handleLinkCommand's gate drops
   every command not starting with "/link/" — the A1S received the frames
   by the dozen (frame probe proved it) and dropped every one. Names are
   now "/link/midiIn" + "/link/midiOut".
2. **BLE-MIDI decode:** the old parser treated every bit7-set byte as a
   timestamp, eating MIDI status bytes (0x90/0xB0) — pad hits lost their
   status. Now a state machine: timestamps only at message boundaries,
   running-status override, real-time ≥0xF8 anywhere.
3. **CCCD subscribe:** write_flat chose write-without-response and the
   M-VAVE silently ignored it; now the 0x2902 descriptor is discovered
   and written with a Write REQUEST, re-subscribed after pairing (CCCD
   resets on encryption), and the pad's pairing (PASSKEY_ACTION) is
   auto-accepted.
4. **Persistence:** the last peer is stored in NVS, auto-reconnected at
   boot and retried every 3 s; Disconnect forgets; 250 ms keepalive
   writes keep the pad's stream alive (it starts notifying once the
   client writes — the Ableton session does the same with LED data).
5. The A1S's Midi::Flush no longer touches UART1 (it IS the link).

**Debug helpers still flashed:** `/debug/setLinkBaud` (both ends),
`/debug/getBtnAdc` (C3), plus rate-limited probes: `relay #`/`midiIn #`/
`frame #`/`notify #` lines in both consoles — trim at the next cleanup.
Reference repo: ~/Projects/open-smc-pad (the pad's own RE: stock pads =
notes 36–51 on CH10, encoders CC 30–45 on CH1, just-works pairing with
bonding; the bench pad runs STOCK firmware).
- **Panel hardware status updates**: the OLED is ALIVE (the "dead panel"
  diagnosis was wrong — the fault was the DC-pin connection, now fixed);
  intermittent C3 boot failures were the **GPIO8/9 strapping pins being
  dragged by the encoder** — hardware 10k pull-ups to 3V3 are required
  (bench rule 4, confirmed by the user: turning the encoder one step
  changes whether the board boots).
- **Docs** ✅ bench rules + hardware post-mortem — commit 88b468db
- **P6 cutover** 🔨 **IN PROGRESS (this working tree, uncommitted→committed here)**:
  - A1S DebugUI: the UART0 bench task is REMOVED; DebugUI::handleCommand
    stays as the pure /debug dispatcher LinkEndpoint routes panel commands
    to. UI-side commands (getUiState, framebuffer, injectEvent, getMixState)
    are gone from the DSP — the C3's DebugLink owns those.
  - A1S sources REMOVED from the BBA build (kept in the tree for MK2/AEM):
    UIMenu, menupages (C3 is the single copy now), UserInput, Display.
    components/drivers BBA: ssd1306.c/ssd1306_i2c.c/encoder.cpp out
    (GPIO23 = link RX now; no local encoder).
  - **GPIO21 = amplifier enable** (user: it enables the amp when selected
    as output). The old Display I2C-SDA config enabled it as a side
    effect; codec_bba InitCodec now configures GPIO21 as an output driven
    high explicitly. VERIFY audibly on first boot.
  - Favorites: the dead ui_task is now MK2/AEM-only (BBA branch of
    StartUI is a no-op).
  - main.cpp: BBA path = InitFS/InitSD → StartSoundProcessor →
    LinkEndpoint::Start. No Display, no UIMenu, no DebugUI task.
  - **WIP status**: builds clean through the CMake/UI fixes; the final
    link + MIDI + DRAM validation on hardware is the next step.

## What was verified from code

- A1S LinkEndpoint: UART_NUM_1, **115200** baud, TX=GPIO18, RX=GPIO19, task at prio 1 on core 0
- C3 LinkClient: UART_NUM_0, **115200** baud, TX=GPIO21, RX=GPIO20, task at prio 2
- Protocol framing (STX/ETX), message names, ParseHandshake — all verified correct by code review
- Host tests 28/28 pass (framing round-trips, handshake choreography)
- A1S main.cpp line 93 calls `LinkEndpoint::Start()` inside `#ifdef CONFIG_TBD_PLATFORM_BBA`
- C3 `app_main.cpp` calls `DspProxy::Init()` which calls `LinkClient::Start()` via `DspProxyLink.cpp`

## Previous session's red herrings (all resolved)

- "`LinkEp: link endpoint on UART1` seen only ONCE" — capture artifact, not
  firmware: the line prints on EVERY boot (previous captures were garbled —
  the A1S console is plain 115200, per sdkconfig; the "460800 console" note
  in older notes was wrong).
- "hello retry logging lost" — the pre-crash session's uncommitted
  LinkClient.cpp logging was wiped; re-added.
- Software/config hypotheses (task not starting, midiuart driver clash, ROM
  UART0 conflict, GPIO18/19 SPI theft via codec.cpp's unguarded initSPI) —
  all investigated and cleared: BBA links `codec_bba.cpp` (no VSPI init),
  BBA is CONFIG_WIFI_UI (SerialAPI dormant), no runtime UART0 baud override.

## Uncommitted changes on feat/ui-split (COMMIT THESE — a crash lost work last time)

- `main/LinkEndpoint.cpp` — kLinkBaud 115200 + raw-RX/offline-frame
  instrumentation + `/debug/setLinkBaud` support (`LinkEndpoint::SetBaud`)
- `main/LinkEndpoint.hpp` — SetBaud declaration
- `main/DebugUI.cpp` — `/debug/setLinkBaud` command + include
- `uimcu/main/LinkClient.cpp` — kLinkBaud 115200 + raw-RX logging + hello
  retry log + SetBaud
- `uimcu/main/LinkClient.hpp` — SetBaud declaration
- `uimcu/main/DebugLink.cpp` — `/debug/setLinkBaud` command + include
- `uimcu/main/input/UserInput.cpp`, `uimcu/ui/Display.cpp` — earlier panel
  debug logging (from 76f642c1, committed) + this session's flash of both
  boards

**Both boards are already flashed with this firmware.**

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
7. **The panel app owns the C3's USB serial** (`usb_serial_jtag_driver_install`). Historically esptool couldn't sync while the app ran (BOOT-held download mode as fallback), but on 2026-10-28 `idf.py -p /dev/ttyACM0 flash` synced and flashed fine with the app running — try the direct flash first.
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
