# BBA v2 (ESP32-S3) Porting SDD

**Status**: approved design, pre-implementation
**Branch**: `ctag-bba-v2-s3` (from `feat/migration-idf6`)
**Benchmarks & compute analysis**: `doc/esp32s3-porting-guide.md` (hardware-measured on ESP32-S3 @ 240 MHz, IDF 6.1)

## 1. Overview

Port the TBD DSP firmware from the ESP32-A1S (classic ESP32-D0WD + ES8388) to a custom
**BBA v2** board built around an **ESP32-S3-WROOM-1 (N16R8)** with a **WM8978** codec
(breakout board). The C3 panel (`uimcu/`) and the panel-link protocol are unchanged;
only the DSP board, its codec driver, and the pin map change.

### Goals
- New compile-time platform `TBD_PLATFORM_BBA2` beside the existing ones; legacy targets
  (A1S/BBA, MK2, …) keep compiling and behaving unchanged.
- WM8978 I2C-control driver + `codec_bba2` I2S facade implementing the existing
  `DRIVERS::Codec` interface (drop-in, zero changes to `SPManager` call sites).
- **Single-core synthesis**: the second-core Elements/Mbeads helper tasks, ping-pong
  buffers and the resolution step-down ladder are compiled out on BBA2 (`#if`-guarded,
  kept for legacy targets). Elements runs 52 modes @ 44.1 kHz on core 1
  (117,670 / 174,150 cycles = 67.6%, zero overruns — bench-proven).
- Load-time CPU-budget cap so no supported plugin chain can overrun single-core.
- 44.1 kHz locked on BBA2 (the 32 kHz Elements workaround was a classic-ESP32 CPU crutch).
- SD card without a card-detect pin (mount-state reporting; C3 hot-swap UI preserved).
- Full MIX-page parity: stereo line in/out, mic/line input switch, input gain,
  speaker output follows line out at the codec level.

### Non-goals
- `SvfBank` adoption in Elements/Rings resonators (guide §5) — deferred follow-up;
  the budget is met without it.
- ESP-ADF (rejected outright, guide §8).
- Panel (C3) firmware changes.
- Beads/Mbeads full-fidelity work — Beads is architecturally out of reach on S3
  (guide §7); Mbeads stays in the bundle as on `feat/migration-idf6` but runs
  single-core throttled on BBA2.

## 2. Hardware

| Item | Value |
|---|---|
| Module | ESP32-S3-WROOM-1-**N16R8** — 16 MB flash, 8 MB Octal PSRAM @ 80 MHz |
| Codec | WM8978 breakout, 2-wire (I2C) control mode |
| Codec lines | MCK, SCK (BCLK), FSA (WS), DAT (**DACDAT**, S3→codec playback), SDB (**ADCDAT**, codec→S3 capture) |
| Codec control | SDA, SCL — I2C, 7-bit address **0x1A** |
| Codec enable | **None wired** — power/mute handled via WM8978 registers (R1–R3 power, R5 mute) |
| SD | SDMMC 4-bit slot, **no card-detect pin** |
| Panel link | UART1 → C3 @ 921600 8N1 |
| Debug/console | USB Serial/JTAG (native USB, GPIO19/20) |

### Notes
- MCLK is generated **by the S3 (I2S master) at 256·fs** = 11.2896 MHz @ 44.1 kHz.
  The breakout's "~12.288 MHz" label is nominal (256·fs @ 48k family); WM8978 accepts
  MCLK directly in slave mode with R6/R7 clocking set for the 44.1 kHz family.
- WM8978 over I2C is **write-only** — no register readback. The driver keeps a shadow
  register file (the legacy in-repo SPI reference `codec.cpp` WM8978 table documents
  the map); "identify" = successful ACK on the R0 reset write.
- Strapping pins **0, 3, 45, 46** unused; OPI-PSRAM-internal **35/36/37** unused;
  19/20 reserved for USB D±.

