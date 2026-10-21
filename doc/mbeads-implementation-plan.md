# Mbeads Implementation Plan

Companion to `doc/mbeads-port.md`. This file is the task-level execution plan.

## Status

| Task | Status | Commit |
|---|---|---|
| 1. Beads DSP core at 44.1 kHz / fixed blocks | **done** | submodule `8fc52b16`, parent `ef59b5cd` |
| 2. Mbeads plugin glue + UI descriptors | **done** | parent `6578f247` |
| 3. Validation, instrumentation, fixes | **done (software)** | pending commit |

Task 3 follow-ups that landed: default patch `density = 3072` / `seed = 1`;
`GranularProcessor::Reset(bool mono)` now clears only the active delay line
(768 KB instead of 1.5 MB); the duplicate load-time `Configure` was removed
(`Processor::Init` already configures Digital); `kBlockSize` is tied to
`beads::Processor::kMaxBlockSize` with a `static_assert`; `PERF_MEASURE("Mbeads")`
and `PERF_MEASURE("MbeadsGranular")` plus `PerfMonitor::EnableLogging(true,
5000, 1, 5)` are compiled in for IDF builds only.

Measured (Task 3): `sizeof(beads::Processor)` 29,496 B (Xtensa) / 30,736 B
(host); `ctag-tbd.bin` 0x3158b0 = 3,233,968 B; 436,048 B (12%) free in the
0x380000 app partition; host core test PASS. Hardware CPU/stack measurement is
**pending** user approval to flash (`doc/mbeads-port.md` §11 has the commands).

## Global Constraints

- Repo root: `/home/schrab/Projects/ctag-tbd`; branch: `feat/mbeads`.
- Target: CTAG TBD BBA, ESP32, **44,100 Hz**, **32-sample blocks**, audio task
  stack **4,096 bytes**, 112 KB sound-processor arena in PSRAM.
- Beads is vendored at `components/mutable/eurorack/beads`.
- Beads is a **stereo** processor. `isStereo = true`. The audio buffer is
  interleaved `L,R,L,R...` `float`.
- **Do not port** `beads.cc`, `ui.cc`, `drivers/`, `settings.cc`, `synth.cc`,
  wavetable players/voices, or `resources.cc` (the generated wavetable bank).
- **Do not allocate heap memory in `Process()`**. All buffers are allocated in
  `Init()`.
- **No VLAs in the audio path.** TBD passes 32 samples; fixed arrays only.
- `beads::Processor::Process` must receive **separate input and output
  frames**. In-place processing corrupts the dry signal.
- TBD trigger convention for this plugin: `data.trig[slot] == 1` means active.
- Do not touch unrelated work in the tree: `ctag-bba/`, `temp_a1s.txt`,
  `doc/*.pdf`, `components/moog/MoogLadders`, `simulator/Simple-Web-Server`,
  `simulator/rtaudio`, or nested submodules.
- When committing in the Beads submodule, stage **only** the Beads files you
  changed. Do not stage `avrlib`, `stmlib`, `avr_audio_bootloader`, etc.
- Do not flash hardware without explicit user approval.

---

## Task 1: Port the Beads DSP core to 44.1 kHz and fixed 32-sample blocks

### Files

- `components/mutable/eurorack/beads/dsp/processor.h`
- `components/mutable/eurorack/beads/dsp/processor.cc`
- `components/mutable/eurorack/beads/dsp/granular_processor.h`
- `components/mutable/eurorack/beads/resources_min.cc` (new)
- `components/mutable/CMakeLists.txt`
- `tests/host/mbeads_core_test.cpp` (new)
- `tests/host/run_mbeads_tests.sh` (new)

### Beads sample-rate table

Add `float host_sample_rate_` to `beads::Processor`. Change the signature to:

```cpp
void Init(uint8_t* buffers[], uint16_t* reverb_buffer, float host_sample_rate);
```

