# Meaningful Optimizations & Fixes from `servandobarreiro/ctag-tbd` (TBD-16 / `p4_main`)

This document catalogs every meaningful performance optimization, memory improvement, and correctness fix found in `https://github.com/servandobarreiro/ctag-tbd` (the **dadamachines TBD-16** firmware forked from `ctag-fh-kiel/ctag-tbd` `p4_main`), evaluated against our **ESP32-S3 BBA2 (`ctag-bba-v2-s3`)** codebase.

---

## Executive Priority Matrix

| # | Optimization / Fix | Category | Source Commit | Impact on ESP32-S3 (`ctag-bba-v2-s3`) | Effort |
|---|---|---|---|---|---|
| **1** | **Diode Ladder Filter Coefficient Caching (`ctagDiodeLadderFilter` & `5`)** | DSP / CPU | `60523ec6` | **High** (~40+ float ops & 9 float divs eliminated *per sample*) | Low (Drop-in) |
| **2** | **`PsramStringMap` for `pMapPar` / `pMapCv` / `pMapTrig`** | Internal SRAM | `b69cb179` | **High** (Frees 5–30 KB internal SRAM; zero DSP runtime cost) | Low (Drop-in) |
| **3** | **`TBD03` Reciprocal & Per-Sample Loop Hoisting** | DSP / CPU | `c1285111` | **Medium-High** (Eliminates 32 float divs/block + 8 block divs + switch) | Low (Drop-in) |
| **4** | **Software DC-Blocker Removal in `SPManager::audio_task`** | DSP / Audio | `679ef85b` | **Medium** (WM8978 hardware ADC HPF is already active; saves 64 ops/block + phase shift) | Trivial |
| **5** | **Static Aligned Codec Buffer + Loop Unrolling (`ReadBuffer`/`WriteBuffer`)** | Codec / I2S | `05652885` | **Medium** (Removes stack VLA, enables branchless float-domain clamping) | Low |
| **6** | **Pre-`SoftClip` Full-Block Peak Metering in `audio_task`** | Metering / UI | `b69cb179` | **Medium** (Fixes 1st-sample-only peak bug; detects active clipping) | Trivial |
| **7** | **`loadPresetInternal()` Missing-Key Guard (`HasParam`)** | Correctness | `b69cb179` | **High** (Prevents missing preset keys from clobbering C++ defaults to `0`) | Trivial |
| **8** | **Rompler & ChordSynth Buffer/Array Out-of-Bounds Clamps** | Safety / Crash | `b69cb179` | **High** (Prevents memory corruption at high pitch ratios or chord note counts) | Trivial |
| **9** | **POSIX `open`/`read`/`write` + Pre-Allocation + 16 KB SDMMC Bounce** | SD Card I/O | `0782669d`, `75d9d479` | **Medium** (Bypasses `stdio` buffering; reduces FAT fragmentation) | Low |
| **10** | **Lock-Free Cycle Profiler (`PerfMonitor`)** | Diagnostics | `ad3f8b1e` | **Low-Medium** (Per-subblock atomic cycle stats without audio-thread `printf`) | Optional |

---

## 1. DSP & Audio Loop Optimizations

### 1.1 Diode Ladder Filters: Hoisting Per-Sample Coefficients (`60523ec6`)
* **Files**:
  * `components/ctagSoundProcessor/filters/ctagDiodeLadderFilter.cpp` (`.hpp`)
  * `components/ctagSoundProcessor/filters/ctagDiodeLadderFilter5.cpp` (`.hpp`)
* **Problem in Our Tree**:
  `Process(float in)` currently recomputes `G[0..3]`, `GAMMA`, `SG[0..3]`, `alpha`, `beta[0..3]`, `gamma[0..2]`, `delta[0..2]`, and `epsilon[0..2]` **on every sample** — even though `g` and `gp1` only change once per 32-sample block in `SetCutoff()`. Worse, it executes **9 floating-point divisions per sample** (`G[3..0]`, `beta[0..3]`, and `1.0f / (1.0f + kval_ * GAMMA)`) and computes `(z[2] + fb4 * delta[2])` and `(z[1] + fb3 * delta[1])` twice.
