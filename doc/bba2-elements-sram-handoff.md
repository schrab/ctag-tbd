# BBA2 ESP32-S3 Audio & SRAM Architecture Handoff

**Date:** 2026-11-05 · **Branch:** `ctag-bba-v2-s3` · **Hardware:** BBA2 rev A (ESP32-S3-WROOM-1 N16R8, WM8978 codec via I2S master, ESP32-C3 panel link via UART1 on GPIO1 TX / GPIO42 RX).

---

## 1. Executive Summary & Progression

During the bring-up and stabilization of the BBA2 (ESP32-S3) audio engine, four stacked issues were identified and addressed:

1. **Link Task Shared I-Cache Contention (Fixed in `adb0ee9d`)**:
   - `CONFIG_FREERTOS_HZ` was unintentionally set to 1000 Hz (1 ms tick) instead of 100 Hz (10 ms tick).
   - On S3, the L1 instruction cache is shared between Core 0 and Core 1. A 1 kHz link polling loop on Core 0 constantly evicted audio render code from the shared cache.
   - Restored `CONFIG_FREERTOS_HZ=100` and `vTaskDelay(1)` (10 ms) pacing.

2. **Dirty Distortion / Harsh Harmonic Artifacts (Fixed in `adb0ee9d`)**:
   - Both Elements and Bjorklund suffered from digital distortion when filters or resonators decayed.
   - Root cause: Denormal float underflows in IIR filter state variables.
   - Restored Xtensa FPU `WUR.FCR` with bit 5 (`FTZ | DAZ` — Flush-To-Zero and Denormals-Are-Zero) on Core 1 audio task. Elements and Bjorklund audio output cleared up immediately.

3. **5-Second Freeze / CPU Cache Error Panic (Fixed in `514bdd34`)**:
   - Exactly 5 seconds after boot, the DSP crashed or froze with "DSP link lost".
   - Root cause: `SPManagerDataModel::SetActivePluginID()` set `storePending = true` during boot instantiation even when the plugin ID and patch number did not change. Exactly 5 seconds later, `FlushModelStoreIfDue()` wrote to SPI flash without taking `processMutex`, stalling MSPI cache while Core 1 accessed PSRAM/flash.
   - Fixed by skipping redundant `storePending` triggers and taking `processMutex` during deferred flash writes to cleanly mute audio and prevent cache collisions.

4. **Periodic Crackles / Overruns on Elements (Addressed in this handoff)**:
   - Elements rendered cleanly for simple inputs but crackled periodically on sustained chords/strikes.
   - Root cause: Elements object was forced into external PSRAM because `ctagSPAllocator` consumed 114,688 bytes of SRAM1, leaving only 112,640 bytes contiguous (Elements needed 115,844 bytes).
   - Running from PSRAM doubled render time from ~117k to ~246k cycles per 32-sample block, exceeding the 240,000-cycle real-time budget at 32 kHz.

---

## 2. SRAM1 & SRAM2 Architectural Deep Dive

### Hardware Memory Map Comparison

| Memory Region | Classic ESP32 (A1S AudioKit) | ESP32-S3 (BBA2 N16R8) |
|---|---|---|
| **SRAM0** | 64 KB I-bus only (IRAM) | **32 KB Cache**: 16 KB I-Cache + 16 KB reclaimed IRAM (`0x40374000`) |
| **SRAM1** | **128 KB** D/IRAM (Max contiguous DRAM was **~113.8 KB** at `0x3FFE4350`) | **416 KB Unified D/IRAM** (`0x3FC88000..0x3FCF0000` / `0x40378000..0x403E0000`). Both I-bus & D-bus share this pool. |
| **SRAM2** | **200 KB** DRAM-only, fragmented by ROM & static data (<20 KB usable chunks) | **64 KB Data Cache**: 32 KB D-Cache + 32 KB DRAM-only heap block at `0x3FCF0000` |

### Why Legacy Assumptions Caused the Crackles

In the classic ESP32, SRAM1 had a hard limit of 128 KB. `CONFIG_SP_FIXED_MEM_ALLOC_SZ` (114,688 B = 112 KB) was designed to claim almost the entire 113.8 KB contiguous DRAM block at boot. Elements (115,844 B) could *never* fit in internal SRAM on classic ESP32 and had to run from PSRAM with a dedicated Core 0 helper task.