Use this table (enum order is Dirty, Clean, Vintage, Digital):

| Media | Engine rate | Ratio |
|---|---:|---:|
| `MEDIA_DIRTY_TAPE` | `host * 0.5` | 2 |
| `MEDIA_CLEAN_TAPE` | `host * 0.5` | 2 |
| `MEDIA_VINTAGE_DIGITAL` | `host * 0.5` | 2 |
| `MEDIA_DIGITAL` | `host` | 1 |

- `sample_rate()` returns the engine rate.
- `ratio()` returns `{2, 2, 2, 1}`.
- Remove/replace the `ENABLE_HARDWARE_24K` branches so the table is explicit.
- `Init` calls:
  - `processor_digital_.Init(host_sample_rate, buffers)`
  - `processor_vintage_digital_.Init(host_sample_rate * 0.5f, buffers)`
  - `processor_clean_tape_.Init(host_sample_rate * 0.5f, buffers)`
  - `processor_dirty_tape_.Init(host_sample_rate * 0.5f, buffers)`
  - `reverb_.Init(reverb_buffer, host_sample_rate)`
  - `wow_and_flutter_.Init(host_sample_rate)` (it processes host-rate output)
- `Configure`:
  - `pre_reverb_limiter_.Init(1.0f / (host_sample_rate_ * 4.0f), 0.9f)`
  - `output_limiter_.Init(1.0f / (host_sample_rate_ * 6.0f), 0.9f)`
  - `trigger_generator_.Init(host_sample_rate_)`
  - `delay_controller_.Init(host_sample_rate_, max_delay * (mono ? 2 : 1))`
  - `reverb_.Reset(host_sample_rate_)`
  - Correction after review: both controllers are stepped with host-rate blocks
    (32 samples). The half-rate engine downsamples their outputs afterward.
    Initializing them at the engine rate would make them run 2× fast on
    Vintage/Clean/Dirty. This supersedes the engine-rate wording above.
- `Process` uses `host_sample_rate_` for the delay/granular transition
  (`delay_mode_transition_`), not the engine rate.
- Templates:
  - digital: `RESOLUTION_16_BIT`, `kDelayBufferSizeBits / (16 * 2)`, `1`
  - vintage: `RESOLUTION_12_BIT`, `kDelayBufferSizeBits / (12 * 2)`, `2`
  - clean: `RESOLUTION_12_BIT`, `kDelayBufferSizeBits / (12 * 2)`, `2`
  - dirty: `RESOLUTION_8_BIT`, `kDelayBufferSizeBits / (8 * 2)`, `2`

### Fixed block buffers

In `beads::Processor`:

```cpp
static const size_t kMaxBlockSize = 32;
TriggerGenerator::Output triggers_buffer_[kMaxBlockSize];
DelayController::Output taps_buffer_[kMaxBlockSize];
bool gate_out_[kMaxBlockSize];
FloatFrame dry_signal_[kMaxBlockSize];
```

- Replace the three VLAs in `Processor::Process` with these members.
- `size` is always 32; guard by clamping to `kMaxBlockSize`.
- Move the dry-signal computation **before** calling the wet processor. For
  Digital/Clean copy `in`; for Vintage quantize to 12-bit; for Dirty use the
  existing `MuLaw2Lin(Lin2MuLaw(...))` path. This makes `Process` safe even if
  a caller passes `in == out`, although the glue still uses separate buffers.

In `beads::GranularProcessor::Process` (ratio != 1):

- Replace `d_freeze[d_size]`, `d_triggers[d_size]`, `d_taps[d_size]`,
  `d_in[d_size]`, `d_out[d_size]`, `downsampled[d_size]`, `upsampled[size]`
  with fixed arrays:
  - `d_*` arrays: `kMaxBlockSize / 2` (16)
  - `upsampled`: `kMaxBlockSize` (32)
