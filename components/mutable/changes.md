# plaits
- Set dsp/dsp.h sample rates and buffer sizes
- More initializations
# rings
- Set dsp/dsp.h sample rates and buffer sizes
- Added a few const in part.cc, part.h
- More initializations
# clouds
- Sample rate set to 44100Hz
- Initializations
- Max grains adapted to match ESP32 processing power
- Buffer allocations, changed to match available memory on ESP32
- Merge with parasites reverse mode
- Spectral mode not working, would require own FFT due to performance
# general
- stmlib remove TEST ifdef make default standard c api, remove STM special op codes
- stmlib DelayLine with heap buffer
# marbles
- random/output_channel.h: quantizer_[6] -> quantizer_[8] (the BBA MOD menu
  exposes 8 preset scales — 7 diatonic modes + pentatonic; LoadScale(0..7)
  and quantizer_[scale_index_] with index >= 6 overflowed the upstream array
  and froze the DSP)