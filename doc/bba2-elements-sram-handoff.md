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
   - Clean compilation under ESP-IDF v6.1 (`b31ee0a0`).
   - App binary size: `0x351480` bytes (5% headroom in 0x380000 partition).
3. **Hardware Verification (Confirmed on BBA2 hardware)**:
   - Flashed to `/dev/ttyACM0` and verified in boot log:
     ```
     I (11281) Elements: object 115844 bytes -> INTERNAL (largest internal block was 31744)
     I (11551) Elements: reverb buffer in PSRAM
     I (11551) Elements: single-core S3: 32 modes (cap 52)
     ```
   - Audio confirmed clean by user at nominal settings ("sounds clean").

---

## 6. Open Findings & Next Investigation Steps

### Finding 1: Freeze on Random Parameter Change

- **Symptom:**
  Adjusting parameters randomly from the panel or link interface causes the DSP to freeze or disconnect ("DSP link lost").
- **Root Cause Analysis:**
  1. On ESP32-S3, stdout console is routed through the native USB-Serial-JTAG controller (`/dev/usbserjtag`).
  2. `CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y` is set in the debug build (`LOG_DEFAULT_LEVEL=4`).
  3. In `components/ctagSoundProcessor/ctagSPDataModel.cpp` (lines 81, 90), `ESP_LOGD("Model", "Setting id %s, with %s to %d", ...)` and `ESP_LOGD("Model", "I found the value")` execute synchronously inside `ctagSPDataModel::SetParamValue` on every single parameter change received over the link.
  4. In ESP-IDF's `components/esp_driver_usb_serial_jtag/src/usb_serial_jtag_vfs.c`:
     ```c
     do {
         if (usb_serial_jtag_ll_txfifo_writable()) { ... break; }
     } while ((esp_timer_get_time() - s_ctx.last_tx_ts) < TX_FLUSH_TIMEOUT_US);
     ```
     When the 64-byte hardware TX FIFO is full (e.g. when the USB CDC host is not actively reading stdout or the endpoint buffer is saturated), each character write busy-waits up to `TX_FLUSH_TIMEOUT_US` (50,000 µs = 50 ms).
  5. A single 60-character debug message takes up to 3 seconds of synchronous blocking in Core 0's link task. This blocks the link loop, starves link heartbeats, trips the Task Watchdog or C3 offline watchdog (10 s), and drops the USB link.
  6. Additionally, `PerfMonitor::EnableLogging(true, 5000, 1, 5)` was active on Core 1 (audio core), periodically dumping multi-line log strings to stdout.
- **Action Items to Resolve:**
  1. Remove or comment out the `ESP_LOGD("Model", ...)` calls at lines 81 and 90 of `components/ctagSoundProcessor/ctagSPDataModel.cpp`. Parameter change paths must remain allocation-free and logging-free.
  2. Disable `PerfMonitor::EnableLogging` periodic stdout tasks on the audio core in `components/ctagSoundProcessor/ctagSoundProcessorElements.cpp`.
  3. Set `CONFIG_LOG_DEFAULT_LEVEL_INFO=y` in `sdkconfig.defaults.bba2` so verbose debug statements do not flood the USB-Serial-JTAG FIFO.

### Finding 2: Crackles at Geometry > 900 (Modal Resonator Headroom)

- **Symptom:**
  Elements plays cleanly at default/low geometry, but introducing higher geometry values (> 900) introduces small crackles in the audio output.
- **Root Cause Analysis:**
  1. **Budget:** At 32 kHz with 32 samples per block on a 240 MHz ESP32-S3, the real-time block render budget is `32 / 32000 * 240,000,000` = **240,000 cycles (1,000 µs)**.
  2. **Baseline Load:** PERF_MONITOR baseline on Core 1 at 32 modes:
     `Elements_Core1: avg= 777.22 µs (186,532 cyc) min= 728.76 µs max=2602.47 µs n=12684`.
     Average execution already occupies ~78% of the budget.
  3. **Resonator Non-linearity with Geometry:** In `components/mutable/eurorack/elements/dsp/resonator.cc:67`:
     `stiffness = Interpolate(lut_stiffness, geometry_, 256.0f);`
     - When `geometry < 900` (`geometry_ < 0.22`), `stiffness` is zero or negative; `brightness_attenuation = (1 - geometry_)^8` heavily damps higher modes, keeping per-block SVF iterations low and stable.
     - When `geometry > 900`, `stiffness` crosses into positive values (up to +0.708), and `q_loss_damping_rate` triples. This sustains high Q factors and active oscillations across all 32 modes simultaneously.
  4. The cumulative compute of 32 simultaneous ringing modes under high geometry pushes block render cycles beyond 240,000 cycles, causing I2S TX DMA underruns (crackles).
- **Action Items to Resolve:**
  1. Lower `kResolutionFull` in `components/ctagSoundProcessor/ctagSoundProcessorElements.hpp:82` from 32 modes to **28 or 24 modes** on BBA2:
     - At 28 modes, SVF cycle count drops by ~12.5% (~25,000 cycles/block).
     - At 24 modes, SVF cycle count drops by 25% (~50,000 cycles/block), leaving > 100,000 cycles (~40%) headroom.
  2. Test geometry sweep 0..4095 on hardware to verify that crackles are completely eliminated.