- `d_size = size / ratio` is 16 for TBD. Do not exceed the fixed sizes.

### Resources

Create `components/mutable/eurorack/beads/resources_min.cc`:

- Include `beads/resources.h`.
- Define `const float lut_sine[]` exactly as in `resources.cc`.
- Define `lookup_table_table`, `lookup_table_i16_table`, `lookup_table_i8_table`.
- **Do not** define `wav_integrated_waves` or `wavetables_table`.
- Do not compile `beads/resources.cc`.

Expected saving: 49,920 `int16_t` = 99,840 bytes.

### CMake

Add Beads sources to `components/mutable/CMakeLists.txt`:

```
eurorack/beads/dsp/processor.cc
eurorack/beads/dsp/processor.h
eurorack/beads/dsp/granular_processor.h
eurorack/beads/dsp/trigger_generator.cc
eurorack/beads/dsp/trigger_generator.h
eurorack/beads/dsp/delay_controller.cc
eurorack/beads/dsp/delay_controller.h
eurorack/beads/dsp/delay_clock.cc
eurorack/beads/dsp/delay_clock.h
eurorack/beads/dsp/mu_law.cc
eurorack/beads/dsp/mu_law.h
eurorack/beads/resources_min.cc
eurorack/beads/resources.h
```

Do not add `beads/resources.cc` or `beads/dsp/synth.cc`.

### Host test

Create `tests/host/mbeads_core_test.cpp` and
`tests/host/run_mbeads_tests.sh`.

The test must:

1. Allocate three 262,144-byte delay banks and a 65,536-byte reverb buffer
   (host `malloc` is fine; this test does not run on the ESP32).
2. `beads::Processor processor; processor.Init(buffers, reverb, 44100.0f);`
3. `processor.Configure(beads::Processor::MEDIA_DIGITAL, false);`
4. Feed 2,000 blocks of 32 samples containing a 220 Hz sine (amplitude 0.25)
   through `Process`, with separate `FloatFrame in[32]` / `out[32]`.
5. Assert all output values are finite; after the first 100 blocks, assert the
   output RMS is > 0.
6. Switch to `MEDIA_VINTAGE_DIGITAL`, process another 500 blocks; assert the
   output is finite and non-silent.
7. Exercise `freeze` for 50 blocks and `seed` for 50 blocks; assert no
   non-finite output.
8. Print `sizeof(beads::Processor)` and a PASS/FAIL line.

Compile command (adjust only if a missing symbol is discovered):

```sh
g++ -std=c++17 -O2 -ffast-math \
  -Icomponents/mutable/eurorack \
  tests/host/mbeads_core_test.cpp \
  components/mutable/eurorack/beads/dsp/processor.cc \
  components/mutable/eurorack/beads/dsp/trigger_generator.cc \
  components/mutable/eurorack/beads/dsp/delay_controller.cc \
  components/mutable/eurorack/beads/dsp/delay_clock.cc \
  components/mutable/eurorack/beads/dsp/mu_law.cc \
  components/mutable/eurorack/beads/resources_min.cc \
  components/mutable/eurorack/stmlib/utils/random.cc \
  components/mutable/eurorack/stmlib/dsp/units.cc \
  components/mutable/eurorack/stmlib/dsp/atan.cc \
  -o /tmp/mbeads_core_test
```

The shell script must build and run this test, and exit non-zero on failure.

### Verification

- `bash tests/host/run_mbeads_tests.sh` passes.
- `grep -R "\[size\]\|\[d_size\]" components/mutable/eurorack/beads/dsp/processor.cc components/mutable/eurorack/beads/dsp/granular_processor.h`
  returns no VLA declarations in the audio path.
- `grep -n "resources.cc" components/mutable/CMakeLists.txt` shows only the
  Clouds/Plaits/Rings/Tides/Braids/Elements `resources.cc` entries, **not**
  `beads/resources.cc`.