## 3. Pin map (BBA v2)

| Function | GPIO |
|---|---|
| WM8978 MCLK | 2 |
| WM8978 BCLK (SCK) | 41 |
| WM8978 WS (FSA) | 42 |
| WM8978 DACDAT (S3→codec) | 40 |
| WM8978 ADCDAT (codec→S3) | 39 |
| WM8978 I2C SDA / SCL | 8 / 9 |
| SDMMC CLK / CMD / D0 / D1 / D2 / D3 | 6 / 7 / 10 / 11 / 12 / 13 |
| Link UART1 TX / RX (→ C3) | 17 / 18 |
| Spare | 1, 14, 15, 16, 21, 33, 34, 38, 47, 48 |

Pins are compile-time constants in the BBA2 driver files (same pattern as
`codec_bba.cpp` / `es8388.cpp`), not Kconfig knobs.

## 4. Platform & build integration

- `main/Kconfig.projbuild`: add `TBD_PLATFORM_BBA2` to `choice TBD_PLATFORM`
  (default remains BBA). `N_CVS`/`N_TRIGS` = same 102/43 slot space as BBA
  (root `CMakeLists.txt`).
- `sdkconfig.defaults.bba2`: `CONFIG_IDF_TARGET="esp32s3"`, 16 MB flash QIO 80M,
  Octal PSRAM (`SPIRAM_MODE_OCT`, `SPIRAM_TYPE_AUTO`, `SPIRAM_USE_MALLOC`) 80 MHz,
  240 MHz CPU, **`CONFIG_FREERTOS_HZ=1000`** (decided: kills the documented
  `pdMS_TO_TICKS(n<10)` no-op bug class; timing behavior of all tasks audited in the
  implementation plan), USB Serial/JTAG console, PERF optimization, custom partition
  table. Partition layout unchanged: dual 3.5 MB OTA + 3 MB storage + 5 MB sample ROM
  at 0xB00000 (Freakwaves wavetables preserved).
- `components/drivers/CMakeLists.txt`: BBA2 arm selecting `wm8978.cpp` + `codec_bba2.cpp`.
- Compiler flags per guide §4: `-O3 -ffast-math -funroll-loops` on the mutable
  component (BBA2; legacy targets unchanged).
