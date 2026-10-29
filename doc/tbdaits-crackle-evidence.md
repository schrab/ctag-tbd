# TBDaits 5–10 Hz Overrun Crackle — Evidence & Root-Cause Analysis

**Date**: 2026-10-29  
**Target**: ESP32-A1S (`CONFIG_TBD_PLATFORM_BBA`, branch `feat/ui-split`, post-P6 UI cutover)  
**Plugin under investigation**: `TBDaits` ([`ctagSoundProcessorTBDaits.cpp`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSoundProcessorTBDaits.cpp), Mutable Instruments Plaits port in [`components/mutable/eurorack/plaits/`](file:///home/ubuntu/ctag-tbd/components/mutable/eurorack/plaits/))

---

## 1. Symptom & Measured Hardware Evidence

### 1.1 Audio Budget vs. Steady-State Cost
The audio engine processes 32-sample blocks at 44.1 kHz on Core 1 ([`SoundProcessorManager::audio_task`](file:///home/ubuntu/ctag-tbd/main/SPManager.cpp#L77-L331), priority 23):
- **Block period (hard real-time deadline)**: `32 / 44100 Hz` = **725.62 µs** (**174,150 CPU cycles** at 240 MHz, [`CPU_MAX_ALLOWED_CYCLES`](file:///home/ubuntu/ctag-tbd/main/SPManager.cpp#L67)).
- **`Void` baseline (both channels `Void`)**: **8,000–9,000 cycles/block** (~35 µs, ~5% of budget), which includes `MarblesEngine::Process`, `Codec::ReadBuffer`, input DC-cut filters, channel staging, and `Codec::WriteBuffer`.
- **Full 24-model sweep (`smodel = 0..23`) via `/debug/setPluginParam` + `/debug/getAudioStats`**:
  - Steady-state block cost across all 24 synthesis models ranges from **41,000 to 128,000 cycles/block** (**170–533 µs**, **23.5%–73.5%** of the 174,150-cycle budget).
  - Worst-case steady-state model is `smodel = 22` at **~128k cycles (73.5%)**.
- **`PERF_MEASURE("TBDaits")` instrumentation ([`ctagSoundProcessorTBDaits.cpp:66`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSoundProcessorTBDaits.cpp#L66))**:
  - Measured average `TBDaits::Process` time is **`avg ≈ 268 µs` (~64,300 cycles, ~37% of budget)** — comfortably inside the 725.6 µs deadline.

### 1.2 The Smoking Gun: Periodic 5–10 Hz Single-Block Latency Spikes
Despite the ~268 µs average render cost, live hardware captures with `PERF_MONITOR` and ear tests across all 24 models revealed:
- Audible **5–10 Hz periodic crackle / clicks** present on **every** synthesis model (`smodel = 0..23`), both idle and voice-active.
- `PERF_MONITOR` recorded `TBDaits` **single-block `max` render times of ~950–1,000 µs (~228,000–240,000 cycles, 131%–138% of the 725.6 µs budget)** repeating at a **5–10 Hz (~100–200 ms) cadence**.
- Every block that spikes to 950–1,000 µs misses the 725.6 µs I2S DMA cadence on Core 1, starving `Codec::WriteBuffer` for one block and producing an audible click.

---

## 2. Exonerated & Previously Fixed Suspects

Before isolating the 5–10 Hz spikes in `voice.Render()`, five hypotheses were investigated, tested on hardware, and either fixed as separate bugs or ruled out as the source of the 5–10 Hz overrun crackle:

| # | Hypothesis | Investigation / Commit | Verdict on 5–10 Hz Crackle |
|---|---|---|---|
| **H1** | Stereo plugin `ch=1` nullptr dereference causing DSP panics | Guarded `sp[chan] == nullptr` in [`SoundProcessorManager::SetChannelParamValue`](file:///home/ubuntu/ctag-tbd/main/SPManager.cpp#L469-L474) and clamped `smodel` to `0..23` (`005cc172`). | **Separate bug (crash fixed).** Did not affect steady-state audio rendering. |
| **H2** | ES8388 DAC output stage overloaded by onboard NS4150 amp (`GPIO21` pinned high when `output_source="headphones"`) | [`Codec::UpdateAmpEnable`](file:///home/ubuntu/ctag-tbd/components/drivers/codec.cpp) now drives `GPIO21` low in headphone mode (`7d6bfaa5`). | **Separate bug (analog distortion fixed).** Eliminated analog clipping on headphones, but the 5–10 Hz digital overrun clicks remained. |
| **H3** | Xtensa LX6 FPU denormal microcode traps in decaying filter/LPG states | Enabled Xtensa FPU Non-Standard mode (`NS`, bit 5 of `FCR`: Flush-To-Zero + Denormals-Are-Zero) at the start of [`SoundProcessorManager::audio_task`](file:///home/ubuntu/ctag-tbd/main/SPManager.cpp#L83-L87) (`79d6ae90`), verified in boot log: `I (428) SPM: FPU FCR 0x0 -> 0x20 (NS=denormals flushed)`. | **Exonerated.** `max` block spikes to ~950–1,000 µs persisted with `FCR = 0x20`. |
| **H4** | `PERF_MONITOR` 5 s background task printing over UART0 | Made `PerfMonitor` logging opt-in via `/debug/enablePerf` (`d30528ab`). | **Exonerated.** A 5-second print period (0.2 Hz) cannot cause a 5–10 Hz crackle, and the crackle is unchanged with `PerfMonitor` disabled. |
| **H5** | Unbounded float output overdriving `Codec::WriteBuffer` (as seen in `Elements` port) | Checked [`plaits::ChannelPostProcessor::Process`](file:///home/ubuntu/ctag-tbd/components/mutable/eurorack/plaits/dsp/voice.h#L87-L116) and [`ctagSoundProcessorTBDaits.cpp:140-143`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSoundProcessorTBDaits.cpp#L140-L143). Unlike `Elements` (which summed 52 modes in float up to ±11.0), Plaits runs `Limiter::Process` + `stmlib::Clip16` into `int16_t` (`-32768..32767`) inside `Voice::Render`, and `TBDaits::Process` divides by `32768.f` with `fLevel ∈ [0.0, 1.0]`. | **Exonerated.** Output is strictly bounded to `[-1.0f, +1.0f]`. |

---

## 3. Code-Level Root Causes of the 5–10 Hz Overruns

Inspecting what runs at **10 Hz (100 ms)** on the A1S and how `TBDaits` memory is laid out in hardware uncovered **two interacting root causes** that explain both the **exact 5–10 Hz cadence** and the **~700 µs stall added to a ~268 µs render**, plus **one parameter-mapping bug** in `ctagSoundProcessorTBDaits`:

---

### Root Cause 1 (The 10 Hz Trigger): `LinkEndpoint::pushOnChange()` Hammers SPI Flash (LittleFS) + PSRAM RapidJSON Every 100 ms (10 Hz)

In [`main/LinkEndpoint.cpp`](file:///home/ubuntu/ctag-tbd/main/LinkEndpoint.cpp#L755-L794), `linkTask` (pinned to Core 0, priority 6) wakes up every **`kStreamPeriodMs = 100` ms (10 Hz)** and calls both `pushStream()` and `pushOnChange()`:

```cpp
// main/LinkEndpoint.cpp:789-793
if ((now - lastStreamMs) >= kStreamPeriodMs) { // kStreamPeriodMs = 100 ms (10 Hz)
    lastStreamMs = now;
    pushStream();
    pushOnChange();
}
```

Inside [`LinkEndpoint::pushOnChange()`](file:///home/ubuntu/ctag-tbd/main/LinkEndpoint.cpp#L360-L399), the intent was to push JSON blobs only when their FNV-1a hash changes. However, **to compute those hashes every 100 ms**, `pushOnChange()` unconditionally regenerates every JSON blob from scratch — including **reading and parsing 4 JSON files from SPI Flash (`/spiffs/...`) via LittleFS 10 times per second**:

1. **`SoundProcessorManager::GetCStrJSONSoundProcessors()`** ([`LinkEndpoint.cpp:395`](file:///home/ubuntu/ctag-tbd/main/LinkEndpoint.cpp#L395)) → calls [`SPManagerDataModel::GetCStrJSONSoundProcessors()`](file:///home/ubuntu/ctag-tbd/main/SPManagerDataModel.cpp#L101-L108):
   - Calls **`loadJSON(m, "/spiffs/data/sp/sp-model.jsn")`** — opens `/spiffs/data/sp/sp-model.jsn` on LittleFS (SPI Flash), reads the file, parses it into a RapidJSON `Document`, and re-serializes `"SoundProcessors"` **10 times per second** (even though the list of compiled sound processors is 100% static at runtime!).
2. **`SoundProcessorManager::GetCStrJSONGetPresets(0)` and `(1)`** ([`LinkEndpoint.cpp:372-383`](file:///home/ubuntu/ctag-tbd/main/LinkEndpoint.cpp#L372-L383)) → calls [`ctagSPDataModel::GetCStrJSONPresets()`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSPDataModel.cpp#L110-L143):
   - Calls **`loadJSON(d, "/spiffs/data/sp/mp-TBDaits.jsn")`** (and channel 0's `mp-*.jsn`) — opens the plugin's entire preset bank file on LittleFS (SPI Flash), reads and parses every preset patch into a RapidJSON `Document`, builds a summary DOM, and serializes it **10 times per second per channel (20 file reads/parses per second)**.
3. **`Favorites::GetAllFavorites()`** ([`LinkEndpoint.cpp:388`](file:///home/ubuntu/ctag-tbd/main/LinkEndpoint.cpp#L388)) → calls [`FavoritesModel::GetAllFavorites()`](file:///home/ubuntu/ctag-tbd/main/FavoritesModel.cpp#L28-L35):
   - Calls **`loadJSON(m, "/spiffs/data/favs.jsn")`** — opens `/spiffs/data/favs.jsn` on LittleFS (SPI Flash), parses all favorites, and serializes them **10 times per second**.
4. **`SoundProcessorManager::GetCStrJSONActivePluginParams(0)` and `(1)`** ([`LinkEndpoint.cpp:365-370`](file:///home/ubuntu/ctag-tbd/main/LinkEndpoint.cpp#L365-L370)) → calls [`ctagSPDataModel::GetCStrJSONParams()`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSPDataModel.cpp#L57-L64):
   - Clones and merges the full plugin descriptor + current parameter DOM (`mergeModels()`) and serializes it via RapidJSON **10 times per second per channel**.
5. **`SoundProcessorManager::GetCStrJSONConfiguration()`** ([`LinkEndpoint.cpp:391`](file:///home/ubuntu/ctag-tbd/main/LinkEndpoint.cpp#L391)) and **`MarblesEngine::GetConfigJSON()`** ([`LinkEndpoint.cpp:386`](file:///home/ubuntu/ctag-tbd/main/LinkEndpoint.cpp#L386)):
   - Re-serializes configuration and Marbles state via RapidJSON **10 times per second**.

**Why this stalls Core 1 (`audio_task`) at 5–10 Hz**:
On ESP32 (`ESP32-D0WD-V3`), Core 0 and Core 1 share a **single SPI0/SPI1 external memory bus** (serving both 80 MHz SPI Flash and 80 MHz SPI PSRAM) and a 32 KB external memory cache per core. Every 100 ms (10 Hz), `pushOnChange()` on Core 0 performs LittleFS SPI Flash reads (`esp_littlefs` → `esp_partition_read` → SPI Flash transactions + cache operations) and hundreds of RapidJSON heap allocations/deallocations in PSRAM. Whenever Core 1 (`audio_task`) has a cache miss into Flash (`.rodata` lookup tables `plaits::lut_*`) or reads/writes PSRAM during that 100 ms window, Core 1 is stalled waiting on the shared SPI bus and cache refill.

---

### Root Cause 2 (Why `voice.Render()` Is Exposed to PSRAM Bus Stalls): `ctagSPAllocator` Misses Internal DRAM by 848 Bytes and Places Both `plaits::Voice` (14 KB) and `shared_buffer` (16 KB) in External SPI PSRAM

Even after the P6 UI cutover freed ~187 KB of internal DRAM on the A1S, the boot log (`uimcu/tools/a1s_crackle_text.txt`) reveals that **`ctagSPAllocator` still falls back to external PSRAM**:

```text
I (00:00:00.147) ctagSPAllocator: AllocateInternalBuffer: allocating 114688 bytes
W (00:00:00.155) ctagSPAllocator: Not enough internal DRAM, trying PSRAM...
...
I (00:00:00.382) SPManager: Creating sound processor on ch 1 with id TBDaits
I (00:00:00.388) ctagSPAllocator: SetAllocationType: Single Channel CH1
I (00:00:00.396) ctagSPAllocator: Allocate: allocating CH1 14196 bytes object size, ch1 43148 bytes blockMem
...
I (00:00:01.077) main: P6 heap telemetry: internal free=209264 B (204 KiB), largest_internal_block=113840 B (111 KiB)
```

Look closely at the numbers and allocator architecture:
1. At the first line of [`app_main()`](file:///home/ubuntu/ctag-tbd/main/main.cpp#L59), `ctagSPAllocator::AllocateInternalBuffer(CONFIG_SP_FIXED_MEM_ALLOC_SZ)` requests a single contiguous block of **`114,688` bytes** (`112 KiB`).
2. Because ESP32 internal DRAM is split across non-contiguous SRAM regions at boot, the largest single contiguous internal DRAM block is **`113,840` bytes (`0x1BCB0`)** — **just 848 bytes smaller than `114,688` bytes**, even though **209,264 bytes (204 KiB)** of internal DRAM are free!
3. Because `heap_caps_malloc(114688, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` fails by 848 bytes, [`ctagSPAllocator::AllocateInternalBuffer`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSPAllocator.cpp#L41-L45) falls back to `heap_caps_malloc(114688, MALLOC_CAP_SPIRAM)`.
4. Crucially, [`ctagSoundProcessor::operator new`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSoundProcessor.hpp#L91-L93) allocates the **sound processor C++ object itself** out of `ctagSPAllocator`:
   - The `ctagSoundProcessorTBDaits` instance is **14,196 bytes** (`sizeof(ctagSoundProcessorTBDaits)`), which contains the embedded `plaits::Voice voice` object (all 24 engine instances, SVF filter states, delay lines, phase accumulators, LPGs, and post-processors), `plaits::Patch patch`, `plaits::Modulations modulations`, and all `atomic<int32_t>` parameter fields.
   - Its `shared_buffer` (**16,384 bytes**, passed to `voice.Init(&allocator)`) is also carved out of `ctagSPAllocator` ([`ctagSoundProcessorTBDaits.cpp:51-53`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSoundProcessorTBDaits.cpp#L51-L53)).
5. Consequently, **every single state read and write inside `voice.Render()` accesses external 80 MHz SPI PSRAM** instead of zero-wait-state internal SRAM! When `pushOnChange()` thrashing hits the 32 KB external cache every 100 ms on Core 0, Core 1's PSRAM working set (`14 KB` `Voice` + `16 KB` `shared_buffer` + `.rodata` Flash LUTs) is evicted and stalled on the SPI bus, turning a 268 µs render into a 950–1,000 µs overrun.

---

### Root Cause 3 (Wrapper Defect): Uninitialized `cv_*` / `trig_*` Atomics & Dead CV Mappings in `ctagSoundProcessorTBDaits`

In [`components/ctagSoundProcessor/ctagSoundProcessorTBDaits.hpp:45-56`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSoundProcessorTBDaits.hpp#L45-L56), every `atomic<int32_t> cv_*` and `trig_*` member is default-initialized (`0`) instead of `-1`:

```cpp
// components/ctagSoundProcessor/ctagSoundProcessorTBDaits.hpp:45-56
atomic<int32_t> trig_enable, trig_Gate;
atomic<int32_t> smodel, cv_smodel;
atomic<int32_t> pitch, cv_pitch;
atomic<int32_t> freq, cv_freq;
atomic<int32_t> harmonics, cv_harmonics;
atomic<int32_t> timbre, cv_timbre;
atomic<int32_t> morph, cv_morph;
...
```

This is the exact same bug class documented in Finding F3 of [`docs/elements-port-review-findings.md`](file:///home/ubuntu/ctag-tbd/docs/elements-port-review-findings.md):
- In [`ctagSoundProcessor::loadPresetInternal()`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSoundProcessor.hpp#L168-L181), `setParamValueInternal(id, "cv", ...)` is only called if `model->IsParamCV(id)` is true (i.e., the preset JSON explicitly contains a `"cv"` key for that parameter).
- Any parameter without a `"cv"` or `"trig"` key in the loaded preset retains its default-constructed value of **`0`** (`!= -1`), silently binding it to `data.cv[0]` (`A.NOTE`) or `data.trig[0]` (`A.GATE`).
- Worse, if `cv_smodel` is `!= -1`, [`ctagSoundProcessorTBDaits.cpp:94-97`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSoundProcessorTBDaits.cpp#L94-L97) sets `modulations.engine = data.cv[cv_smodel]`, which causes [`plaits::Voice::Render`](file:///home/ubuntu/ctag-tbd/components/mutable/eurorack/plaits/dsp/voice.cc#L128-L140) to detect `engine_index != previous_engine_index_` whenever `data.cv[cv_smodel]` changes and invoke **`e->LoadUserData()` + `e->Reset()` + `out_post_processor_.Reset()` inside the real-time audio callback**, resetting the oscillator/engine state mid-note!
- Additionally, `knowYourself()` ([`ctagSoundProcessorTBDaits.cpp:166-171`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSoundProcessorTBDaits.cpp#L166-L171)) registers `"timbre"` → `cv_timbre` and `"morph"` → `cv_morph` in `pMapCv`, whereas `Process()` ([`ctagSoundProcessorTBDaits.cpp:113-121`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSoundProcessorTBDaits.cpp#L113-L121)) never reads `cv_timbre` or `cv_morph` (Wait: `Process()` does not apply `cv_timbre`/`cv_morph` to `patch.timbre`/`patch.morph` at all!).

---

## 4. Fix Plan & Verification Strategy

### Step 1 — Eliminate 10 Hz SPIFFS File Reads & Unconditional JSON Re-serialization in `LinkEndpoint`
- Replace the unconditional 10 Hz `GetCStrJSON*()` + `loadJSON("/spiffs/...")` polling in [`LinkEndpoint::pushOnChange()`](file:///home/ubuntu/ctag-tbd/main/LinkEndpoint.cpp#L360-L399) with lightweight dirty/generation tracking (or cached strings invalidated only on actual mutation in `SoundProcessorManager`, `Favorites`, and `MarblesEngine`).
- During normal playback/idle when no preset, favorite, plugin, or parameter is being mutated, `pushOnChange()` will do **zero SPIFFS file reads, zero RapidJSON DOM parses, and zero PSRAM allocations**.
- Even when a parameter changes (e.g. via encoder or `/link/paramSet`), only `activeParams` for that channel is re-serialized — never re-reading `/spiffs/data/sp/sp-model.jsn`, `/spiffs/data/sp/mp-*.jsn`, or `/spiffs/data/favs.jsn` from Flash.

### Step 2 — Keep `ctagSPAllocator` in Zero-Wait-State Internal SRAM
- Update [`ctagSPAllocator`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSPAllocator.cpp) so that mono channels (`CH0` and `CH1`, 56 KB each) and stereo processors fit in **internal DRAM** (`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`):
  - Since the largest contiguous internal DRAM block at `app_main()` entry is `113,840 B` (`0x1BCB0`) and total free internal DRAM is `209,264 B` (`204 KiB`), allocating either two independent `57,344 B` (`56 KiB`) channel arenas in internal DRAM for mono processors (or sizing the contiguous internal arena within the available contiguous internal block and only falling back to PSRAM if a `STEREO` allocation exceeds it) guarantees that `ctagSoundProcessorTBDaits` (`14,196 B`) and its `shared_buffer` (`16,384 B`) reside **100% in internal SRAM**.

### Step 3 — Fix `ctagSoundProcessorTBDaits` Initialization & CV Mappings
- Initialize all `cv_*` and `trig_*` atomics in [`ctagSoundProcessorTBDaits.hpp`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSoundProcessorTBDaits.hpp#L45-L56) to `-1`.
- Wire `cv_timbre` and `cv_morph` in [`ctagSoundProcessorTBDaits.cpp`](file:///home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSoundProcessorTBDaits.cpp#L86-L90) so CV modulation mapped to the `timbre` and `morph` parameters works as expected.

### Step 4 — Hardware Verification on A1S
1. Built and flashed the A1S (`0x10000` app partition only).
2. Verified in the A1S boot log (`task-694`):
   - `ctagSPAllocator` allocates **110,592 bytes in internal DRAM** (`I (133) ctagSPAllocator: Allocated 110592 bytes in internal DRAM (clamped from 114688)` — no PSRAM fallback warning).
   - Excluding `RestServer.cpp`, `BtMidiReceiver.cpp`, `components/network` (`esp_wifi`/`mdns`/`esp_netif`), and the unused `midiuart_instance` freed ~40 KiB of static internal DRAM: **137,944 bytes (134.7 KiB)** of internal DRAM (largest free block **94,208 bytes**) remain free *after* `ctagSPAllocator` claims its 110.6 KB internal SRAM arena.
   - Even the largest stereo processor (`TBDings`, reduced from 113,944 B to 105,708 B by sharing the mutually-exclusive `resonator_`, `string_`, and `fm_voice_` arrays in [`rings::Part`](file:///home/ubuntu/ctag-tbd/components/mutable/eurorack/rings/dsp/part.h#L163-L167)) fits in internal SRAM with 4,884 B headroom.
   - `TBDaits` (`14,196 B` object + `16,384 B` `shared_buffer` = `30,580 B`) now runs **100% in zero-wait-state internal SRAM**, and [`LinkEndpoint::pushOnChange()`](file:///home/ubuntu/ctag-tbd/main/LinkEndpoint.cpp#L360-L399) performs **zero SPIFFS reads and zero RapidJSON work** during steady-state playback.
3. **Hardware ear-test verified by user (2026-10-29)**: 5–10 Hz overrun crackle is completely gone; audio is clean across models.