- `sizeof(beads::Processor)` is printed (expected ~26–35 KB).

### Commit

1. In `components/mutable/eurorack`, stage only the Beads files and commit:
   `beads: port DSP core to 44.1 kHz and fixed 32-sample blocks`.
2. In the repo root, stage:
   - `components/mutable/eurorack` (gitlink)
   - `components/mutable/CMakeLists.txt`
   - `tests/host/mbeads_core_test.cpp`
   - `tests/host/run_mbeads_tests.sh`
   Commit: `Mbeads: add Beads DSP core and host validation`.

### Report

Write the full report to the path given in the dispatch. Return only:
status, commits (submodule + parent), one-line test summary, and concerns.

---

## Task 2: Add the `Mbeads` plugin glue and descriptors

### Files

- `components/ctagSoundProcessor/ctagSoundProcessorMbeads.hpp` (new)
- `components/ctagSoundProcessor/ctagSoundProcessorMbeads.cpp` (new)
- `spiffs_image/data/sp/mui-Mbeads.jsn` (new)
- `spiffs_image/data/sp/mp-Mbeads.jsn` (new)

### Class

```cpp
namespace CTAG { namespace SP {
class ctagSoundProcessorMbeads : public ctagSoundProcessor {
public:
    static void* operator new(std::size_t sz);
    static void operator delete(void* p);
    void Process(const ProcessData&) override;
    void Init(std::size_t blockSize, void* blockPtr) override;
    ~ctagSoundProcessorMbeads() override;
private:
    void knowYourself() override;
    beads::Processor processor;
    uint8_t* delay_buffer_ = nullptr;
    uint16_t* reverb_buffer_ = nullptr;
    uint8_t* delay_banks_[3] = {};
    beads::FloatFrame in_[32];
    beads::FloatFrame out_[32];
    bool freeze_[32];
    stmlib::GateFlags seed_flags_[32];
    int last_media_ = -1;
    int mute_blocks_ = 0;
    // atomics: time, time_mod, size, size_mod, shape, shape_mod, pitch,
    // pitch_mod, density, feedback, dry_wet, reverb, media, freeze, seed
    // cv atomics: cv_time, cv_size, cv_shape, cv_pitch, cv_density,
    // cv_feedback, cv_dry_wet, cv_reverb
    // trig atomics: trig_freeze, trig_seed
};
}}
```

Use the same PSRAM `operator new` pattern as
`ctagSoundProcessorElements.hpp`: `heap_caps_malloc(sz, MALLOC_CAP_SPIRAM)`,
fallback `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`, `assert(p)`.
`operator delete` calls `heap_caps_free(p)`.

### Init

1. `knowYourself();`
2. `model = std::make_unique<ctagSPDataModel>(id, isStereo);`
3. `LoadPreset(0);`
4. Allocate:
   - `delay_buffer_ = (uint8_t*) heap_caps_malloc(786432, MALLOC_CAP_SPIRAM);`
   - `reverb_buffer_ = (uint16_t*) heap_caps_malloc(65536, MALLOC_CAP_SPIRAM);`
   - `assert` both non-null; log `ESP_LOGE("Mbeads", ...)` before assert.
   - `delay_banks_[0] = delay_buffer_;`
   - `delay_banks_[1] = delay_buffer_ + 262144;`
   - `delay_banks_[2] = delay_buffer_ + 524288;`
5. `processor.Init(delay_banks_, reverb_buffer_, 44100.0f);`
6. `processor.Configure(beads::Processor::MEDIA_DIGITAL, false);`
7. `last_media_ = 0;`

Destructor frees both PSRAM allocations.

### Parameters

`knowYourself()` must set `isStereo = true; id = "Mbeads";` and register:

| id | pMapPar | pMapCv | pMapTrig |
|---|---|---|---|
| `time` | yes | yes | no |
| `time_mod` | yes | no | no |
| `size` | yes | yes | no |
| `size_mod` | yes | no | no |
| `shape` | yes | yes | no |
| `shape_mod` | yes | no | no |
| `pitch` | yes | yes | no |
| `pitch_mod` | yes | no | no |
| `density` | yes | yes | no |
| `feedback` | yes | yes | no |
| `dry_wet` | yes | yes | no |
| `reverb` | yes | yes | no |
| `media` | yes | no | no |
| `freeze` | yes | no | yes |
| `seed` | yes | no | yes |

All CV/trig atomics initialize to `-1`. Param atomics initialize in the
`Init` before `LoadPreset`, or rely on `LoadPreset(0)` — ensure no uninitialized
reads.

### Process mapping

```cpp
const float time = time_ / 4095.0f;
const float time_mod = clamp((time_mod_ - 2047.5f) / 2047.5f, -1.0f, 1.0f);
// same for size, shape
const float pitch = float(pitch_); // semitones, -24..24
const float pitch_mod = clamp((pitch_mod_ - 2047.5f) / 2047.5f, -1.0f, 1.0f);

beads::Parameters p;
p.time = {time, time_mod, cv_time != -1 ? data.cv[cv_time] * 1.6f : 0.0f,
          cv_time != -1};
p.size = {size, size_mod, cv_size != -1 ? data.cv[cv_size] * 1.6f : 0.0f,
          cv_size != -1};
p.shape = {shape, shape_mod, cv_shape != -1 ? data.cv[cv_shape] * 1.6f : 0.0f,
           cv_shape != -1};
p.pitch = {pitch, pitch_mod,
           cv_pitch != -1 ? data.cv[cv_pitch] * 60.0f : 0.0f,
           cv_pitch != -1};
p.monophonic_mode = false;
```

Use aggregate initialization only if it matches the struct field order
(`base`, `attenuverter`, `cv`, `has_cv`); otherwise assign fields explicitly.

```cpp
beads::ModulatedParameter density;
density.base = density_ / 4095.0f;
density.cv = cv_density != -1 ? data.cv[cv_density] : 0.0f;
```

Mix values: base / 4095, plus `data.cv[cv_*]` if assigned, clamped 0..1.

Media:
- `0` → `beads::Processor::MEDIA_DIGITAL`
- `1` → `beads::Processor::MEDIA_VINTAGE_DIGITAL`
- clamp other values to 0.
- If `last_media_ != media`, call `processor.Configure(media, false);`,
  set `last_media_ = media; mute_blocks_ = 2;`. `Configure` clears 768 KB;
  this is the accepted one-time mute. After configuring, zero `data.buf` for
  `mute_blocks_` blocks and return without processing.

Freeze:
```cpp
bool freeze = freeze_ != 0;
if (trig_freeze != -1) freeze = data.trig[trig_freeze] == 1;
for (size_t i = 0; i < 32; ++i) freeze_[i] = freeze;
```

Seed:
```cpp
beads::SeedButtonState seed_state = beads::SEED_BUTTON_STATE_OFF;
const stmlib::GateFlags* seed_input = nullptr;
if (trig_seed != -1) {
    const bool s = data.trig[trig_seed] == 1;
    for (size_t i = 0; i < 32; ++i) {
        seed_flags_[i] = s ? stmlib::GATE_FLAG_HIGH : 0;
    }
    seed_input = seed_flags_;
} else if (seed_ != 0) {
    seed_state = beads::SEED_BUTTON_STATE_ON;
}
```

Input/output conversion:
```cpp
for (size_t i = 0; i < 32; ++i) {
    in_[i].l = data.buf[2 * i];
    in_[i].r = data.buf[2 * i + 1];
}
beads::Processor::MixParameters mix = {feedback, dry_wet, reverb, false};
processor.Process(seed_state, seed_input, freeze_, density, p, mix,
                  in_, out_, 32);
for (size_t i = 0; i < 32; ++i) {
    data.buf[2 * i] = out_[i].l;
    data.buf[2 * i + 1] = out_[i].r;
}
```

