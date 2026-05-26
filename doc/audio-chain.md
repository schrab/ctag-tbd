# Audio Processing Chain

## Architecture
- **Core 0**: UI tasks (menu, LED) at low priority (idle+2/3)
- **Core 1**: Audio task at highest priority (23)

## Audio Task Loop (`SPManager::audio_task`)
```
while (runAudioTask) {
    1. Read CVs + trigs: Control::Update(&pd.trig, &pd.cv)
    2. Read audio from codec: Codec::ReadBuffer(fbuf, 32)
    3. DC removal, peak detection, noise gate
    4. Process sound processors:
       - sp[0]->Process(pd)  // CH0 processes fbuf in-place
       - if !stereo: sp[1]->Process(pd) // CH1 processes fbuf
    5. Mono-to-stereo conversions
    6. Soft-clipping (stmlib::SoftClip)
    7. LED level metering
    8. Write audio: Codec::WriteBuffer(fbuf, 32)
}
```

## Timing
- Sample rate: 44100 Hz
- Buffer size: 32 frames per block (~0.726ms)
- I2S DMA: 4 buffers × 32 frames = 128 frames total DMA latency
- Block rate for modulation: 44100/32 ≈ 1378 Hz

## ProcessData
```cpp
struct ProcessData {
    float *buf;    // 32 frames × 2 ch interleaved [L,R,L,R,...]
    float *cv;     // CV array (size N_CVS, platform-dependent)
    uint8_t *trig; // Trigger array (size N_TRIGS)
};
```
Platform CV/TRIG sizes: V1/V2=4/2, STR=8/2, MK2=22/12, BBA=90/40.

## SoundProcessor Base Class
```cpp
class ctagSoundProcessor {
    virtual void Process(const ProcessData &) = 0;
    virtual void Init(std::size_t blockSize, void *blockPtr) = 0;
    void SetParamValue(id, key, val);
    const char *GetCStrJSONParamSpecs();
};
```
Memory via arena allocator (`ctagSPAllocator`) to prevent fragmentation.

## Existing Internal Modulation Helpers
- `ctagSineSource` — quadrature sine oscillator (sin+cos)
- `ctagADEnv` — Attack-Decay envelope with loop
- `ctagADSREnv` — Full ADSR envelope
- `ctagDecay` — Exponential decay smoothing
- `ctagTimer` — Timeout timer
- `ctagRollingAverage` — Smoothing utility
- `CVtranscoder` — Curve shaping (log/exp)
