# Elements Port Review — Findings

Review of the Elements port (`components/ctagSoundProcessor/ctagSoundProcessorElements.*`,
`components/mutable/eurorack/elements/`) against upstream
`pichenettes/eurorack` commit `f4cfa76`.

Method: source diff of the vendored DSP tree against the upstream checkout, plus
a host harness (`/tmp/opencode/elements_harness.cpp`; rebuild against the current
vendored DSP before reuse) that mirrors the TBD wrapper mapping and runs
`elements::Part` with gate held/pulsed, all three resonator models,
zero-initialized and raw-PSRAM (`0xAA`) object memory, and an external-input
burst.

Second-core offload work (2026-10-22/23) is out of scope for this review: it did
not change the DSP or the wrapper's parameter mapping.

## F1 — Global pitch offset: +5.53 semitones (open)

Symptom: resonator pitch is sharp by exactly 44.1/32 = 1.378 (+5.53 semitones).

Cause: `elements/dsp/dsp.h` was changed to `kSampleRate = 44100.0f` (upstream:
32000.0f), but the MIDI-to-frequency LUTs used by `Part::Process`
(`lut_midi_to_f_high` / `lut_midi_to_f_low` in `elements/resources.cc`) were not
regenerated. They are byte-identical to upstream, which is generated for
32 kHz by `elements/resources/lookup_tables.py` (`SAMPLE_RATE = 32000`), and the
values are normalized cycles/sample. At 44.1 kHz every note therefore plays
44.1/32 too high.

Evidence (host harness):

```
lut_midi_to_f_high[69+48] * 44100 = 606.375 Hz   (expected 440.000)
lut_midi_to_f_high[69+48] * 32000 = 440.000 Hz
```

Fix direction: regenerate `resources.cc` with `SAMPLE_RATE = 44100` (and re-check
any other sample-rate-dependent tables). Not done in this branch.

## F2 — `strength` and `play` are dead parameters (resolved)

- Fixed: `strength` mapped in `knowYourself()` (`pMapPar`/`pMapCv`) and applied
  in `Process()`.
- `play` mapped in `knowYourself()` (`pMapPar`/`pMapTrig`) and integrated with
  `perfState.gate`:
  - Value 2 (short-press OK on UI): triggers a clean ~14.5 ms momentary pulse.
  - Value 1 (right encoder on UI): latched ON (continuous drone/sustain).
  - Value 0 (right encoder or short-press when ON): latched OFF.
  - Long-press OK opens modulation mapping (`MODE_MAP`) to map external triggers.
- `gate` preset fixed to start at `0` (was stuck at `1` at boot, preventing
  rising-edge strikes).

## F3 — CV/trig slot indices are uninitialized (resolved)

- Fixed: All `cv_*` and `trig_*` atomics in `ctagSoundProcessorElements.hpp`
  explicitly initialized to `-1`.
- `resonator_model` added as a separate menu item in the Resonator group
  (0 = Modal, 1 = String, 2 = Strings).
- `easter_egg` is purely a toggle switch in the menu (no trigger mapping),
  defaulting to `0` (Modal resonator active at boot). Trigger mapping logic
  for active-low CTAG triggers (`!= 1`) applied to `gate` and `play`.

## F4 — Extra 0.125 master gain downstream of SoftLimit (open; intentional)

The wrapper multiplies the `Part::Process` output by `0.125` before writing the
codec buffer, on both the helper mix and the single-core path. Upstream applies
`SoftLimit(main/aux)` and outputs directly, so the port is ~18 dB below stock
Elements. SoftLimit is level-dependent (`x*(27+x²)/(27+9x²)`), so the extra gain
also changes its drive point rather than only the loudness. The gain was added
to stop int16 hard clipping from coherent 52-mode summation; removing it needs a
proper limiter (out of scope for the second-core task).

## Brief issues that did not reproduce

Host harness, `strength = 0.5`, gate held and pulsed, zero-init and raw-PSRAM
(`0xAA`) object memory:

- Models 0 (MODAL), 1 (STRING) and 2 (STRINGS) all produce signal:
  gate-held RMS `0.00458` / `0.00979` / `0.00656`, `nan = 0`. No "models 0/2
  silent" failure at the DSP level.
- External excitation reaches the resonator: STRING with an external burst RMS
  `0.01747` vs `0.00979` without it.

## Not covered

- UI-layer items from the brief (VALUEEDIT encoder behaviour, parameter naming,
  momentary-button presentation) are not DSP review items; the missing `play`
  mapping itself is F2.
- The second-core offload and its measured outcome: `doc/elements-port.md`,
  `designs/elements-second-core.md`.