* **What They Changed**:
  1. Moved all `G`, `SG`, `alpha`, `alpha_half`, `beta`, `gamma`, `delta`, `epsilon`, `kval_GAMMA`, and `reciprocal_kval_GAMMA = 1.0f / (1.0f + kval_GAMMA)` calculations into `updateCoefficients()`, called only from `Init()`, `SetCutoff()`, and `SetResonance()`.
  2. Replaced the per-sample division `(in - kval_ * SIGMA) / (1.0f + kval_ * GAMMA)` with multiplication by `reciprocal_kval_GAMMA`.
  3. Reused intermediate feedback terms `temp0`, `temp1`, `temp2` for `fbo1..3` instead of recomputing them.
  4. Replaced `(0.5f * xin - z[k]) * alpha` with `(xin - z[k] - z[k]) * alpha_half` using precalculated `alpha_half = 0.5f * alpha`.
* **Savings**: Eliminates **9 float divisions and ~35 float multiplications/additions per sample** (288 float divisions saved per 32-sample block).

---

### 1.2 `TBD03` Per-Block & Per-Sample Loop Hoisting (`c1285111`)
* **File**: `components/ctagSoundProcessor/ctagSoundProcessorTBD03.cpp`
* **What They Changed**:
  1. **Hoisted per-sample division out of the 32-sample loop**: Replaced `float eg = pre_eg_val + (egvalVCA - pre_eg_val) / (float) bufSz * i;` inside the `for (int i = 0; i < bufSz; i++)` loop with a precomputed `const float eg_step = (egvalVCA - pre_eg_val) / (float) bufSz;` and `eg += eg_step;` per sample (1 division per block instead of 32).
  2. **Replaced `/ 4095.f` with `* INV_4095`** (`constexpr float INV_4095 = 1.0f / 4095.f`) across 8 parameter normalizations per block.
  3. **Direct filter pointer table**: Replaced `switch (ftype)` with `ctagFilterBase* filters[5] = {...}; filter = filters[ftype];`.

---

### 1.3 Removing Redundant Software DC Blocker in `SPManager::audio_task` (`679ef85b`)
* **Files**:
  * `main/SPManager.cpp`
  * `components/drivers/codec_bba2.cpp`
* **Problem in Our Tree**:
  In `main/SPManager.cpp`, our `audio_task` runs a 32-sample stereo software IIR DC blocker (`0.995f` pole) on every input block. However, on BBA2 our WM8978 codec **already runs a hardware digital ADC high-pass filter** (`R14 = 0x108`, bit 8 `HPFEN = 1` in `components/drivers/codec_bba2.cpp`, which implements a 3.7 Hz DC-blocking filter @ 44.1 kHz).
* **Benefit**:
  Removing the redundant software DC blocker in `SPManager.cpp` (or gating it out when the codec hardware HPF is active) saves a 32-iteration stereo loop on every block and avoids double high-pass phase shift on CV/audio inputs.

---

### 1.4 Pre-`SoftClip` Full-Block Peak Detection (`b69cb179`)
* **File**: `main/SPManager.cpp`
* **Problem in Our Tree**:
  Our `SPManager::audio_task` runs `stmlib::SoftClip` in one loop and then computes `peakOut` from **only the very first sample (`fbuf[0] + fbuf[1]`) after clipping** — missing peaks on samples `1..31`, cancelling out anti-phase stereo signals (`L = -R`), and never detecting when `SoftClip` is actively saturating (`|x| > 1.0f`).
* **What They Changed**:
  Fused peak detection into the `SoftClip` loop **before** `SoftClip(lin_l)` is applied:
  ```cpp
  float peakRaw = 0.f;
  for (uint32_t i = 0; i < BUF_SZ; i++) {
      float lin_l = fbuf[i * 2];
      float lin_r = fbuf[i * 2 + 1];
      float a = fabsf(lin_l) > fabsf(lin_r) ? fabsf(lin_l) : fabsf(lin_r);
      if (a > peakRaw) peakRaw = a;
      if (ch0_outputSoftClip) fbuf[i * 2]     = stmlib::SoftClip(lin_l);
      if (ch1_outputSoftClip) fbuf[i * 2 + 1] = stmlib::SoftClip(lin_r);
  }
  peakOut = 0.9f * peakOut + 0.1f * peakRaw;
  ```

---

## 2. Memory & Heap Architecture Optimizations

### 2.1 `PsramStringMap` for Parameter Maps (`b69cb179`)
* **Files**:
  * `components/ctagSoundProcessor/helpers/PsramAllocator.hpp`
  * `components/ctagSoundProcessor/ctagSoundProcessor.hpp`
* **Problem in Our Tree**:
  Every `ctagSoundProcessor` instance holds three `std::map<string, function<void(const int)>>` members (`pMapPar`, `pMapCv`, `pMapTrig`). Because `CONFIG_SPIRAM_USE_CAPS_ALLOC=y` is used, default `std::allocator` allocates all red-black tree nodes (`~64–80 bytes` each including `std::function` state) from **internal DRAM**. For complex plugins (`Subbotnik`, `Retroactor`, `Elements`, `Marbles`), `knowYourself()` allocates hundreds of nodes in internal SRAM, even though those maps are **never accessed from `Process()`** (only when a parameter/CV mapping is edited or a preset loads).
