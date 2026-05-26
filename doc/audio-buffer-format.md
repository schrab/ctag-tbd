# Audio Buffer Format & Codec

## Buffer Properties
| Property | Value |
|----------|-------|
| Sample type | `float` (32-bit IEEE 754) |
| Range | -1.0 to +1.0 (0dBFS = ±1.0f) |
| Layout | Stereo interleaved `[L, R, L, R, ...]` |
| Block size | `BUF_SZ = 32` frames |
| Total buffer | `BUF_SZ * 2 = 64` floats |
| Indexing | `buf[i*2]` = left, `buf[i*2+1]` = right |
| Sample rate | 44100 Hz |
| Processing | In-place — read input, overwrite with output |

## Codec API
```cpp
CTAG::DRIVERS::Codec::ReadBuffer(float *buf, uint32_t sz);
CTAG::DRIVERS::Codec::WriteBuffer(float *buf, uint32_t sz);
```

## Audio Task Flow
```
Codec::ReadBuffer(fbuf, 32)      ← I2S DMA, converts to float
DC cut, noise gate, peak detect
sp[0]->Process(pd)               ← plugin modifies fbuf in-place
sp[1]->Process(pd)               ← if not stereo ch0
SoftClip
Codec::WriteBuffer(fbuf, 32)     ← converts back, I2S DMA out
```

## File I/O Constraints from Audio Task
| Constraint | Value |
|------------|-------|
| Stack size | 4096 bytes — too small for fopen/fread |
| Priority | 23 (highest) — blocking starves everything |
| IRAM | Task is IRAM_ATTR, fopen is flash code (cache misses) |
| Blocking | 0.7ms deadline per block — fopen can take many ms |
| Mutex | Uses `xSemaphoreTake(mutex, 0)` — never blocks, mutes on contention |

**`fopen`/`fread` from LittleFS does NOT work from audio task.** 
All JSON file I/O happens from control/UI tasks (protected by processMutex).
Fast reads use `esp_flash_read()` (SPI flash directly) or pre-buffered SPIRAM.
