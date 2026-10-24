# Elements Port Review Brief

## Scope
Review the Elements (Mutable Instruments) port to CTAG TBD, located in:
- `components/ctagSoundProcessor/ctagSoundProcessorElements.cpp`
- `components/ctagSoundProcessor/ctagSoundProcessorElements.hpp`
- `components/mutable/eurorack/elements/` (DSP core)
- `spiffs_image/data/sp/mui-Elements.jsn`, `mp-Elements.jsn`

## Status (2026-10-23)

Review completed against upstream `pichenettes/eurorack@f4cfa76`; verified
findings and harness evidence are in `docs/elements-port-review-findings.md`.

- Brief issues 3 (models 0/2 silent) and 4 (external input) did not reproduce in
  the host harness: all three models produce signal and external bursts reach the
  resonator.
- Open findings: pitch +5.53 semitones (F1), dead `strength`/`play` params (F2),
  uninitialized CV/trig slot indices (F3), extra 0.125 master gain (F4).
- CPU measurement is wired: see "CPU Measurement" below and `doc/elements-port.md`
  for the second-core outcome (helper over budget; auto fallback to single-core
  12 modes).
- UI-layer items (VALUEEDIT encoder, naming, momentary presentation) are not DSP
  review items.

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
- Gain: 0.125 master gain, applied on the audio core on both the helper and single-core paths (see findings F4).
- Resonator modes: originally 52, reduced to 12 for single core. A second-core helper offload (2026-10) auto-falls back to single-core 12 modes; see `doc/elements-port.md`.
- Sample rate: 44.1 kHz (original 32 kHz). The DSP constant was updated, but the MIDI-to-frequency LUTs were not (findings F1).
- Incoming audio: left→strike_in, right→blow_in.
- Default gate: 1 (causes one strike at boot).
- Default resonator_model: 1 (STRING) in the declaration, but effectively follows `data.cv[0]` because the CV index is uninitialized (findings F3).

## Review Tasks
1. Verify the port matches original Elements behavior (sound, params, CV). — done; findings F1–F4.
2. Check if 0.125 gain is correct or should be different. — done; F4 (open).
3. Check if models 0/2 should produce audio (maybe need specific excitation). — not reproduced; all models produce signal.
4. Verify incoming audio excitation works. — not reproduced; external burst reaches the resonator.
5. Check UI param naming against manual. — out of DSP scope.
6. Test CPU budget (audioCycles) for Elements. — done; see CPU Measurement and `doc/elements-port.md`.
7. Verify the momentary play button works in UI. — UI handles it; the wrapper never maps `play` (F2).

## CPU Measurement

`/debug/getAudioStats` reports `audioCycles`/`cpuOverload` plus the Elements
helper fields: `elementsHelperCycles`, `elementsHelperMaxCycles`,
`elementsHelperOverload`, `elementsHelperStackHighWater` (`-1` = retired),
`elementsHelperResolution`, `elementsHelperMisses`,
`elementsHelperModeOverride`. `/debug/setElementsMode{"value":12..56}` pins a
mode count for measurement (`<= 0` restores the auto ladder).

## Files to Review
- `components/ctagSoundProcessor/ctagSoundProcessorElements.cpp` (Process, Init, knowYourself)
- `components/ctagSoundProcessor/ctagSoundProcessorElements.hpp`
- `spiffs_image/data/sp/mui-Elements.jsn` (UI params)
- `spiffs_image/data/sp/mp-Elements.jsn` (default preset)
- `components/mutable/eurorack/elements/dsp/voice.cc` (mode count)
- `components/mutable/eurorack/elements/dsp/part.cc` (DSP core)