- Submodule: advance `components/mutable/eurorack` to `85942a64` (adds
  `stmlib/dsp/svf_bank.h`; the bench branch's only reusable artifact besides the guide).
- `doc/esp32s3-porting-guide.md` copied onto this branch.

## 5. Codec subsystem

### 5.1 WM8978 driver (`components/drivers/wm8978.{hpp,cpp}`)

I2C class in the `es8388.cpp` mold (own lazy `i2c_master_bus`, port -1, bounded
retries since control calls can originate off the audio task):

- I2C addr 0x1A, 400 kHz, pins 8/9.
- 58-register shadow file mirroring the legacy `codec.cpp` WM8978 table; all writes
  update shadow + bus; no reads (write-only interface).
- `init()`: R0 reset → power up (R1: buffer/VMID/BIAS, R2/3: ADC/DAC/OUT mixers) →
  R4 audio interface (I2S, 16-bit, slave) → R5 (mute off, comp off) → R6/R7 clocking
  (MCLK÷1, 44.1 kHz family) → DAC/ADC path enables, input mixer (LIP/LIN mic w/ boost
  vs L2/R2 line) → output mixers: OUT1 line out (stereo), OUT4 mono speaker sourced
  from the same mix so the speaker follows line out.
- API mirrors `es8388`: `init()`, `identify()` (ACK on reset write), `inputSelect(mic|line)`,
  `setInputGain()`, `setOutputVolume(0..N)` (OUT1 digital/analog per register semantics),
  speaker enable/volume via OUT4 mixer gain, `softMute(bool)` (R5), `readAllReg()`
  (shadow dump for `/debug`).
- I2S pins and formats are NOT the driver's business (facade owns I2S).

### 5.2 Facade (`components/drivers/codec_bba2.{hpp,cpp}`)

Static `DRIVERS::Codec` implementing the exact `codec_bba.hpp` interface
(`InitCodec`, `ReadBuffer`, `WriteBuffer`, `HighPassEnable/Disable`, `RecalibDCOffset`,
`SetOutputLevels`, `SetSampleRate/GetSampleRate`, `SetInputSource`, `SetOutputSource`,
`SetMixerMode`, `SetAnalogBypass`, `SetInputGain`, debug reg access):

- New `driver/i2s_std.h` API, S3, I2S controller auto-allocated, **master**.
- 16-bit stereo, Philips; `dma_desc_num = 4`, `dma_frame_num = 32`
  (the "grow DMA via `dma_frame_num`, never `dma_desc_num`" rule carries over);
  MCLK multiple 256; pins per §3.
- `ReadBuffer`: int16→float `3.0518509476E-5f`, **zero-fill on error/100 ms timeout**
  (A1S semantics kept — audio never blocks forever on a dead bus).
- `WriteBuffer`: float→int16 clamp ±32767, 100 ms timeout.
- `SetSampleRate`: API kept for interface parity; on BBA2 any request ≠ 44100 is
  logged and ignored (§6.2). `SetInputSource` → `wm8978::inputSelect`; input gain →
  mic boost/PGA; DC-offset recalib path is a no-op wrapper (WM8978 has no ES8388-style
  DC register — the existing software DC-cut in `audio_task` remains the real filter).
- No amp/shutdown GPIO exists: output muting is R5 soft-mute + volume regs.

## 6. Audio path

### 6.1 Single-core synthesis (helper elimination)

On BBA2 (compile-time `#if !CONFIG_TBD_PLATFORM_BBA2` guards keep the legacy path):

- `ctagSoundProcessorElements`: audio task calls `part.Process(patch, state, …)`
  **directly on core 1**. `StartHelper`, the 2-slot ping-pong `HelperBlock` machinery,
  `helper_shared_`, and the V9 step-down ladder are not compiled. Resolution is
  fixed at 52 modes (user-settable via the existing `/debug/setElementsMode`).
- `ctagSoundProcessorMbeads`: same treatment (its helper task, core-0 wait loops —
  including the `vTaskDelay(pdMS_TO_TICKS(n<10))` no-op waits flagged in AGENTS.md —
  disappear on BBA2). Runs single-core throttled (guide §7: 4–8 grains).
- Debug stream: `elementsHelper*` fields report as absent/zero on BBA2; single-core
  cycle counts come from the existing `lastAudioTaskCycles` instrumentation.
- Concurrency model after the change: one real-time task (core 1, prio 23,
  `processMutex` try-take mute-on-contention) + core 0 system tasks
  (`link_ep`, `sd_audio`, both prio 1 — the AGENTS.md starvation rule applies verbatim).

### 6.2 44.1 kHz locked

The Elements→32 kHz `requestedSampleRate` path in `SetSoundProcessorChannel` /
`audio_task` is `#if`-guarded off on BBA2. Sample rate is a build-time constant
44100; no I2S clock reconfig on plugin load (no reconfig glitch). Legacy targets
keep dynamic switching.

### 6.3 Load-time CPU budget cap

- Static per-plugin cost table (cycles @ 44.1 kHz, from bench §6.1): Plaits 45k,
  Rings 55k, Clouds 62k, Elements-52M 120k, Elements-32M 93k; Mbeads provisional **100,000** (unmeasured, conservative — refine on hardware
  during bring-up).
- In `SPManager::SetSoundProcessorChannel` (BBA2 only): sum the estimated cost of
  the requested load plus what's already loaded on the other channel; if the total
  exceeds **90% of 174,150 cycles** and the request is Elements, load it at
  32 modes instead of 52 (worst known chain Elements-32M → Clouds = 86.7%).
  The cap is deterministic and logged; no other de-rating.

## 7. SD card

- SDMMC 4-bit via GPIO matrix (pins §3), `gpio_cd = GPIO_NUM_NC` (unchanged pattern).
- No CD GPIO exists: `IsSDPresent()` returns the **mount state** instead of polling
  a pin. First boot mounts once; failure = "no card".
- C3 hot-swap UI (UNMOUNT/MOUNT item) preserved as-is: UNMOUNT unmounts + marks
  absent; MOUNT re-attempts and reports the result. No io-caps change needed.

## 8. Panel link, MIDI, console

- `LinkEndpoint`: unchanged protocol/logic; UART1 pins → 17/18 @ 921600. The
  dormant-midiuart install/delete dance follows the BBA pattern (all MIDI lives on
  the C3; GPIO19/20 are USB, never touched).
- Console: USB Serial/JTAG (GPIO19/20) — log capture is the native USB CDC port,
  not `stty /dev/ttyUSB0`. The A1S "console open resets the chip" behavior does
  not apply to S3 USB Serial/JTAG; `screenshot.py`-style benching works the same
  over the C3 console (unchanged).
- `/debug/*` dispatcher, 25 Hz stream, chunked param pushes: carried over untouched.

## 9. Memory & optimization

- ~330 KB contiguous internal DRAM post-boot: the 114,688 B `ctagSPAllocator`
  arena fits in internal SRAM with wide margin (no PSRAM fallback needed);
  large delay/grain buffers (Clouds, reverb) in Octal PSRAM — bench-proven cost
  +1.1% cycles.
- No change to `SP_FIXED_MEM_ALLOC_SZ` (compat with all plugin assumptions).
- `-O3 -ffast-math -funroll-loops` on the mutable component only (BBA2).

## 10. Verification & bring-up

### Host / build (no hardware)
1. `tests/host/run_tests.sh` green.
2. BBA2 build: `IDF_TARGET=esp32s3` with `sdkconfig.defaults.bba2` → `idf.py build` clean.
3. Legacy regression: `a1s` build still compiles; binary boots on the A1S bench.

### Hardware bring-up checklist (after wiring, one console session each stage)
1. Flash via USB-Serial/JTAG; boot log clean, Octal PSRAM detected.
2. WM8978 probe: I2C ACK on reset write; `/debug` reg dump sane.
3. I2S tone test: line out signal at 44.1 kHz, input loopback, VU moves.
4. Mic path: inputSelect(mic), gain sweep, no clipping surprises.
5. Speaker out follows line out.
6. SD mount + WAV browse/play/record round-trip through the C3 panel.
7. Panel link handshake (C3 sees boot metadata, params chunked push OK).
8. Elements 52-mode soak: `loopHist` stable at 7, zero overruns, no helper tasks
   present in `ps`-style task list.

## 11. Decisions log

| Decision | Choice | Rationale |
|---|---|---|
| Helper tasks | `#if`-guarded out on BBA2, kept for legacy | Bench: 52 modes fits single-core at 67.6%; user decision (b) |
| Overflow case (Elements 52M → Clouds) | load-time budget cap, Elements→32 modes | Deterministic; worst chain 86.7% |
| Sample rate | 44.1 kHz locked on BBA2 | 32 kHz was a classic-ESP32 CPU crutch |
| Bench branch merge | skipped; copy guide + bump eurorack submodule | user decision (c) |
| FreeRTOS tick | 1000 Hz | user decision (d); retires the `<10 ms` vTaskDelay bug class |
| Mbeads | stays in bundle, single-core throttled on BBA2 | bench-branch removal not inherited; revisit after bring-up |
| Codec enable pin | none; software power registers | board has no SDB-shutdown line (SDB = ADCDAT) |
