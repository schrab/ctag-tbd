# Elements Sound Processor Port

## Overview

Port of Mutable Instruments Elements (modal resonator / physical modeling synthesizer) to the TBD platform. Elements is a polyphonic physical modeling voice with exciters (bow, blow, strike) feeding a modal resonator with up to 52 SVF modes plus a Dattorro reverb.

The resonator was reduced to 12 modes to fit the single-core audio-block budget. A second-core offload (`Part::Process` on a core-0 helper task, one block, one-block latency) was then implemented to try to restore 52 modes. Hardware measurement showed the helper is itself over budget at every attempted mode count, so production automatically falls back to the proven single-core 12-mode path (see "Second-core helper").

## CPU Budget

- Sample rate: 44100 Hz
- Buffer size: 32 samples per audio task iteration
- Block period: 32 / 44100 = 0.726 ms
- CPU frequency: 240 MHz
- Budget per block: 240e6 × 0.726e-3 = 174,150 cycles
- Budget per sample: 174,150 / 32 ≈ 5,442 cycles

## Performance Measurements

Measured with `components/instrumentation/` lock-free cycle monitor (PERF_MEASURE RAII macros):

### 12 Resonator Modes (shipped fallback)

Measured on the single-core path (the configuration production settles on):

| Section | Avg Cycles | Avg µs | % of total |
|---------|-----------|--------|-----------|
| Voice (4 voices × 12 modes) | 107,000 | 446 | 60% |
| SoftLimit (per-sample) | 4,700 | 20 | 2.6% |
| Reverb (32768-sample Dattorro) | 35,000 | 146 | 20% |
| Elements DSP total | ~147,000 | ~612 | 82% |
| System overhead (ModEngine, I2C I/O, DC cut, noise gate, SD audio, etc.) | ~36,000 | ~151 | 20% |
| **Full pipeline total** | **~183,000** | **~763** | **100%** |
| **Budget** | **174,150** | **726** | |

### Resonator Mode Scaling

| Modes | Voice avg cycles | Full pipeline avg |
|-------|-----------------|-------------------|
| 52 | ~260k (est.) | ~336k (est.) |
| 32 | 145,115 | ~207k |
| 24 | 130,201 | ~194k |
| 16 | 113,542 | ~188k |
| **12** | **106,797** | **~183k** |

### Max Spikes

Voice max spikes reach 1600-2400 µs (384k-576k cycles) during note-on events (voice initialization, cache misses). These briefly exceed budget and trigger the CPU overload indicator.

## Second-core helper (52-mode attempt)

Implemented in `ctagSoundProcessorElements.cpp/.hpp` (SPEC.md V5–V11):

- Helper task `"Elements"`, core 0, priority `configMAX_PRIORITIES - 2`, stack 4096, one task for the plugin's lifetime.
- Sequence-checked double buffer (2 slots) carrying `PerformanceState`, a `Patch` snapshot, `blow_in[32]`, `strike_in[32]`, `main[32]`, `aux[32]`, resonator model, easter-egg flag.
- Core 1 snapshots the block inputs and patch at block start, then notifies; the helper renders one complete 32-sample block; core 1 mixes the previous block's output × `0.125` one block later. No mutex: core 1 never touches `part` while the helper runs, and a missed block is silence plus a miss counter.
- Firmware-only code (FreeRTOS task, `esp_cpu_*`) is behind `#ifndef TBD_SIM`; the simulator takes the single-core 12-mode path.
- Runtime resolution: `Voice::set_resolution()` / `Part::set_resolution()` (submodule change); `Voice` defaults to 52. `Init()` selects 52 only when the helper starts.

Fallback ladder (one-way per plugin instance):

1. `StartHelper()` success → 52 modes; failure → single-core 12 modes.
2. Runtime: 8 consecutive struggling blocks (missed deadline or over-budget render) step down one rung: 52 → 32 → 24 → 16 → 12.
3. At the 12-mode floor, 8 further struggling blocks trigger the literal V9 handoff: the helper is asked to stop, core 1 keeps outputting silence until the helper has exited, then renders inline at 12 modes on the audio core.

Measured on hardware (ESP32-D0WD-V3, per 32-sample block, budget 174,150 cycles):

| Metric | Value |
|---|---|
| Helper at 52 modes | ~225k cycles/block typical; ~445k–555k worst-case → over budget |
| Step-down | ladder engages on every boot; 8-block hysteresis per mode |
| Settled state | single-core 12 modes ~25–30 ms after boot, ~40 missed blocks while settling |
| Failures | none observed (no crash, watchdog or silent wedge) |

