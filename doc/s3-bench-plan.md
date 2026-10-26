# Plan: ESP32-S3 Bench Port — Elements Performance Measurement

Execution plan for a fresh session. Goal: run the existing firmware on the
**ESP32-S3-WROOM N16R8** (octal PSRAM, 16MB flash) with a **PCM5102A** DAC to
measure Elements' performance on S3 scalar — the agreed step 2 before any
SIMD work. Elements as synth: **no audio input** (stub), **no UI** (all
control via the existing UART0 STX/ETX debug JSON). Marbles stays in the
audio path (part of the real pipeline, negligible cost).

Ground truth to compare against: `doc/radio-free-dsp-test.md` (2026-10-26/27
A/B on the ESP32-A1S).

## 0. Branch

`port/s3-bench` off `test/radio-free-dsp` (9d8ef982 — inherits radio-free
default, 8KB `debug_ui` stack fix, internal-reverb preference). Bench-only,
do not merge.

## 1. Platform plumbing

- `main/Kconfig.projbuild` (choice at :2–31): add
  `config TBD_PLATFORM_S3BENCH` bool "S3 bench (PCM5102A, no UI)" to the
  TBD_PLATFORM choice.
- Root `CMakeLists.txt:20–46`: S3BENCH case → same `-DN_CVS=102
  -DN_TRIGS=43` as BBA, TBD_HW "S3BENCH".
- `main/CMakeLists.txt:4–7`: compile `Midi.cpp` on S3BENCH too — it carries
  `MarblesEngine::Init()` via `Midi::Init()`, which is required because
  `MarblesEngine::Process()` runs unconditionally at `SPManager.cpp:109`.
  BtMidiReceiver stays out automatically (CONFIG_BT_ENABLED unset).
- `main.cpp:58–89`: add an S3BENCH branch — skip Display splash / InitSD /
  UIMenu / UserInput entirely; run `StartSoundProcessor()` then
  `DebugUI::Start()`.
- `main/DebugUI.cpp`: gate the UI-dependent commands
  (`getDisplayFramebuffer`, `getUiState`, `getMixState` — they reference
  UIMenu/Display) behind the platform; keep `getAudioStats` /
  `setElementsMode` / `setActivePlugin` / `setPluginParam` /
  `getPluginParams` / `getConfig` / `setConfigValue` — the measurement
  surface. Check how `main/Display.cpp` compiles on non-BBA platforms today
  (V1/STR pattern) and follow it so the S3BENCH build links with no live UI.
- Keep the `Control::Init` → `Midi::Init` path from the non-STR branch of
  `StartSoundProcessor` (SPManager.cpp:385+) — it initializes Marbles +
  SDAudio (512KB SPIRAM ring, never touches SD).

## 2. Codec stub — new `components/drivers/codec_s3.cpp` (+ `codec_s3.hpp`)

Satisfies the `Codec::` facade (codec.hpp:39–85): `InitCodec`, `ReadBuffer`,
`WriteBuffer`, `SetOutputLevels` + the two level constants; everything else
rides the existing `#else` inline no-ops in codec.hpp:69–85.

- Use the **same new channel API** as codec_bba.cpp:50–100
  (`driver/i2s_std.h`): **TX channel only**
  (`i2s_new_channel(&cfg, &tx, NULL)`), std Philips **16-bit stereo @44100**
  (matches the ES8388 WriteBuffer format), `dma_desc_num=4`,
  `dma_frame_num=32`, `mclk = I2S_GPIO_UNUSED` (PCM5102A runs its internal
  PLL from BCK in 3-wire mode).
- **Wiring (confirmed with user):** BCK=GPIO41, WS=GPIO42, DIN=GPIO40 — all
  safe on N16R8 (clear of flash/octal-PSRAM 26–37, USB 19/20, UART0 console
  43/44, strapping 0/45/46). DAC module: SCK→GND, XSMT→3V3, FMT→GND.
  Three defines in `codec_s3.hpp` to edit if the board differs.
- `ReadBuffer` = zero-fill, **non-blocking**. Audio-loop pacing then comes
  from `WriteBuffer`'s TX backpressure (portMAX_DELAY) — verify loop
  stability via `audioCycles` before trusting any helper numbers.
