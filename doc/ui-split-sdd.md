# SDD: UI Split — ESP32-C3 Panel Controller + UART-Linked ESP32-A1S DSP

Status: design, not yet implemented. Baseline: branch `test/radio-free-dsp`
(9d8ef982) — radio-free default, 8KB `debug_ui` stack, reverb buffer prefers
internal DRAM. Measured ground truth in `doc/radio-free-dsp-test.md`.

## 1. Goals / non-goals

Goals:
- Move the entire panel UI (OLED, encoders, buttons) off the ESP32-A1S onto an
  ESP32-C3 Super Mini, linked over UART (the A1S's old MIDI UART pins).
- Move MIDI DIN in/out **and** BLE MIDI to the C3; keep the DSP module
  radio-free permanently (no WiFi/BT on the A1S).
- Keep all DSP/codec/SD/SPIFFS/sample-ROM on the A1S.
- Unload the A1S as far as possible: DRAM (~+25KB beyond the measured 192KB
  free), core 0 (only MIDI/link + helpers + IDLE remain), no UI stack churn.

Non-goals (this SDD): no DSP changes, no Elements resolution work (covered by
`doc/s3-bench-plan.md` / SIMD plans), no PCB design.

## 2. Hardware

### 2.1 C3 Super Mini pin map (exact 13-pin fit, zero spare)

Board facts: usable GPIOs 0–10, 20, 21 (13). GPIO18/19 = USB-C (native
USB-Serial/JTAG = console/flash, consumes no header pins). GPIO11–17 =
internal flash. Strapping: GPIO2, 8, 9.

| Function | Pins | Notes |
|---|---|---|
| Link → A1S (UART0) | RX=20, TX=21 | 921600 8N1, crossover to A1S UART1 |
| OLED SSD1309 (SPI) | SCK=6, MOSI=7, CS=2, DC=3 | RST tied 3V3 via RC; 10–20 MHz bus |
| Encoder E1 (panel/value) | A=8, B=9 | strapping pins: external pull-ups; do not hold at reset |
| Encoder E2 | A=4, B=5 | GPIO-ISR quadrature (C3 has no PCNT) |
| Buttons BTN_A + BTN_B | both on GPIO0 (ADC1_CH0) | resistor ladder, mirrors the A1S GPIO36 design (`UserInput.cpp:41–66` thresholds model) |
| MIDI DIN in (UART1 RX) | 10 | 31250 8N1, optocoupler input |
| MIDI DIN out (UART1 TX) | 1 | 31250, standard DIN-out wiring |

Confirmed decisions: buttons share one ADC pin (resistor ladder) so the
13-pin budget fits; SPI OLED kept over I²C for frame speed (~2 ms vs ~10 ms
per full frame). Verify the wiring against the physical board revision
before bring-up.

### 2.2 A1S side

- Link inherits UART1 on **GPIO18 (TX) / GPIO19 (RX)** — the former MIDI DIN
  pins (`components/drivers/midiuart.cpp:42`), 921600.
- Frees GPIO21/22 (OLED I²C), 5/23 (encoder PCNT), 36 (button ADC).
- GPIO0 = I2S MCLK constraint untouched. UART0 (GPIO1/3) stays the bench
  debug channel — see §4.5.

## 3. System architecture

| Concern | Today (A1S) | After split |
|---|---|---|
| Menu engine + 6 pages | `ui_menu` task, core 0 | **C3** (ported, see §5.1) |
| Display driver | I²C SSD1309 `Display.cpp` | **C3** SPI rewrite, same framebuffer API |
| Input | `input_task` (ADC buttons + PCNT enc) | **C3** GPIO-ISR encoders + ADC ladder |
| BLE MIDI | NimBLE central on A1S (`BtMidiReceiver`) | **C3** (port; same NimBLE API) |
| MIDI DIN | UART1 31250 on A1S | **C3**; bytes forwarded over link |
| MIDI parsing → engines | `Midi::Update()` on A1S (currently **dormant**: only caller `Control::Update()` has no caller on BBA) | stays on A1S; **the link task becomes its caller** — first live MIDI path on this branch |
| WiFi/REST | A1S (`network.cpp`, `RestServer`) | **C3** (phase 5), REST handlers proxy DSP via link |
| DSP, codec, SD, SPIFFS, sample ROM | A1S | unchanged |
| Debug/bench channel | UART0 STX/ETX JSON (`DebugUI`) | unchanged on A1S |

Task map after cutover (A1S): core 1 = `audio_task` (prio 23) + PERF logger;
core 0 = MIDI/link task (prio 5) + Elements/Mbeads helpers (transient/grain)
+ IDLE. Nothing else.

## 4. Link protocol

Framing: **STX(0x02)/ETX(0x03) JSON**, same shape as `DebugUI::handleCommand`
— transport-generalize that dispatcher so UART0 (bench) and UART1 (link) share
it. 921600 8N1. Version handshake + heartbeat at boot; the C3 renders a
"DSP offline" state when the heartbeat dies (and the DSP keeps playing with
last values if the C3 dies).

### 4.1 Commands (UI → DSP)
Reuse the existing debug command set as-is: `setActivePlugin`,
`setPluginParam`, `getPluginParams`, `getConfig`, `setConfigValue`,
`getAudioStats`, `setElementsMode`, plus new thin wrappers for the DspProxy
surface (§6): presets save/load, favorites activate, SDAudio start/stop,
MarblesEngine get/set (or full-state set, §4.2), MIDI enable flags.

### 4.2 Pushes (DSP → UI)
- **Boot metadata** (once, after handshake): plugin list JSON, active plugin
  params JSON (per channel), config JSON, presets, favorites, full Marbles
  state, IO capabilities.
- **On-change pushes**: param values changed by MIDI learn / REST; active
  plugin switch; config commits.
- **Streaming @10 Hz**: VU peaks (4×uint32), Marbles output snapshot
  (bitfield, `MarblesEngine::GetOutputSnapshot`), CPU overload flag.
- **SD proxy**: directory listings for HOME/TAPE (pages scan `/sd` via POSIX
  today, `UIMenuPageHome.cpp:340–353`) → `fs.list`/`fs.isMounted` messages.
- **MIDI transport**: raw MIDI bytes as hex arrays, C3→A1S into the parser.
  MIDI line rate is 3125 B/s max; JSON overhead is irrelevant at 921600.
  Out: A1S→C3 for DIN-out forwarding if ever needed.

### 4.3 Caching rule
Read-heavy getters (MOD page get-modify-set pairs, `GetStringID` per redraw)
are served from a **C3-local cache**, invalidated by the on-change pushes.
Never round-trip per encoder tick — one param set message per tick is fine
(~100 B ≈ 1 ms), a get+set round trip is not.

### 4.4 Bandwidth budget
Worst case (VU + snapshot @10 Hz + encoder ticks + occasional metadata) <
30 KB/s vs ~92 KB/s line rate. Boot metadata burst: params JSON ≈ 10–40 KB in
one shot — still < 0.5 s. Binary COBS upgrade path noted, not taken (JSON is
proven on-device, debuggable, host-testable).

### 4.5 Bench channel retained
UART0 `DebugUI` stays on the A1S (idle-prio, tiny) — it is the measurement
instrument (the 2026-10-26/27 harness runs over it). The link dispatcher is
the same code, so bench scripts keep working unchanged.

## 5. Firmware

### 5.1 C3 (`uimcu/` project, target esp32c3)
- **Ported as-is**: `UIMenu` core (ROOT/PANEL_IN states, panel bar),
  `menupages/*` (all 6), `Display` framebuffer + fonts (FONT_5X7; new SPI
  backend behind the same `Display::` API — pages call Display directly), the
  page-local logic (tap tempo, encoder acceleration, deferred/live config
  split).
- **Input rewrite**: E2 = panel switch (replaces ROOT encoder behavior),
  E1 = in-page navigation/value, BTN_A = OK (short) / MOD-map (long),
  BTN_B = BACK. Long-press-on-release semantics preserved from
  `UserInput.cpp:124–155`. Encoders: GPIO-ISR quadrature state table → same
  `InputEvent` queue model.
- **`DspProxy` interface**: one class replacing the direct DSP calls
  catalogued in §6; link-backed implementation + local cache (§4.3). Pages
  change only their call targets.
- **`UIMenuPageMidi`** becomes C3-local against its own `BtMidiReceiver`
  port (BLE scan/connect UI needs no link).
- **Phase 5**: `network.cpp` + `RestServer` port; REST handlers proxy DSP
  state over the link.

### 5.2 A1S
- `LinkEndpoint` task on UART1, **8KB stack** (the `setActivePlugin`
  overflow lesson — plugin delete+create runs in this task;
  `radio-free-dsp-test.md` §bugs). It parses the same STX/ETX JSON,
  dispatches into `DebugUI::handleCommand` + new handlers, and calls
  `Midi::Update()` (dormant-MIDI fix: currently nothing calls it —
  `Control.cpp:58` unreachable since commit `1a3bc77b`).
- Radio-free (`TBD_RADIO_FREE`) promoted from test branch to permanent.
- **Cutover (P6)**: remove UIMenu/UserInput/Display/menupages/framebuffer-shot
  from the A1S build; keep DebugUI (UI commands gated), SPManager, drivers.

## 6. DspProxy surface (audited calls crossing the boundary)

Condensed catalog (representative file:line; full audit in session log):

- **SPManager**: `GetCStrJSONSoundProcessors` (Home:60, SPIFFS read per call),
  `GetCStrJSONActivePluginParams` (Params:114), `GetCStrJSONGetPresets`
  (Params:188), `GetCStrJSONConfiguration`/`SetConfigurationFromJSON`
  (System:69/349, Mix:95/199 — commit holds processMutex + SPIFFS write),
  `GetStringID` (Params:98, **per redraw**), `IsPluginStereo` (Params:100),
  `SetSoundProcessorChannel` (Home:175–197, heavy),
  `SetChannelParamValue` (Params:275/332/371/374/574 — keys
  `current`/`cv`/`trig`), `ChannelSavePreset`/`ChannelLoadPreset`
  (Params:396/388), `SetMeteringEnabled` (Mix:82/284/371), `GetVUPeak` ×4
  (Mix:208–210, 4 Hz), `GetCPUOverload` (UIMenu:123, per redraw).
- **MarblesEngine** (MOD page is get-modify-set heavy): full get/set pairs in
  `adjustRow` Mod.cpp:164–235 + `formatRow` re-reads :237–390;
  `GetOutputSnapshot` (Mod:37/60, 4 Hz), `RequestReset`, learn
  (`StartLearn`/`StopLearn`/`IsLearning`/`GetLastLearnedSlot`,
  Params:357–570), dynamic CC slot get/set, `SaveConfig` (Mod:77).
- **Codec** (direct, outside SPManager): `SetInputGain`/`SetOutputLevels`
  live per encoder step (Mix:324/326), `MAX_OUTPUT_LEVEL`/`DEFAULT_OUTPUT_LEVEL`
  constants (Mix:49–61), `DebugReadReg` (DebugUI:344).
- **SDAudio**: Stop/StartPlayback/StartRecording/IsPlaying/IsRecording/
  GetTotalFrames/GetPositionFrames (Tape:97–161).
- **Favorites**: GetActiveFav/GetAllFavorites/ActivateFavorite (Home:80–206).
- **BtMidiReceiver/Midi flags**: MIDI page only (MidiPage:47–162) — becomes
  C3-local.
- **Filesystem**: `IsSDMounted` + direct `opendir/readdir` on `/sd`
  (Home:331–353, Tape:46–60) → SD proxy messages.
- **Debug-only**: `DebugStateJson`, `UIMenu::GetDebugState`, framebuffer
  shot (dies with the A1S UI at P6).

## 7. Repo structure

```
doc/ui-split-sdd.md        ← this document
protocol/                  ← IDF-free shared C++ headers: framing, message
                             names/ids, version; compiled by BOTH firmwares
                             AND tests/host (plain g++)
uimcu/                     ← new ESP-IDF project, target esp32c3
  main/                    ← app: link client, DspProxy, SPI display, input,
                             BtMidiReceiver port, (P5) network/REST
  ui/                      ← ported UIMenu core + menupages + Display + fonts
                             (single copy; A1S copies deleted at P6)
  sdkconfig.defaults       ← tracked; 4MB flash → single app slot + storage
main/                      ← A1S: + link endpoint; − UI sources at P6
components/drivers/        ← UART1 reconfig from 31250 MIDI to 921600 link
tests/host/                ← + test_link_protocol.cpp in run_tests.sh
```

Rationale: menu sources become C3-owned (single copy, no drift, no dual
build); protocol headers pure-C++ so host tests cover framing/version/
mutation logic without ESP-IDF. `ctag-bba/` (untracked snapshot) untouched.

## 8. Phasing (each phase = flashable, testable milestone)

- **P0** Merge `test/radio-free-dsp` → `perf/dsp-pipeline` (radio-free,
  stack fix, internal reverb become the baseline).
- **P1** `protocol/` headers + host tests (framing, version handshake,
  message round-trip via a loopback pair).
- **P2** C3 solo bring-up: SPI OLED + 2 encoders + ADC-ladder buttons +
  UIMenu running against a **stub DspProxy** (fake state) — prove panel
  hardware and the menu port with no link. Flash via C3 native USB.
- **P3** Link live: A1S `LinkEndpoint` + C3 `DspProxy`-over-link + boot
  metadata push. Navigation works end-to-end on real state; bench scripts
  still pass on UART0.
- **P4** MIDI: DIN in/out on C3 + `BtMidiReceiver` port → raw bytes over
  link → A1S `Midi::Update` (dormant-MIDI fix verified: Marbles clock, CC
  learn, note paths, running status). MIDI page goes C3-local.
- **P5** WiFi/REST on C3 (port `network.cpp`/`RestServer`; REST handlers
  proxy via link).
- **P6** A1S cutover: UI sources removed, radio-free permanent, re-run the
  validation harness (`radio-free-dsp-test.md` protocol) — expect internal
  DRAM ≈ 215KB+ free, core 0 = MIDI/link + IDLE + helpers, fallback audio
  task unchanged (core-1 path untouched by design).

## 9. Budgets (measured / estimated)

| Item | Value | Source |
|---|---|---|
| A1S internal DRAM free (radio-free, Elements loaded) | 192.3KB, largest 110.6KB | measured 2026-10-26 |
| A1S DRAM after UI removal | ≈ +25KB (stacks 4+2+8KB + RapidJSON churn) | estimate from task table |
| A1S core 0 after cutover | MIDI/link (prio 5) + IDLE + transient helpers | task audit |
| Link line rate | 921600 ≈ 92KB/s; worst-case traffic < 30KB/s | §4.4 |
| C3 flash (4MB) | app ≈ 1.2–1.6MB (menu + NimBLE + WiFi) → single slot + ~2MB storage | estimate |
| C3 RAM (400KB) | menu + pages ≈ 60–100KB heap incl. param JSON cache | estimate |
| Param latency across link | < 1 ms per set (~100 B @ 921600) | §4.3 |

## 10. Risks / mitigations

- **Zero spare GPIOs** — pin map is final; any future peripheral needs a
  documented trade (§2.1). Verify wiring against the physical board revision
  before P2.
- **Strapping pins on encoder E1** (GPIO8/9) — external pull-ups; never hold
  at reset; test power-on with encoders in random states during P2.
- **Plugin-load stack depth on the A1S link task** — 8KB minimum from day one
  (proven 4KB overflow, `radio-free-dsp-test.md` §bugs).
- **Dormant-MIDI fix changes behavior** — `Midi::Update()` was dead; waking
  it via the link may expose long-latent parser paths; test with real DIN
  gear in P4 (clock, CC learn, notes, running status).
- **Dual-flash choreography** — protocol version handshake refuses mismatched
  pairs; CI builds both images (add a uimcu job when the project lands).
- **JSON parse cost on C3** — RapidJSON on 160MHz RISC-V is fine at these
  sizes; the param JSON cache means parse-once-per-change, not per frame.

## 11. Validation harness

Reuse unchanged: STX/ETX JSON on A1S UART0 (`/debug/getAudioStats`,
`getUiState`, framebuffer shots), `/tmp/rf_*.py` scripts (recreate per
`radio-free-dsp-test.md` §artifacts if cleared). The C3 gets equivalent
debug commands (`getUiState`, framebuffer shot) over its own USB console so
host tests can assert on navigation state without reading pixels.
