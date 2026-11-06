# Implementation Plan: Porting Optimizations & Fixes from `servandobarreiro/ctag-tbd`

This implementation plan accompanies [doc/servando_tbd16_optimizations.md](file:///home/ubuntu/ctag-tbd/doc/servando_tbd16_optimizations.md). It audits which items from `servandobarreiro/ctag-tbd` are already present in our `ctag-bba-v2-s3` branch and provides exact step-by-step instructions for another agent to execute the remaining 6 optimizations/fixes.

---

## Part 1: Pre-Implementation Audit (`ctag-bba-v2-s3`)

### Already Present in `ctag-bba-v2-s3` (Do NOT Re-Apply)
- **Item #1 (`ctagDiodeLadderFilter` & `ctagDiodeLadderFilter5` coefficient caching, commit `60523ec6`)**: Already applied in `components/ctagSoundProcessor/filters/ctagDiodeLadderFilter.cpp` and `ctagDiodeLadderFilter5.cpp`.
- **Item #3 (`TBD03` reciprocal & per-sample `eg_step` hoisting, commit `c1285111`)**: Already applied (plus block-level filter devirtualization) in `components/ctagSoundProcessor/ctagSoundProcessorTBD03.cpp:64-260`.
- **Item #5 (`codec_bba2.cpp` static buffer + float-domain clamping)**: Already applied in `components/drivers/codec_bba2.cpp:121-170`.
- **Item #6 (Full-block output peak detection)**: Already applied in `main/SPManager.cpp:396-415`.

### Remaining Items to Execute (Steps 1–6 Below)
1. **Step 1 (Item #2)**: `PsramStringMap` (`PsramAllocator.hpp`) for `pMapPar` / `pMapCv` / `pMapTrig` in `ctagSoundProcessor.hpp`.
2. **Step 2 (Item #7)**: `loadPresetInternal()` missing-key guard (`HasParam`) in `ctagSPDataModel` and `ctagSoundProcessor.hpp`.
3. **Step 3 (Item #8)**: Out-of-bounds clamps in `RomplerVoice.cpp`, `RomplerVoiceMinimal.cpp`, `ChordSynth.cpp`, and delay-bounds comments in `mifx/reverb.h`.
4. **Step 4 (Item #4)**: Bypass redundant software DC blocker (`in_dccutl` / `in_dccutr`) on BBA2 in `main/SPManager.cpp` (WM8978 hardware ADC HPF is already active).
5. **Step 5 (Item #2.2)**: Increase `ctagSampleRom::BufferInSPIRAM()` PSRAM safety reserve from `128 KB` to `512 KB`.
6. **Step 6 (Item #9)**: Disable redundant Newlib `stdio` buffering (`setvbuf(..., _IONBF, 0)`) on `SDAudio` 4 KB streaming reads/writes.

---

## Part 2: Step-by-Step Execution Instructions

> **Working Rules for the Executing Agent (from `AGENTS.md`)**:
> 1. **NEVER edit files with inline python/sed scripts** — use `read_file`, `edit_file`, and `write_file` tools only.
> 2. Read each target file around the indicated line numbers before applying `edit_file`.
> 3. Build and verify on hardware after completing the edits.

---

### Step 1: Add `PsramAllocator.hpp` and Switch `pMapPar` / `pMapCv` / `pMapTrig` to `PsramStringMap`

#### 1a. Create `components/ctagSoundProcessor/helpers/PsramAllocator.hpp`
Create `/home/ubuntu/ctag-tbd/components/ctagSoundProcessor/helpers/PsramAllocator.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "esp_heap_caps.h"

namespace CTAG { namespace SP { namespace HELPERS {

template <typename T>
struct PsramAllocator {
    using value_type = T;
    PsramAllocator() noexcept = default;
    template <typename U> PsramAllocator(const PsramAllocator<U>&) noexcept {}
    T* allocate(std::size_t n) {
        void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM);
        if (!p) {
            // Fallback to default malloc when PSRAM is unavailable (e.g. simulator).
            p = std::malloc(n * sizeof(T));
        }
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t) noexcept {
        heap_caps_free(p);
    }
};

template <class T, class U>
inline bool operator==(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept { return true; }
template <class T, class U>
inline bool operator!=(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept { return false; }

template <typename T>
using PsramVector = std::vector<T, PsramAllocator<T>>;

// PSRAM-backed std::map<std::string, V> — routes red-black tree nodes and
// std::function targets to PSRAM, freeing ~5-25 KB of internal SRAM.
template <typename V>
using PsramStringMap = std::map<
    std::string, V,
    std::less<std::string>,
    PsramAllocator<std::pair<const std::string, V>>
>;

}}} // namespace CTAG::SP::HELPERS
```

#### 1b. Update `components/ctagSoundProcessor/ctagSoundProcessor.hpp`
1. Include `"helpers/PsramAllocator.hpp"` right after `#include "ctagSPAllocator.hpp"` (around line 92).
2. Replace `pMapPar`, `pMapCv`, and `pMapTrig` declarations (around lines 210–212):
```cpp
            HELPERS::PsramStringMap<function<void(const int)>> pMapPar;
            HELPERS::PsramStringMap<function<void(const int)>> pMapCv;
            HELPERS::PsramStringMap<function<void(const int)>> pMapTrig;
```

---

### Step 2: Add `HasParam()` Guard to `loadPresetInternal()`

#### 2a. Declare `HasParam` in `components/ctagSoundProcessor/ctagSPDataModel.hpp`
Around line 50 (right after `int GetParamValue(const string &id, const string &key);`), add:
```cpp
            bool HasParam(const string &id);
```

#### 2b. Implement `HasParam` in `components/ctagSoundProcessor/ctagSPDataModel.cpp`
Around line 128 (right after `GetParamValue`), add:
```cpp
bool ctagSPDataModel::HasParam(const string &id) {
    if (!activePreset.IsObject()) return false;
    if (!activePreset.HasMember("params")) return false;
    Value &patchParams = activePreset["params"];
    if (!patchParams.IsArray()) return false;
    for (auto &v : patchParams.GetArray()) {
        if (!v.HasMember("id") || !v["id"].IsString()) continue;
        if (v["id"] == id) return true;
    }
    return false;
}
```
*(Also change `if (!v.HasMember("id")) return 0;` in `GetParamValue` at lines 119–120 to `if (!v.HasMember("id") || !v["id"].IsString()) continue;` so a malformed entry doesn't abort the search early).*

#### 2c. Guard `loadPresetInternal()` in `components/ctagSoundProcessor/ctagSoundProcessor.hpp`
In `ctagSoundProcessor::loadPresetInternal()` (around lines 188–202), add `if (!model->HasParam(kv.first)) continue;`:
```cpp
            virtual void loadPresetInternal() {
                // iterate all parameters, take names from parameter map (first element)
                for (auto const &kv: pMapPar) {
                    if (!model->HasParam(kv.first)) continue;
                    setParamValueInternal(kv.first, "current", model->GetParamValue(kv.first, "current"));
                    // check if cv and trig are set in preset, if so set in processor param
                    if (model->IsParamCV(kv.first)) {
                        //ESP_LOGW("MOdel", "IntParam %s, %d", name.c_str(), model->GetParamValue(name, "cv"));
                        setParamValueInternal(kv.first, "cv", model->GetParamValue(kv.first, "cv"));
                    } else if (model->IsParamTrig(kv.first)) {
                        //ESP_LOGW("MOdel", "BoolParam %s, %d", name.c_str(), model->GetParamValue(name, "trig"));
                        setParamValueInternal(kv.first, "trig", model->GetParamValue(kv.first, "trig"));
                    }
                }
            };
```

---

### Step 3: Add Out-of-Bounds Clamps in `RomplerVoice`, `RomplerVoiceMinimal`, `ChordSynth`, and `mifx/reverb.h`

#### 3a. `components/ctagSoundProcessor/synthesis/RomplerVoice.cpp`
Around line 135, clamp `readBufferLength` to `readBufferMaxSize - 4`:
```cpp
        readBufferLength = static_cast<uint32_t>(phaseIncrement * float(size) + readBufferPhase);
        if (readBufferLength > static_cast<int32_t>(readBufferMaxSize - 4)) {
            readBufferLength = readBufferMaxSize - 4;
        }
```

#### 3b. `components/ctagSoundProcessor/synthesis/RomplerVoiceMinimal.cpp`
1. Around line 59 (after `ad.SetDecay(params.d);`), add envelope looping:
```cpp
        ad.SetAttack(params.a);
        ad.SetDecay(params.d);
        ad.SetLoop(params.loop);
```
2. Around line 123, clamp `readBufferLength` to `readBufferMaxSize - 2`:
```cpp
        readBufferLength = static_cast<uint32_t>(phaseIncrement * float(size) + readBufferPhase);
        if (readBufferLength > static_cast<int32_t>(readBufferMaxSize - 2)) {
            readBufferLength = readBufferMaxSize - 2;
        }
```

#### 3c. `components/ctagSoundProcessor/polypad/ChordSynth.cpp`
Around lines 83–90, fix `calcInversion` to respect `nnotes` and clamp `idx` into `[0, kChordNumNotes - 1]`:
```cpp
void  CTAG::SP::ChordSynth::calcInversion(int8_t *ht_steps, const int16_t &chord, const int16_t &inversion,
                                                   const int16_t &nnotes) {
    int8_t inv[8];
    for (int i = 0; i < nnotes; i++) {
        int idx = i + 2 + inversion;
        if (idx < 0) idx = 0;
        if (idx >= kChordNumNotes) idx = kChordNumNotes - 1;
        inv[i] = chords[chord][idx];
    }
    memcpy(ht_steps, inv, nnotes);
}
```

#### 3d. `components/ctagSoundProcessor/mifx/reverb.h`
Add the delay-line buffer bounds comments around lines 89–125 and 179–215 documenting that `ap1` (`150` samples, center `10` + depth `80`), `del2` (`6312` samples, center `6261` + depth `50`), and `del1` (`4501` samples, center `4460` + depth `40`) are sized to exact capacity and LFO depths must not be increased.

---

### Step 4: Bypass Redundant Software DC Blocker on BBA2 (`main/SPManager.cpp`)

In `main/SPManager.cpp` around lines 178–191:
On `CONFIG_TBD_PLATFORM_BBA2`, `codec_bba2.cpp:91` already enables WM8978's hardware digital ADC high-pass filter (`write_reg(14, 0x108)` — `HPFEN = 1`, 3.7 Hz cutoff @ 44.1 kHz). Guard `in_dccutl` / `in_dccutr` with `#if !defined(CONFIG_TBD_PLATFORM_BBA2)` so BBA2 only computes `maxl` / `maxr` in that loop:

```cpp
        // In peak detection (on BBA2, DC cut is performed in WM8978 ADC hardware HPF R14)
        float maxl = 0.f, maxr = 0.f;
        float max = 0.f;
        for (uint32_t i = 0; i < BUF_SZ; i++) {
#if !defined(CONFIG_TBD_PLATFORM_BBA2)
            fbuf[i * 2] = in_dccutl(fbuf[i * 2]);
#endif
            float val = fabsf(fbuf[i * 2]);
            if (val > maxl) maxl = val;
#if !defined(CONFIG_TBD_PLATFORM_BBA2)
            fbuf[i * 2 + 1] = in_dccutr(fbuf[i * 2 + 1]);
#endif
            val = fabsf(fbuf[i * 2 + 1]);
            if (val > maxr) maxr = val;
        }
```
*(Also guard the `in_dccutl` / `in_dccutr` definitions at the top of `SPManager::audio_task` with `#if !defined(CONFIG_TBD_PLATFORM_BBA2)` to avoid unused-variable warnings).*

---

### Step 5: Increase `ctagSampleRom::BufferInSPIRAM()` PSRAM Safety Reserve

In `components/ctagSoundProcessor/helpers/ctagSampleRom.cpp` around line 219:
Change the PSRAM reservation from `128 * 1024` to `512 * 1024` so `BufferInSPIRAM()` always leaves at least 512 KB of PSRAM free for `RapidJSON` (`CrtAllocator`) and `PsramStringMap`:
```cpp
    void ctagSampleRom::BufferInSPIRAM() {
        if(ptrSPIRAM != nullptr) return; // already buffered
        size_t maxSizeBytes = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        if (maxSizeBytes <= 512 * 1024) return;
        maxSizeBytes -= 512 * 1024; // reserve 512K for RapidJSON / PsramStringMap
        if(maxSizeBytes < 1024*1024) return; // not enough memory for this to make sense
```

---

### Step 6: Disable Redundant `stdio` Buffering in `SDAudio.cpp`

In `main/SDAudio.cpp`, inspect where `fopen` is called for playback (`"rb"`) and recording (`"wb"`). Because `SDAudio` already uses its own static 4 KB buffer (`static int16_t tmp[kIoChunkFrames * 2]`), call:
```cpp
setvbuf(f, nullptr, _IONBF, 0);
```
immediately after a successful `fopen()` in the `sd_audio` worker task so Newlib does not allocate a separate `BUFSIZ` buffer or perform extra intermediate `memcpy` calls on Core 0.

---

## Part 3: Build, Flash & Verification Procedure

### 1. Build Firmware (`build-bba2`)
```bash
bash -c "source /home/ubuntu/esp-idf-v6.1/export.sh >/dev/null 2>&1 && ninja -C build-bba2 gen_project_binary"
```
Verify the build succeeds with zero errors and no new warnings in the modified files.

### 2. Flash to S3 DSP (`/dev/ttyACM1`)
```bash
/home/ubuntu/.espressif/python_env/idf6.1_py3.12_env/bin/python -m esptool --chip esp32s3 -p /dev/ttyACM1 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x10000 build-bba2/ctag-tbd.bin
```

### 3. Hardware Verification via C3 Panel (`/dev/ttyACM0`)
Run a single-session verification script using `uimcu.tools.screenshot` (`open_port`, `wait_ready`, `request`) to verify:
1. **Internal SRAM savings (`PsramStringMap`)**:
   - Query `/debug/getHeap` before and after loading parameter-heavy plugins (`Subbotnik`, `Retroactor`, `Elements`, `TBDaits`). Confirm internal 8-bit free heap (`freeInternal`) increases by ~5–20 KB compared to baseline while `largestInternal` remains intact.
2. **Preset loading (`HasParam` guard)**:
   - Switch between plugins and presets (`/debug/setActivePlugin`, `/link/presetLoad`) and verify parameters initialize cleanly.
3. **Zero I2S stalls / overruns**:
   - Sample `/debug/getAudioStats` and `/debug/getLoopHist` (`{"len": 128}`) across a 10-second window at both 44.1 kHz (`Void` / `TBD03` / `TBDaits`) and 32 kHz (`Elements`) and verify:
     - `diagStallRd` delta = `0`
     - `diagStallWr` delta = `0`
     - `diagLoopOver` delta = `0`
     - `i2sErrR` = `0`, `i2sErrW` = `0`, `cpuOverload` = `0`.
