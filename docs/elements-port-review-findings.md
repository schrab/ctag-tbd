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

## F2 — `strength` and `play` are dead parameters (open)

- `perfState.strength` is hardcoded to `0.5f` in
  `ctagSoundProcessorElements.cpp` (~line 284). Neither `strength` nor `play`
  has a `pMapPar`/`pMapCv` entry, so UI and preset values are ignored. The UI
  already treats `play` as momentary, but nothing consumes it.
- The hardcoded value is also a level difference: the host harness at
  `strength = 1.0` vs `0.5` (STRING model) measures RMS `0.03656` vs `0.00979`
  (-11.4 dB), because strength drives the exciter through a nonlinear accent LUT
  (`voice.cc` `lut_accent_gain_coarse`).

Fix direction: map `strength` (0..1) and `play` (momentary gate) in the wrapper;
choose the default against the upstream module's knob position.

## F3 — CV/trig slot indices are uninitialized (open)

Every `cv_*` / `trig_*` atomic in `ctagSoundProcessorElements.hpp:134-157` is
default-initialized. C++ leaves such atomics indeterminate; on this platform they
read as zero. `Process()` treats any value `!= -1` as an active mapping, so a
parameter without a preset CV/trig index follows `data.cv[0]` / `data.trig[0]`:

- `resonator_model` is declared `= 1` (STRING), but `cv_resonator_model`
  defaults to 0, so `rModel = (int)(floorf(data.cv[0] * 3.f)) % 3` runs and the
  model follows `data.cv[0]` — model 0 (MODAL) at rest.
- `trig_easter_egg` defaults to 0, so the easter egg follows `data.trig[0]`.

Fix direction: initialize all slot-index atomics to `-1`.

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