### Descriptors

`mui-Mbeads.jsn`:

```json
{
  "id": "Mbeads",
  "isStereo": true,
  "name": "Mbeads",
  "hint": "Port of Mutable Instruments Beads",
  "params": [
    {"id":"time","name":"Time","type":"int","min":0,"max":4095,"step":1},
    {"id":"time_mod","name":"Time Mod","type":"int","min":0,"max":4095,"step":1},
    {"id":"size","name":"Size","type":"int","min":0,"max":4095,"step":1},
    {"id":"size_mod","name":"Size Mod","type":"int","min":0,"max":4095,"step":1},
    {"id":"shape","name":"Shape","type":"int","min":0,"max":4095,"step":1},
    {"id":"shape_mod","name":"Shape Mod","type":"int","min":0,"max":4095,"step":1},
    {"id":"pitch","name":"Pitch","type":"int","min":-24,"max":24,"step":1},
    {"id":"pitch_mod","name":"Pitch Mod","type":"int","min":0,"max":4095,"step":1},
    {"id":"density","name":"Density","type":"int","min":0,"max":4095,"step":1},
    {"id":"feedback","name":"Feedback","type":"int","min":0,"max":4095,"step":1},
    {"id":"dry_wet","name":"Dry / Wet","type":"int","min":0,"max":4095,"step":1},
    {"id":"reverb","name":"Reverb","type":"int","min":0,"max":4095,"step":1},
    {"id":"media","name":"Media","type":"int","min":0,"max":1,"step":1},
    {"id":"freeze","name":"Freeze","type":"bool"},
    {"id":"seed","name":"Seed","type":"bool"}
  ]
}
```

`mp-Mbeads.jsn`: one `Default` patch, `activePatch: 0`, with every parameter.
Use `cv: -1` for CV-capable params, `trig: -1` for bool params. Defaults:

```
time 2048, time_mod 2047
size 2048, size_mod 2047
shape 2048, shape_mod 2047
pitch 0, pitch_mod 2047
density 3072
feedback 0
dry_wet 2048
reverb 0
media 0
freeze 0, seed 1
```

Rationale for the two non-center defaults: Beads' density center is the
manual-tap zone and needs a seed gate; `density = 3072` + `seed = 1` makes the
granular engine audible on a fresh load. Both remain exposed.

### Verification

- `idf.py build` succeeds.
- `build/ctag-tbd.bin` is under the 3.5 MB app partition.
- `python3 -m json.tool spiffs_image/data/sp/mui-Mbeads.jsn > /dev/null`
- `python3 -m json.tool spiffs_image/data/sp/mp-Mbeads.jsn > /dev/null`
- `grep -n "Mbeads" build/gen_include/ctagSoundProcessors.hpp build/gen_include/ctagSoundProcessorFactory.hpp`
  shows the generated registration.
- No new malloc/free in `Process`.

### Commit

Commit the four files:
`Mbeads: add plugin glue and UI descriptors`.

### Report

Write the full report to the path given in the dispatch. Return only:
status, commit, one-line build/test summary, and concerns.

---

## Task 3: Build validation, instrumentation, and measurement report

### Files

- `components/ctagSoundProcessor/CMakeLists.txt`
- `components/ctagSoundProcessor/ctagSoundProcessorMbeads.cpp`
- `components/ctagSoundProcessor/ctagSoundProcessorMbeads.hpp`
- `spiffs_image/data/sp/mp-Mbeads.jsn`
- `components/mutable/eurorack/beads/dsp/granular_processor.h`
- `components/mutable/eurorack/beads/dsp/processor.cc`
- `doc/mbeads-port.md`
- `tests/host/mbeads_core_test.cpp`
- `tests/host/run_mbeads_tests.sh` (if not already done)

### Follow-up fixes from task reviews