* **What They Changed**:
  Created a header-only STL allocator `PsramAllocator<T>` that routes `std::map` node allocations to `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` with a fallback to `std::malloc`:
  ```cpp
  HELPERS::PsramStringMap<function<void(const int)>> pMapPar;
  HELPERS::PsramStringMap<function<void(const int)>> pMapCv;
  HELPERS::PsramStringMap<function<void(const int)>> pMapTrig;
  ```
* **Benefit on ESP32-S3**:
  Saves **5–25 KB of internal DRAM** depending on the active plugins and eliminates internal heap fragmentation during plugin switches, with zero impact on `audio_task`.

---

### 2.2 RapidJSON `CrtAllocator` Null-Safety & PSRAM Reservation (`3a55e994`)
* **Files**:
  * `components/rapidjson/rapidjson/allocators.h`
  * `components/ctagSoundProcessor/helpers/ctagSampleRom.cpp`
* **What They Found**:
  When `ctagSampleRom::BufferInSPIRAM()` grabbed `largest_free_block - 128 KB` of PSRAM, RapidJSON's `CrtAllocator::Malloc` (which allocates from `MALLOC_CAP_SPIRAM`) ran out of PSRAM during large JSON preset/param serializations and returned `NULL`, causing a crash. They increased the PSRAM safety reserve in `BufferInSPIRAM` from `128 KB` to `512 KB`.

---

## 3. Codec & I2S Driver Optimizations

### 3.1 Static Aligned Conversion Buffer & Float-Domain Clamping (`05652885`)
* **File**: `components/drivers/codec_bba2.cpp`
* **What They Changed**:
  1. Replaced stack-allocated VLAs (`int16_t rx_buf[sz * 2]` / `tx_buf[sz * 2]`) with a persistent cache-aligned `static DRAM_ATTR __attribute__((aligned(16)))` buffer.
  2. Clamped samples in the **float domain** (`[-1.0f, 1.0f]`) using hardware FPU min/max (`fmaxf`/`fminf`, which compile to single-cycle branchless `max.s`/`min.s` instructions on ESP32-S3) *before* multiplying by full-scale integer range and casting.
  * **Bug in Our Current `WriteBuffer`**: In `components/drivers/codec_bba2.cpp`, we currently do:
    ```cpp
    int32_t v0 = static_cast<int32_t>(32767.f * buf[i * 2]);
    v0 = MAX(v0, -32768);
    v0 = MIN(v0, 32767);
    ```
    If `ch0_outputSoftClip` is disabled and a plugin outputs `NaN` or `> 65536.f`, `static_cast<int32_t>` is undefined behavior on Xtensa FPU, whereas clamping in float (`fmaxf(fminf(x, 1.0f), -1.0f)`) before multiplying by `32767.f` is branchless and immune to integer overflow.

---

## 4. Bugfixes & Safety Improvements Worth Porting

### 4.1 `loadPresetInternal()` Missing-Key Guard (`b69cb179`)
* **Files**:
  * `components/ctagSoundProcessor/ctagSoundProcessor.hpp`
  * `components/ctagSoundProcessor/ctagSPDataModel.cpp` (`.hpp`)
* **Problem in Our Tree**:
  `loadPresetInternal()` iterates over every entry in `pMapPar` and unconditionally calls:
  ```cpp
  setParamValueInternal(kv.first, "current", model->GetParamValue(kv.first, "current"));
  ```
  When a preset JSON does not contain a parameter ID (e.g., a newly added parameter or an aliased parameter), `model->GetParamValue()` returns `0`, silently clobbering the C++ default value initialized in the plugin's header/constructor.
* **Fix**:
  Add `bool ctagSPDataModel::HasParam(const string &id)` and guard the loop with `if (!model->HasParam(kv.first)) continue;`.

---

### 4.2 Out-of-Bounds Clamps in `RomplerVoice`, `RomplerVoiceMinimal`, and `ChordSynth` (`b69cb179`)
* **Files**:
  * `components/ctagSoundProcessor/synthesis/RomplerVoice.cpp`
  * `components/ctagSoundProcessor/synthesis/RomplerVoiceMinimal.cpp`
  * `components/ctagSoundProcessor/polypad/ChordSynth.cpp`