- `WriteBuffer` = identical float→int16 clamp ±32767 conversion as
  codec_bba.cpp:223–239.
- `components/drivers/CMakeLists.txt:6–15`: S3BENCH branch registering
  `fs.cpp codec_s3.cpp` only (no ssd1306/encoder/midiuart/led),
  `REQUIRES driver esp-dsp`.

## 3. sdkconfig + build/flash

- New `sdkconfig.defaults.s3bench`: `CONFIG_IDF_TARGET_ESP32S3`, CPU 240MHz,
  `CONFIG_SPIRAM_MODE_OCTAL` (+ try `SPIRAM_SPEED_120`), flash 16MB,
  **no BT/WiFi** (radio-free), INT_WDT default (no coex), UART0 console
  (one-line switch to `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` if the board is
  USB-only).
- Reuse `partitions_example.csv` (16MB layout fits N16R8).
- Build with an explicit fresh config:
  `idf.py -DSDKCONFIG=sdkconfig.s3bench -DSDKCONFIG_DEFAULTS=sdkconfig.defaults.s3bench set-target esp32s3 && idf.py build`
  (target switch needs a clean build dir). Verify the esp32/A1S build still
  builds clean afterward. Flash at 460800 via the board's port (probe
  `/dev/ttyACM0|ttyUSB0`).

## 4. Measurement protocol (the point of the exercise)

Reuse the 2026-10-26 harness — STX/ETX JSON @115200 on the console port.
Scripts `/tmp/rf_sustain.py`, `/tmp/rf_ab.py` (if /tmp was cleared, recreate
them per `radio-free-dsp-test.md` §artifacts — exact protocol documented
there).

1. Boot: capture the heap log line (S3 internal DRAM expected ≈ +100KB vs
   A1S) + PERF enable.
2. **Pinned 52**: `/debug/setElementsMode{"value":52}` **before**
   `/debug/setActivePlugin{"id":"Elements","ch":0}`; 60s @2Hz
   `elementsHelperCycles`.
3. Pinned 32 and 24 (sustainability curve).
4. Auto ladder, 45s hold check (expect collapse to 12 unless scalar S3
   surprises).
5. 12-mode fallback: `audioCycles` + PERF `Elements` avg.

**A1S reference numbers (same protocol, 2026-10-26/27):**

| Metric | A1S value |
|---|---|
| Helper @52 median | 238.9k cycles (PSRAM reverb) / 224.4k (internal reverb) |
| Helper @32 / @24 median | 202.3k / 186.7k (radio-free) |
| Auto ladder | always collapses to 12 |
| Fallback `audioCycles` median | 160.6k (92% of 174,150 budget) |
| PERF @12-mode fallback | 109.7–123.6k cycles |
| Internal DRAM free | 192.3k (largest block 110.6k) |

**Decision gates:** (a) if the 52-mode helper median ≤ ~174k → S3 scalar
alone unlocks full Elements (unexpected but decisive); (b) realistically:
record the S3 scalar baseline that the future SIMD plan will be judged
against (SVF bank 2.5–3× per `radio-free-dsp-test.md` §implications), and
note the octal-120MHz PSRAM effect on reverb/JSON paths.

## 5. Deliverables

- Branch `port/s3-bench`: platform files + codec stub + sdkconfig; host
  tests still pass (`tests/host/run_tests.sh` untouched); both targets build
  clean.
- `doc/s3-bench-results.md`: measurement table in the radio-free doc's
  format + wiring used + deviations.
- Board left flashed and running the auto ladder (production behavior);
  comparison verdict reported in the session summary.

## 6. Risks

- Display/UIMenu link surface on S3BENCH → mitigated by DebugUI command
  gating + following the V1/STR compile pattern.
- ReadBuffer pacing change (no RX channel) → verify via `audioCycles`
  stability first.
- Board-specific DAC wiring → 3-define pin map; XSMT tied high.
- `set-target` wipes sdkconfig → always use the explicit `-DSDKCONFIG*`
  flags + clean build dir.