Conclusion: on this hardware the helper is over budget at every attempted mode count, so 52 modes is not viable and the automatic fallback is what ships. The offload is a safety net, not a performance win; the audible result equals the previous single-core 12-mode build.

Debug surface:

- `/debug/getAudioStats` — `elementsHelperCycles`, `elementsHelperMaxCycles`, `elementsHelperOverload`, `elementsHelperStackHighWater` (`-1` = helper retired by the V9 handoff), `elementsHelperResolution`, `elementsHelperMisses`, `elementsHelperModeOverride`.
- `/debug/setElementsMode{"value":12..56}` — pins a mode count for measurement (`<= 0` restores the auto ladder). Debug-only; lost on reboot.

## SoftLimit Clipping Issue

Elements' `SoftLimit(x) = x*(27+x²)/(27+9x²)` is **not a hard limiter**. For large x it becomes linear with gain ~1/9, allowing outputs of ±11+ from coherent SVF mode summation. TBD's `Codec::WriteBuffer` multiplies by 32767 and clamps to int16, producing hard digital clipping.

**Mitigated by:** the `0.125` master gain applied on the audio core (`ctagSoundProcessorElements.cpp`, both the helper mix and the single-core path). Retained unchanged by the second-core work (out of scope per SPEC.md).

Caveat: `0.125` is ~18 dB below stock Elements (upstream outputs `SoftLimit(main/aux)` directly), and SoftLimit is level-dependent, so the extra gain also moves its drive point. The bow exciter can still exceed ±1.0 even with the gain, causing low-end crackling/distortion; a proper hard limiter is an open issue.

## Known Issues (open, not fixed in the second-core work)

- **Pitch is +5.53 semitones sharp.** `elements/dsp/dsp.h` was bumped to `kSampleRate = 44100`, but the MIDI-to-frequency LUTs (`elements/resources.cc`, generated by `elements/resources/lookup_tables.py` for `SAMPLE_RATE = 32000`) are byte-identical to upstream, so `440 Hz → 606.375 Hz` (44.1/32). Regenerating the LUTs is the fix direction.
- **`strength` and `play` are dead parameters.** `perfState.strength` is hardcoded `0.5f`; neither param has a `pMapPar`/`pMapCv` mapping, so UI/preset values do nothing. At `strength = 1.0` the host harness measures ~11 dB more output than the hardcoded `0.5`.
- **CV/trig slot indices are uninitialized.** The `cv_*`/`trig_*` atomics default to indeterminate (~0), and the wrapper treats any value `!= -1` as an active mapping, so parameters without a preset index follow `data.cv[0]`/`data.trig[0]`. In particular `resonator_model` (intended default 1/STRING) follows `data.cv[0]` (model 0 at rest) and `easter_egg` follows `data.trig[0]`.
- Full details and harness evidence: `docs/elements-port-review-findings.md`.

## Key Files

- `components/mutable/eurorack/elements/dsp/part.h` / `voice.h` — `set_resolution()`/`resolution()`; `Voice` default resolution 52
- `components/mutable/eurorack/elements/dsp/part.cc` — Part::Process (voice loop, softclip, reverb)
- `components/ctagSoundProcessor/ctagSoundProcessorElements.cpp` / `.hpp` — glue code, helper offload, fallback ladder, 0.125 master gain, debug hooks
- `main/DebugUI.cpp` — `/debug/getAudioStats` `elementsHelper*` fields, `/debug/setElementsMode`
- `components/instrumentation/` — lock-free cycle monitor (available for future profiling)
- `docs/elements-port-review.md` / `docs/elements-port-review-findings.md` — review brief and verified findings

## Reverb

- 32768-sample Dattorro topology (4 AP diffusers + 2× 2AP+1Delay loop)
- Fixed cost ~35k cycles per 32-sample block regardless of buffer size
- Allocated from PSRAM via `heap_caps_malloc(32768 * sizeof(uint16_t), MALLOC_CAP_SPIRAM)`
- Stays inside `Part::Process`: when the helper runs, the reverb runs on core 0 with it (SPEC.md V11); there is no split reverb path.

## Instrumentation Component

Located at `components/instrumentation/`, provides:

- `PERF_MEASURE("name")` — RAII scope measurement using `esp_cpu_get_cycle_count()`
- `PerfMonitor::EnableLogging(true, interval_ms, core, priority)` — separate logging task
- Lock-free atomic accumulation (~10 cycles overhead when enabled, ~3 when disabled)
- Max 32 named sections

Usage:
```cpp
#include "perf_monitor.hpp"
// Add PRIV_REQUIRES instrumentation to component's CMakeLists.txt
// Call PerfMonitor::EnableLogging(true, 5000) once
// Wrap code sections with PERF_MEASURE("section_name") { ... }
```