* **Three Bugs Fixed**:
  1. **`RomplerVoice` / `RomplerVoiceMinimal` buffer overflow**: At extreme pitch shifts (+5 octaves + pitch modulation), `readBufferLength = static_cast<uint32_t>(phaseIncrement * float(size) + readBufferPhase)` can exceed `readBufferMaxSize`, corrupting adjacent arena memory. Fixed by clamping `readBufferLength` to `readBufferMaxSize - 4` (for cubic/Hermite interpolation in `RomplerVoice`) and `readBufferMaxSize - 2` (for linear interpolation in `RomplerVoiceMinimal`).
  2. **`RomplerVoiceMinimal` missing envelope loop**: Added `ad.SetLoop(params.loop);` so the AD envelope loops when sample looping is enabled.
  3. **`ChordSynth::calcInversion` stack/array overflow**: Previously hardcoded `int8_t inv[4]` and `memcpy(ht_steps, inv, 4)` without bounds-checking `i + 2 + inversion` against `[0, kChordNumNotes - 1]`. Fixed by sizing `inv[8]`, clamping `idx` to `[0, kChordNumNotes - 1]`, and copying `nnotes` bytes.

---

### 4.3 `mifx::Reverb` Delay-Line Out-of-Bounds Warning (`b69cb179`)
* **File**: `components/ctagSoundProcessor/mifx/reverb.h`
* **Note**: Documents exact delay-buffer bounds (`ap1` = 150 samples, max read `10 + 80 = 90`; `del1` = 4501 samples, max read `4460 + 40 = 4500`; `del2` = 6312 samples, max read `6261 + 50 = 6311`) after an earlier attempt to increase LFO excursion caused out-of-bounds reads and harsh feedback explosions.

---

## 5. SD Card & Filesystem Optimizations

### 5.1 POSIX Unbuffered I/O (`open`/`read`/`write`) & FAT File Pre-Allocation (`0782669d`)
* **Files**:
  * `components/ctagSoundProcessor/helpers/ctagSampleRom.cpp`
  * `components/drivers/fs.cpp`
* **What They Changed**:
  1. **Direct POSIX `open`/`read`/`write` instead of `fopen`/`fread`/`fwrite`**: Bypasses Newlib's `FILE` stream lock and small internal `BUFSIZ` copy buffer when streaming large WAV chunks between SD card and PSRAM/DRAM.
  2. **FAT File Pre-Allocation**: When creating a file of known or target size on FATFS, seeking ahead (`lseek`) and writing 1 byte before rewinding reserves contiguous clusters upfront, reducing FAT table walks during streaming writes.
  * **Relevance to BBA2 (`SDAudio.cpp`)**: Our tape deck (`SDAudio.cpp`) currently uses `fopen`/`fread`/`fwrite`. Switching the `SDAudio` worker to POSIX `open`/`read`/`write` (or calling `setvbuf(f, nullptr, _IONBF, 0)` since `SDAudio` already reads/writes in 4 KB chunks) avoids redundant Newlib internal buffer copies on Core 0.

### 5.2 Batched 32-Sector (16 KB) SDMMC DMA Wrapper (`75d9d479`, `092169c3`)
* **File**: `components/drivers/custom_sdmmc_cmd.cpp`
* **What They Changed**:
  Wraps `sdmmc_read_sectors` and `sdmmc_write_sectors` (`-Wl,--wrap=...`) so that when reading/writing into a buffer that is not directly DMA-aligned, it transfers up to **32 sectors (16 KB) per SDMMC command** using a persistent DMA bounce buffer instead of ESP-IDF's default 1-sector (512 B) bounce loop.

---

## 6. What Our Codebase (`ctag-bba-v2-s3`) Already Does Better

For completeness, several bottlenecks that `servandobarreiro/ctag-tbd` either worked around with P4 hardware or has not fixed yet are **already solved** in our branch:
1. **`rings::Part` Union Optimization (`TBDings` Fits in Internal SRAM)**:
   * On their P4 build, `ctagSoundProcessorTBDings.cpp` allocates its 64 KB `reverb_buffer` in PSRAM and keeps `resonator_`, `string_`, and `fm_voice_` separate (`113,944 B`). In our tree, sharing those mutually exclusive arrays reduced `TBDings` to `105,708 B` so the entire processor + `reverb_buffer` fit inside our `110,592 B` internal SRAM arena.
2. **Dirty-Cached Generation Tracking on `ctagDataModelBase`**:
   * Avoids re-serializing RapidJSON models on steady-state status checks.
3. **Incremental Parameter Sync (`link/pushParamValue`)**:
   * Avoids full-state JSON regeneration on single parameter edits.
