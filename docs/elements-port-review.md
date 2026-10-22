# Elements Port Review Brief

## Scope
Review the Elements (Mutable Instruments) port to CTAG TBD, located in:
- `components/ctagSoundProcessor/ctagSoundProcessorElements.cpp`
- `components/ctagSoundProcessor/ctagSoundProcessorElements.hpp`
- `components/mutable/eurorack/elements/` (DSP core)
- `spiffs_image/data/sp/mui-Elements.jsn`, `mp-Elements.jsn`

## Known Issues
1. Sound is quiet and distorted (even single-core, 12 modes).
2. `play` param shows only OFF, doesn't trigger on click (momentary button needed).
3. Resonator models 0 (MODAL) and 2 (STRINGS) produce no audio; only model 1 (STRING) works.
4. Incoming audio excitation (External IN 1/2) may not work.
5. UI responsiveness in VALUEEDIT mode (encoder keeps changing value after release).
6. Parameter naming/controls inconsistent with original Elements manual.

## Original Elements Reference
Manual: https://mutable-instruments.net/modules/elements/manual/
Key params: Coarse/Fine frequency, FM, Geometry, Brightness, Damping, Position, Space, Contour, Bow/Blow/Strike levels+timbres, Air flow, Mallet type, Strength, Gate, Easter egg.

## Port Details
- Gain: 0.125 master (in Process, line ~180).
- Resonator modes: originally 52, reduced to 12 for CPU, currently 12.
- Sample rate: 44.1 kHz (original 32 kHz).
- Incoming audio: left→strike_in, right→blow_in.
- Default gate: 1 (set recently).
- Default resonator_model: 1 (STRING).

## Review Tasks
1. Verify the port matches original Elements behavior (sound, params, CV).
2. Check if 0.125 gain is correct or should be different.
3. Check if models 0/2 should produce audio (maybe need specific excitation).
4. Verify incoming audio excitation works.
5. Check UI param naming against manual.
6. Test CPU budget (audioCycles) for Elements.
7. Verify the momentary play button works in UI.

## CPU Measurement
Use the debug endpoint `/debug/getAudioStats` for `audioCycles`, `cpuOverload`.
Elements does not have helper task instrumentation yet — add PERF_MEASURE if needed.

## Files to Review
- `components/ctagSoundProcessor/ctagSoundProcessorElements.cpp` (Process, Init, knowYourself)
- `components/ctagSoundProcessor/ctagSoundProcessorElements.hpp`
- `spiffs_image/data/sp/mui-Elements.jsn` (UI params)
- `spiffs_image/data/sp/mp-Elements.jsn` (default preset)
- `components/mutable/eurorack/elements/dsp/voice.cc` (mode count)
- `components/mutable/eurorack/elements/dsp/part.cc` (DSP core)