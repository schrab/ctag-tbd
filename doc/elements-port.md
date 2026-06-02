# Elements Sound Processor Port

## Overview

Port of Mutable Instruments Elements (modal resonator / physical modeling synthesizer) to the TBD platform. Elements is a polyphonic physical modeling voice with exciters (bow, blow, strike) feeding a modal resonator with up to 52 SVF modes plus a Dattorro reverb.

## CPU Budget

- Sample rate: 44100 Hz
- Buffer size: 32 samples per audio task iteration
- Block period: 32 / 44100 = 0.726 ms
- CPU frequency: 240 MHz
- Budget per block: 240e6 × 0.726e-3 = 174,150 cycles
- Budget per sample: 174,150 / 32 ≈ 5,442 cycles

## Performance Measurements

Measured with `components/instrumentation/` lock-free cycle monitor (PERF_MEASURE RAII macros):

### 12 Resonator Modes (final configuration)

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

## SoftLimit Clipping Issue

Elements' `SoftLimit(x) = x*(27+x²)/(27+9x²)` is **not a hard limiter**. For large x it becomes linear with gain ~1/9, allowing outputs of ±11+ from coherent SVF mode summation. TBD's `Codec::WriteBuffer` multiplies by 32767 and clamps to int16, producing hard digital clipping.

**Fixed by:** 0.125 master gain in `ctagSoundProcessorElements.cpp:180-181` (`out[i] * 0.125f`).

However, the bow exciter can still produce signals that exceed ±1.0 even with 0.125 gain, causing low-end crackling/distortion. Further gain reduction or a proper hard limiter may be needed.

## Key Files

- `components/mutable/eurorack/elements/dsp/voice.cc:73` — `set_resolution(12)` (was 52)
- `components/mutable/eurorack/elements/dsp/part.cc` — Part::Process (voice loop, softclip, reverb)
- `components/ctagSoundProcessor/ctagSoundProcessorElements.cpp` — glue code, 0.125 master gain, reverb buffer alloc
- `components/instrumentation/` — lock-free cycle monitor (available for future profiling)

## Reverb

- 32768-sample Dattorro topology (4 AP diffusers + 2× 2AP+1Delay loop)
- Fixed cost ~35k cycles per 32-sample block regardless of buffer size
- Allocated from PSRAM via `heap_caps_malloc(32768 * sizeof(uint16_t), MALLOC_CAP_SPIRAM)`

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