1. **Audible default patch.** Change `density` from 2048 to 3072 and `seed`
   from 0 to 1 in both `spiffs_image/data/sp/mp-Mbeads.jsn` and the atomics in
   `ctagSoundProcessorMbeads.hpp`. Beads' density center is manual-tap-only and
   needs a seed gate; this makes the wet engine audible on a fresh load.
2. **One-line media clear.** `GranularProcessor::Reset()` currently clears both
   `mono_delay_line_` and `stereo_delay_line_` (1.5 MB). Add
   `Reset(bool mono)` that resets only the active line, and call
   `processor_*.Reset(mono)` from `Processor::Configure`. This restores the
   planned 768 KB one-time clear. Commit the submodule and the parent gitlink.
3. **Remove duplicate load-time Configure.** `Processor::Init` already calls
   `Configure(MEDIA_DIGITAL, false)`. Remove the extra explicit
   `processor.Configure(...)` from `ctagSoundProcessorMbeads::Init`, leaving
   `last_media_ = 0`.
4. **Tie block sizes together.** Add
   `static_assert(kBlockSize <= beads::Processor::kMaxBlockSize, ...)` in the
   glue and use `kBlockSize` consistently in the zero/silence paths instead of
   mixing `bufSz` and `kBlockSize`.
5. **Strengthen the host test if cheap.** Add an integration trigger-rate
   check through `beads::Processor` using `MixParameters.output_gate = true`
   (count right-channel gate transitions for Digital vs Vintage at the same
   density; they should be close). Set `reverb_amount = 0` and `dry_wet = 1`
   for the wet RMS checks, and require a meaningful floor rather than `>1e-6`.
   If this becomes fragile, keep the existing test and report why.

### Instrumentation

- Add `instrumentation` to the IDF `PRIV_REQUIRES` in
  `components/ctagSoundProcessor/CMakeLists.txt` (only for the non-simulator
  branch).
- In `ctagSoundProcessorMbeads.cpp`, include `perf_monitor.hpp` under
  `#ifndef TBD_SIM`.
- Wrap the body of `Process` after the parameter/media setup:
  ```cpp
  #ifndef TBD_SIM
  PERF_MEASURE("Mbeads");
  #endif
  ```
  and optionally add `PERF_MEASURE("MbeadsGranular")` around the
  `processor.Process` call only.
- In `Init`, enable logging once:
  ```cpp
  #ifndef TBD_SIM
  static bool perf_enabled = false;
  if (!perf_enabled) {
      CTAG::INSTRUMENTATION::PerfMonitor::EnableLogging(true, 5000, 1, 5);
      perf_enabled = true;
  }
  #endif
  ```
  (Core/priority values may be adjusted to match the existing instrumentation
  convention; do not create a task on a real-time core at a higher priority
  than audio.)

### Validation

1. `bash tests/host/run_mbeads_tests.sh`
2. `idf.py build`
3. Check `build/ctag-tbd.bin` size and free app-partition bytes.
4. If `/dev/ttyUSB0` exists, **do not flash**; report that hardware
   measurement is pending user approval. If no device, report the same.
5. Update `doc/mbeads-port.md` with:
   - measured `sizeof(beads::Processor)`;
   - firmware image size;
   - host test result;
   - the audible default patch (`density = 3072`, `seed = 1`);
   - the media switch cost after the one-line clear (768 KB, accepted mute);
   - a "Hardware measurement pending" note with the exact commands to run.
6. Record any compile warnings from Beads in the report.

### Commit

Commit:
`Mbeads: add instrumentation and validation report`.

### Report

Write the full report to the path given in the dispatch. Return only:
status, commit, build/test summary, image size, and concerns.

---

## Final Review

After Task 3, dispatch one whole-branch review covering the parent commits and
the Beads submodule commit. Include the submodule diff explicitly; the parent
diff only shows the gitlink.