On ESP32-S3, SRAM1 is a unified **416 KB block**. In `build-bba2/ctag-tbd.map`:
- Code and static data end at `_heap_start = 0x3FCA2C10`.
- The contiguous free internal DRAM in SRAM1 is **~232 KB**.

When `ctagSPAllocator::AllocateInternalBuffer(114688)` ran at boot:
1. It took 114,688 bytes from SRAM1.
2. Contiguous SRAM1 remaining dropped to **112,640 bytes**.
3. `ctagSoundProcessorElements` requested **115,844 bytes** in internal SRAM.
4. Because `115,844 > 112,640` (short by 3,204 bytes), `operator new` fell back to **PSRAM**:
   ```
   I (31182) Elements: object 115844 bytes -> PSRAM (internal block too small!) (largest internal block was 112640)
   ```
5. Modal resonator SVF state reads/writes suffered external SPI PSRAM latency and cache miss penalties:
   - Internal SRAM render cost: **~117,000 cycles**.
   - PSRAM render cost: **~246,000 cycles**.
   - At 32 kHz (32 samples = 1 ms @ 240 MHz), the budget is **240,000 cycles**. 246k cycles overruns the budget, starving I2S DMA and producing crackles.

---

## 3. The Solution

### 1. Right-Size `ctagSPAllocator` to 106,496 Bytes (104 KB) on BBA2
- Following the rings union optimization (`rings::Part` sharing mutually exclusive resonator/string/FM voice arrays), the largest plugin allocated from `ctagSPAllocator` is stereo `TBDings` at **105,708 bytes**.
- Capping `allocSize` on BBA2 to **106,496 bytes** provides 788 bytes of headroom for TBDings while freeing **8,192 bytes** in SRAM1.
- Contiguous free SRAM1 increases to `112,640 + 8,192 = 120,832 bytes`.
- `ctagSoundProcessorElements` (115,844 bytes) fits comfortably (`120,832 >= 115,844`) in fast internal SRAM1!

### 2. Retain 64 KB Reverb Buffer in Octal PSRAM
- Delay lines and reverb buffers are sequential stream accesses with high cache locality (bench-measured cost ~1.1% in PSRAM).
- Keeping `reverb_buffer` in PSRAM preserves SRAM1 and the 32 KB SRAM2 region (`0x3FCF0000`) for FreeRTOS tasks, link UART RX ring buffers, and system dynamic allocations.

### 3. Update Kconfig Range
- `main/Kconfig.projbuild`: Expanded `SP_FIXED_MEM_ALLOC_SZ` range from `113944 118784` to `104448 118784` to permit lower allocation sizes.

---

## 4. Code Changes Summary

- **`components/ctagSoundProcessor/ctagSPAllocator.cpp`**:
  ```cpp
  #if defined(CONFIG_TBD_PLATFORM_BBA2)
      if (allocSize > 106496) allocSize = 106496;
  #endif
  ```
  And starts stepping from `allocSize` instead of raw `size`.

- **`components/ctagSoundProcessor/ctagSoundProcessorElements.cpp`**:
  ```cpp
  #if defined(CONFIG_TBD_PLATFORM_BBA2)
      reverb_buffer = (uint16_t *) heap_caps_malloc(
          32768 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  #else
      reverb_buffer = (uint16_t *) heap_caps_malloc(
          32768 * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      if (reverb_buffer == nullptr) {
          reverb_buffer = (uint16_t *) heap_caps_malloc(
              32768 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
      }
  #endif
  ```

- **`main/Kconfig.projbuild`**:
  Range lowered to `104448 118784`.

---

## 5. Verification & Test Status

1. **Host Unit Tests (`tests/host/run_tests.sh`)**:
   - All 36 host tests passing (marbles, link protocol, input source, chain budget).
2. **Firmware Build (`idf.py -B build-bba2 build`)**:
   - Clean compilation under ESP-IDF v6.1.
   - App binary size: `0x351480` bytes (5% headroom in 0x380000 partition).
3. **Flashing / Hardware Verification Step**:
   - Next command to run on target:
     ```bash
     idf.py -B build-bba2 -p /dev/ttyACM0 flash monitor
     ```
   - Expected boot log confirmation:
     ```
     I (...) Elements: object 115844 bytes -> INTERNAL (largest internal block was ~120832)
     I (...) Elements: reverb buffer in PSRAM
     ```
