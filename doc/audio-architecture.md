# Audio Processing Architecture

## Overview

The audio pipeline is a fixed 2-stage serial chain running on Core 1. Sound
processors modify a shared float buffer in-place. There is no general-purpose
audio routing graph — the topology is hardcoded.

---

## Audio Task Loop

File: `main/SPManager.cpp:72-291`

```c
void IRAM_ATTR SoundProcessorManager::audio_task(void *pvParams) {
    float fbuf[BUF_SZ * 2];          // 32 frames × 2 channels = 64 floats
    float peakIn = 0.f, peakOut = 0.f;
    bool isStereoCH0 = false;

    SP::ProcessData pd;
    pd.buf = fbuf;
    float defaultCv[N_CVS] = {0.f};
    uint8_t defaultTrig[N_TRIGS] = {0};
    pd.cv = defaultCv;
    pd.trig = defaultTrig;

    while (runAudioTask) {
        // 1. LFO modulation → CV slots
        DRIVERS::ModEngine::Process(pd.cv);

        // 2. Read interleaved float samples from codec (ADC)
        DRIVERS::Codec::ReadBuffer(fbuf, BUF_SZ);

        // 3. DC-cut + input peak detection
        for (uint32_t i = 0; i < BUF_SZ; i++) {
            fbuf[i * 2]   = in_dccutl(fbuf[i * 2]);
            fbuf[i * 2+1] = in_dccutr(fbuf[i * 2+1]);
        }

        // 4. Noise gate (optional channel masking)

        // 5. LED green = input level

        // 6. *** SOUND PROCESSORS (mutex-protected) ***
        if (xSemaphoreTake(processMutex, 0) == pdTRUE) {
            if (sp[0] != nullptr) {
                isStereoCH0 = sp[0]->GetIsStereo();
                sp[0]->Process(pd);       // sp[0] processes in-place
            }
            if (!isStereoCH0) {
                if (ch01Daisy) {          // copy L→R before ch1
                    for (uint32_t i = 0; i < BUF_SZ; i++)
                        fbuf[i * 2 + 1] = fbuf[i * 2];
                }
                if (sp[1] != nullptr)
                    sp[1]->Process(pd);   // sp[1] sees sp[0]'s output
            }
            xSemaphoreGive(processMutex);
        } else {
            memset(fbuf, 0, BUF_SZ * 2 * sizeof(float)); // mute if lock held
        }

        // 7. Mono-to-stereo conversion matrix (toStereoCH0/toStereoCH1)
        //    8 routing modes: spread, swap, mix, etc.

        // 8. Soft-clipping + output peak (red LED)

        // 9. SD audio: MixPlayback + RecordSamples

        // 10. Write back to codec (DAC)
        DRIVERS::Codec::WriteBuffer(fbuf, BUF_SZ);
    }
    // cleanup
    memset(fbuf, 0, BUF_SZ * 2 * sizeof(float));
    DRIVERS::Codec::WriteBuffer(fbuf, BUF_SZ);
    runAudioTask = 2;
    vTaskDelete(NULL);
}
```

---

## Buffer Format

| Property | Value |
|----------|-------|
| Type | `float` (IEEE 754 32-bit) |
| `BUF_SZ` | **32** frames per block |
| Buffer total | `float fbuf[64]` = 32 frames × 2 channels interleaved `[L0, R0, L1, R1, ...]` |
| Amplitude range | -1.0 to +1.0 (normalized) |
| Sample rate | **44,100 Hz** |
| Block duration | 32 / 44100 ≈ **0.726 ms** |

---

## Processing Pipeline (`sp[0]` / `sp[1]`)

Processing is strictly serial, not parallel:

1. `sp[0]->Process(pd)` runs first, modifying `fbuf` **in-place**.
2. Only if `isStereoCH0 == false` does `sp[1]->Process(pd)` run.
3. If `sp[0]` is stereo (`isStereoCH0 == true`), **`sp[1]` is never called** —
   both audio channels are consumed by the stereo processor on CH0.
4. `pd.buf` always points to the same `fbuf[64]` — no intermediate copy.

### ch01Daisy Chain

When enabled (`"ch01_daisy": "on"` in config), after `sp[0]` completes and
before `sp[1]` runs:

```c
for (uint32_t i = 0; i < BUF_SZ; i++)
    fbuf[i * 2 + 1] = fbuf[i * 2];  // copy L (ch0 output) → R (ch1 input)
```

- Only applies when `sp[0]` is mono.
- Routes `sp[0]` output (left channel) into `sp[1]` input (right channel).
- The two mono plugins are effectively chained in series.

---

## processMutex

`processMutex` is a FreeRTOS mutex protecting `sp[0]` and `sp[1]` instance
pointers, creation/destruction, preset loading, and `Process()` calls.

| Role | Take behavior | Consequence if locked |
|------|---------------|-----------------------|
| Audio task (Core 1, prio 23) | Zero-wait try-lock | **Audio is muted** (buffer zeroed) |
| Control path (UI, REST, Serial) | Infinite wait (`portMAX_DELAY`) | Blocks until audio releases |

---

## Stereo vs Mono Allocation

File: `components/ctagSoundProcessor/ctagSPAllocator.cpp`

The arena allocator splits the 112 KB internal buffer based on `AllocationType`:

| Allocation Type | CH0 arena | CH1 arena |
|----------------|-----------|-----------|
| `CH0` (mono on ch0) | 56 KB | none |
| `CH1` (mono on ch1) | none | 56 KB |
| `STEREO` | **112 KB** (full) | none |

- Mono plugins on CH0 and CH1 each get **56 KB** of the arena.
- Stereo plugins get the **full 112 KB** — CH1 plugin is destroyed.
- If internal DRAM allocation fails, falls back to `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`.

---

## Memory Resources

| Resource | Size | Notes |
|----------|------|-------|
| Internal DRAM total | ~320 KB | ESP32-D0WD |
| DRAM free after audio init | ~22 KB | Largest block ~17 KB |
| Sound processor arena | **114,688 bytes (112 KB)** | `CONFIG_SP_FIXED_MEM_ALLOC_SZ` |
| External PSRAM | **8 MB** | ESPPSRAM64, 80 MHz, cache in low/high mode |
| PSRAM free at boot | ~3.5 MB | After system + audio initialization |

---

## Constraints for Stereo Chaining

**Two stereo plugins in series is NOT currently possible** due to:

1. **Architecture**: When `sp[0]` is stereo, `sp[1]` is deleted and never runs.
   The audio loop only calls `sp[1]->Process()` if `sp[0]->GetIsStereo() == false`.

2. **Allocation**: Stereo plugins get the full 112 KB arena. There is no
   mechanism to split the arena for two stereo instances.

### What would be required

- **New `AllocType`** (e.g. `STEREO_CHAIN`) that splits arena between two
   stereo plugins (or allocates from PSRAM).
- **Audio loop** modification: call a second `Process()` even when `sp[0]`
   is stereo.
- **Plugin browser** UI: option to "Chain" a second stereo plugin.
- **Plugin memory**: each stereo plugin would get less internal arena; must
   rely on PSRAM fallback (already supported via `heap_caps_malloc`).

### What works today

**Dual mono chaining**: load TBDaits as mono on ch0, Claude as mono on ch1,
enable `ch01Daisy`. Each gets 56 KB of the arena. Output of ch0 feeds ch1
input. This is fully supported with the existing dual-mono dropdown in the
plugin browser (`Ch0 / Ch1 / Both`).
