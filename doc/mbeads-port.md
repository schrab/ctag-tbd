# Mbeads — Port Plan (Mutable Instruments Beads → CTAG TBD)

Status: **v1 implemented (Tasks 1–3); host validation green; hardware CPU/stack
measurement pending user approval to flash.**

Confirmed v1 scope decisions:

- **Media modes:** Digital (44.1 kHz, 16-bit) and Vintage (22.05 kHz, 12-bit).
  Tape modes are deferred.
- **Attenuverters:** all four exposed (Time/Size/Shape/Pitch Mod).
- **Media switching:** runtime switching accepted, with a short mute.
- **Delay mode:** kept, subject to the CPU budget.
- **Synth mode:** out of scope; `wav_integrated_waves` is not compiled.
  `lut_sine` is kept because delay mode needs it.

## Verdict

**Portable, conditionally.** The Beads DSP is self-contained, already present in
`components/mutable/eurorack/beads`, and its memory footprint fits the TBD
hardware. The two real unknowns are:

1. **CPU** — Beads was written for a 400 MHz STM32H743; TBD has a 240 MHz ESP32.
2. **PSRAM behaviour** — the 768 KB granular delay buffer lives in PSRAM and is
   accessed at random grain positions.

The closest precedent, the **Claude** port of Clouds, proves that a granular
engine with a PSRAM sample buffer and a Dattorro reverb already runs on this
hardware (28 grains stereo, 44.1 kHz). Mbeads is larger (768 KB vs ~64 KB of
sample memory) and has more grains (32), so CPU and cache must be **measured
before committing to full fidelity**. The plan below makes that measurement the
first gate and defines fallback tiers.

No blocker was found in memory, flash, audio API, or parameter model.

---

## 1. What Beads is

Beads is a stereo granular texture processor:

- 4-second delay/granular buffer, 16-bit stereo (768,000 bytes nominal).
- Up to 32 simultaneous grains, Hermite interpolation, random panning.
- Grain parameters: Time, Size, Shape, Pitch (each with a bipolar
  attenuverter that also controls internal randomization), Density.
- Mix controls: Feedback, Dry/Wet, Reverb.
- 4 media/quality modes: Dirty Tape (8-bit µ-law), Clean Tape (12-bit,
  half-rate), Vintage Digital (12-bit), Digital (16-bit).
- Delay mode when Size is at maximum (`size > 0.99`): 8-tap multitap delay.
- Dattorro reverb (32,768 × 16-bit = 64 KB).
- Freeze and Seed (grain trigger) controls.
- Built-in wavetable synth when no audio input is present (**out of scope for
  v1** — see §5.5).

Upstream files used by the port:

```
beads/dsp/processor.{h,cc}          whole FX chain
beads/dsp/granular_processor.h      granular/delay engine, 4 template variants
beads/dsp/granular_player.h         32-grain scheduler
beads/dsp/delay_granular_player.h   delay-line grain player
beads/dsp/delay_grain.h             per-grain render
beads/dsp/delay_line.h              16/12/8-bit banked delay line
beads/dsp/trigger_generator.{h,cc}  grain trigger clock
beads/dsp/delay_controller.{h,cc}   delay mode
beads/dsp/multitap_delay.h          delay mode taps
beads/dsp/fx/reverb.h               Dattorro reverb
beads/dsp/fx/wow_and_flutter.h      tape wow/flutter
beads/dsp/mu_law.{h,cc}             8-bit companding
beads/dsp/sine_oscillator.h         uses lut_sine
beads/resources.cc                  lut_sine (wavetables only needed by synth)
```

The Beads UI, codec, CV reader, settings, calibration, AGC, factory test,
background buffer backup, **synth mode and its wavetables** are **not** ported
in v1.

---

## 2. Target hardware constraints

| Item | TBD BBA (current target) |
|---|---|
| MCU | ESP32, 240 MHz, dual-core Xtensa LX6, single-precision FPU |
| Audio | 44,100 Hz, 32-sample blocks (0.726 ms), budget 174,150 cycles/block |
| Internal DRAM | ~22 KB free after audio init |
| PSRAM | 8 MB, 80 MHz quad, cached; ~3.4 MB free with no plugin loaded |
| Audio task stack | **4,096 bytes** |
| App partition | 3.5 MB; current image **3,233,968 bytes (3.23 MB, `0x3158b0`)** |
| Codec | 44.1 kHz fixed; no runtime sample-rate change |

Relevant platform facts:

- `data.buf` is interleaved stereo `float` (`L,R,L,R...`), `bufSz = 32`.
- A stereo plugin (`isStereo = true`) receives both channels in one instance.
- The sound-processor arena is 112 KB in PSRAM; plugins may use it for state.
- TBD trigger values are `0/1` with `1 = active` (`ctagGate16` writes 1 while
  the gate is high).
- CV values are approximately `-1.0 .. +1.0`.

---

## 3. Reference: Claude (Clouds port)

What the existing port teaches:

- **It works at 44.1 kHz.** `claude` passes `data.buf` directly to
  `clouds::GranularProcessor::Process(..., 32)`. Clouds' `sample_rate()` was
  changed to `44100.0f / downsample_factor`.
- **PSRAM granular sample memory is viable.** Claude allocates 112,640 bytes
  of sample memory with `heap_caps_malloc(MALLOC_CAP_SPIRAM)` and uses the
  64 KB arena as FX workspace (reverb, diffuser, correlator).
- **Grain count was tuned empirically for ESP32**: 28 grains for stereo
  high-fidelity, 34 mono high-fidelity, 40 stereo low-fidelity, 54 mono
  low-fidelity. This is the single most useful data point for Mbeads.
- **The glue pattern is proven**: atomics for params, `knowYourself()` maps,
  `LoadPreset(0)`, PSRAM allocation in `Init`, `isStereo = true`, and
  `mui-*.jsn` / `mp-*.jsn` descriptors.
- **Clouds' sample buffer is only ~64 KB**, which fits in the ESP32 data
  cache. Beads' 768 KB does not fit, so cache behaviour must be measured,
  not assumed. However, each active grain reads sequentially, so each grain
  should incur roughly one cache-line miss every 8 output samples (16-bit
  stereo), not one per sample.

Clouds also shows the stack constraint: its engine uses fixed-size member
buffers (`kMaxBlockSize`), while upstream Beads used C99 VLAs sized by the
block. The port replaced those with fixed 32-element members (see §5.3).

---

## 4. Feasibility

### 4.1 Memory

| Allocation | Bytes |
|---|---:|
| Granular delay buffer (3 × 256 KB banks) | 786,432 |
| Reverb buffer (32,768 × uint16) | 65,536 |
| `beads::Processor` object (measured on Xtensa, 32-bit, `-Os`) | 29,496 |
| Block scratch (fixed arrays, included above) | — |
| **Total** | **~881 KB** |

- Fits in PSRAM (~3.4 MB free). The 112 KB sound-processor arena is not
  required for the delay buffer.
- Measured `sizeof(beads::Processor)`: **29,496 bytes** on the Xtensa target
  (`-Os`, `nm` symbol size) and **30,736 bytes** on the x86-64 host (`-O2`).
- Recommendation: allocate delay buffer, reverb and the object from PSRAM
  (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` + `operator new` override, as
  Elements does). This keeps the 112 KB arena free for stereo chaining.
- Fallback if the 768 KB allocation fails: log and fail the load cleanly;
  a reduced 2-second build can be produced later by changing
  `kDelayBufferSizeBits`. Do not silently drop grains.
- PSRAM fragmentation is a risk after many load/unload cycles. Test 100
  load/unload cycles; if it fails, reserve the buffer on first use instead of
  freeing it (documented trade-off).

### 4.2 CPU

| Item | Cycles/block |
|---|---:|
| Total budget | 174,150 |
| Platform overhead (ModEngine, codec, DC cut, noise gate, stereo, SD) | ~36,000 |
| Available for Mbeads DSP | ~138,000 |
| Target for Mbeads DSP (20% margin) | **≤ 130,000** |
| Elements DSP reference (12 modes) | ~147,000 (already over) |
| Clouds/Claude reference | 28 grains stereo runs on this hardware |

**This is the main risk.** Beads targets a 400 MHz H743; the ESP32 is 240 MHz
with a weaker memory system. Mitigations, in order:

1. Reduce `NUM_GRAINS` from 32 to 24 or 16 (one-line change in
   `delay_granular_player.h`). High-density texture is the first thing to lose.
2. Default to `MEDIA_CLEAN_TAPE` (half-rate engine, 12-bit) or add a
   "Low CPU" quality mode that runs the granular engine at 22.05 kHz
   (`ratio = 2`) while keeping the dry signal at 44.1 kHz.
3. Skip the reverb when `reverb == 0` (saves ~35k cycles/block when off).
4. Use linear instead of Hermite interpolation (quality option).
5. Mono delay line (halves grain sample loads); only as a last resort.

**Gate:** do not build the full parameter UI until the benchmark firmware
measures the real cost. Target no continuous CPU-overload indicator at default
settings.

### 4.3 Flash

Measured on the completed v1 port (`feat/mbeads`, ESP-IDF 5.5.4):

- `build/ctag-tbd.bin`: **0x3158b0 = 3,233,968 bytes**.
- Smallest app partition: `0x380000` = 3,670,016 bytes.
- Free: **0x6a750 = 436,048 bytes (12%)**.
- Beads DSP baseline Task 1 image: `0x308df0`. Linking the DSP in Task 2 added
  49,408 bytes; the Task 3 instrumentation added 2,496 bytes. Net +51,904
  bytes, slightly above the 30–50 KB estimate below.

The Beads DSP code is small; the only large asset is `resources.cc` (~450 KB
source, ~100 KB of wavetables). The wavetables are **only used by the synth
mode, which is out of scope for v1**, so `beads/resources_min.cc` keeps only
`lut_sine`. That drops 49,920 `int16_t` values (99,840 bytes) of dead flash.
Keep an eye on the 3.5 MB limit if other plugins grow.

### 4.4 Stack

The **pre-port** `Processor::Process` allocated its scratch buffers as C99 VLAs
on the caller's stack:

- `TriggerGenerator::Output triggers_buffer[32]` → 512 B
- `DelayController::Output taps_buffer[32]` → 2,176 B
- `bool gate_out[32]` + `FloatFrame dry_signal[32]` → 288 B

plus ~1.8 KB in the half-rate path of `GranularProcessor::Process`, i.e. ~4.8 KB
total. That would not have fit the 4,096-byte audio task stack. The port moved
`triggers_buffer_`, `taps_buffer_`, `gate_out_` and `dry_signal_` into fixed
32-element members of `beads::Processor` (which lives in PSRAM), removing ~3 KB
from the audio task stack. The half-rate path (Vintage/Clean/Dirty) still keeps
~1.8 KB of fixed locals on the audio task stack, so the hardware high-water mark
must still be measured before deciding whether to hoist those arrays or raise
the stack to 6,144 bytes. The default Digital path does not take that branch.

---

## 5. Required adaptations

### 5.1 Sample rate

TBD is fixed at 44.1 kHz; Beads hardcodes 48/32/24 kHz. Change
`beads::Processor::Init` to accept the host rate:

| Media | Host rate | Engine rate | Ratio |
|---|---|---|---|
| Digital (16-bit) | 44,100 | 44,100 | 1 |
| Vintage (12-bit) | 44,100 | 22,050 | 2 |
| Clean Tape (12-bit, deferred) | 44,100 | 22,050 | 2 |
| Dirty Tape (8-bit, deferred) | 44,100 | 22,050 | 2 |

- v1 maps the UI `media` param as `0 = Digital`, `1 = Vintage`.
- Vintage uses the half-rate 12-bit path, as agreed (`44.1/22.05`).
- Change the vintage template from `ratio = 1` to `ratio = 2`; the dirty-tape
  template also moves to `ratio = 2` for the later tape modes.
- Change `wow_and_flutter_.Init(24000)` to the host rate (dirty tape only).
- Change `reverb_.Init(reverb_buffer, 48000)` to the host rate. Reverb and the
  output limiters run at 44.1 kHz even for half-rate media.
- Initialize `trigger_generator_` and `delay_controller_` at the host rate, not
  the engine rate: `Processor::Process` steps them with 32 host-rate samples
  and the half-rate engine downsamples their outputs afterward.
- Time/pitch are relative, so the 48→44.1 kHz change shifts delay time by ~9%;
  buffer is 4.35 s instead of 4 s. Acceptable.

### 5.2 In-place processing

The pre-port `beads::Processor::Process` captured `dry_signal` **after** the wet
processor had written `out`, so `in == out` made the dry signal become the wet
signal. The port computes the dry signal **before** the wet engine runs, so
in-place processing is now safe (covered by an `in == out` host test). The glue
still uses separate `FloatFrame in_[32]` and `FloatFrame out_[32]` members for
clarity and because it copies from/to the interleaved `data.buf`.

### 5.3 Fixed block size

The port replaced the VLAs with fixed 32-element members in:

- `beads::Processor` (`triggers_buffer_`, `taps_buffer_`, `gate_out_`,
  `dry_signal_`)
- `beads::GranularProcessor` half-rate path (`d_freeze`, `d_triggers`,
  `d_taps`, `d_in`, `d_out`, `downsampled`, `upsampled`)

Both `Processor::Process` and the half-rate branch clamp `size` to 32; TBD
always passes 32.

### 5.4 Media switching

`Processor::Configure(media, mono)` calls
`GranularProcessor::Reset(mono)`, which clears the delay line of the **active**
channel layout only and resets the grain player and multitap delay. Both the
mono and the stereo layout interpret the same 768 KB of buffer memory with a
different item format, so one pass is enough: the one-time clear is **768 KB**
(clearing both layouts, as upstream did, would be 1.5 MB for no benefit). At
typical PSRAM write bandwidth this is in the order of 20 ms, and it runs from
the audio task when the `media` parameter changes.

The glue calls `configure` only on an actual media change, before processing the
block, and mutes the current and the following block (2 × 32 samples ≈ 1.5 ms).
The buffer clear can exceed the mute, so the switch is heard as a short one-time
gap/glitch. This is the accepted v1 behaviour; the parameter is not meant to be
modulated at audio rate. Options if that ever becomes a problem:

- **Better:** defer the switch to a control-rate flag and mute 2 blocks before
  and after, off the audio task.
- **Safest:** fix media at load time, add runtime switching in a later phase.

Do **not** skip the delay-line clear and keep stale data: a format change
(16-bit ↔ 12-bit ↔ 8-bit) makes the old buffer sound like noise.

Note: plugin load still clears more than 768 KB. `beads::Processor::Init`
initialises all four media templates, and each `DelayLine::Init` clears its
mono and stereo line, i.e. ~6.3 MB, plus one `Configure` clear. It happens once
at load in the UI task, outside the audio callback (~0.2 s), and is a candidate
for a later optimisation (`DelayLine::Init` could skip the inactive layout).

### 5.5 Resource trimming

**Confirmed v1 decision:** drop the synth wavetables. Create a minimal
`beads/resources_min.cc` (rather than compiling the generated
`beads/resources.cc`) that keeps only `lut_sine` and the lookup-table arrays.

- `wav_integrated_waves` is 49,920 `int16_t` values = **99,840 bytes**.
- Its only readers are `wavetable_grain.h` and `wavetable_voice.h`, used by
  `synth.cc`/`synth.h`.
- The synth mode lives in `beads.cc` (`normalization_detector` + `Synth`) and
  is not part of `beads::Processor`, so those symbols would be linked but
  never executed.
- `lut_sine` (used by `sine_oscillator.h` for delay mode) and the lookup-table
  arrays stay.

This removes a real Beads feature (the built-in synth when no input is
present), not a granular-engine component. It can be restored later by porting
`synth.cc` and the wavetable voices.

### 5.6 CMake

Add to `components/mutable/CMakeLists.txt`:

```
eurorack/beads/dsp/processor.cc
eurorack/beads/dsp/trigger_generator.cc
eurorack/beads/dsp/delay_controller.cc
eurorack/beads/dsp/delay_clock.cc
eurorack/beads/dsp/mu_law.cc
eurorack/beads/resources_min.cc
```

Do not compile `beads.cc`, `ui.cc`, `drivers/`, `settings.cc`, `synth.cc` or
the wavetable player.

---

## 6. Parameters and UI

Proposed plugin id: **`Mbeads`** (class `ctagSoundProcessorMbeads`).

| Param | Type | Range | Notes |
|---|---|---|---|
| `time` | int | 0..4095 | grain read position |
| `time_mod` | int | 0..4095 | bipolar attenuverter, 2047 ≈ 0 |
| `size` | int | 0..4095 | grain size; > 0.99 enters delay mode |
| `size_mod` | int | 0..4095 | bipolar attenuverter |
| `shape` | int | 0..4095 | grain envelope |
| `shape_mod` | int | 0..4095 | bipolar attenuverter |
| `pitch` | int | -24..24 | semitones (Beads quantizes its knob; TBD continuous) |
| `pitch_mod` | int | 0..4095 | bipolar attenuverter |
| `density` | int | 0..4095 | grain rate / divider |
| `feedback` | int | 0..4095 | mix |
| `dry_wet` | int | 0..4095 | mix |
| `reverb` | int | 0..4095 | mix |
| `media` | int | 0..1 | 0 Digital (44.1 kHz), 1 Vintage (22.05 kHz) |
| `freeze` | bool | — | trig-capable |
| `seed` | bool | — | manual grain trigger, trig-capable |

CV mapping:

- `time`/`size`/`shape` CV: `data.cv * 1.6` (matches Beads' ±1.6 CV range).
- `pitch` CV: `data.cv * 60` semitones.
- `density` CV: `data.cv` direct.
- `feedback`/`dry_wet`/`reverb` CV: add `data.cv`, clamp to 0..1.
- `has_cv = (cv_slot != -1)`.
- **Attenuverter semantics:** in Beads, the amount knob is the CV depth when
  CV is connected, and the internal-randomization depth when it is not. Keep
  that behaviour and label the params "Time Mod", "Size Mod", etc. in the UI.
  Default amounts to 0 (no randomization); users raise them for Beads' random
  texture or to make CV effective.
- Default `pitch = 0`, `media = 0` (Digital), `dry_wet = 2048`,
  `feedback = 0`, `reverb = 0`, `density = 3072`, `seed = 1`,
  `time/size/shape = 2048`, all four `*_mod` params = 2047 (center, no
  modulation). Beads at density 2048 sits in the manual-tap zone and needs a
  seed gate; `density = 3072` + `seed = 1` makes the granular engine audible
  on a fresh load. Both parameters remain fully exposed.

JSON descriptors:

- `spiffs_image/data/sp/mui-Mbeads.jsn` — a flat `params` array; there is no
  group nesting.
- `spiffs_image/data/sp/mp-Mbeads.jsn` — default patch.

---

## 7. Implementation plan

### Phase 0 — Build + host validation (no UI)

1. Add the Beads DSP sources to `components/mutable/CMakeLists.txt`.
2. Add `beads/resources_min.cc` with `lut_sine` only; do not compile the
   generated `resources.cc`/wavetables.
3. Add a tiny host harness (or a simulator scenario) that feeds a WAV through
   `beads::Processor` at 44.1 kHz in 32-sample blocks.
4. Verify: no crashes, sensible granular output, freeze/seed, reverb tail.
5. Check `sizeof(beads::Processor)` and the PSRAM allocation math.

**Exit:** DSP compiles and produces audio on host.

### Phase 1 — Minimal Mbeads glue + hardware benchmark

1. Create `ctagSoundProcessorMbeads.{hpp,cpp}`:
   - PSRAM `operator new`/`delete`.
   - `isStereo = true`, `id = "Mbeads"`.
   - Allocate 786,432-byte delay buffer and 65,536-byte reverb in PSRAM.
   - 44.1 kHz `Processor::Init`, fixed block arrays, separate in/out frames.
   - Core params only: time, size, shape, pitch, density, feedback, dry_wet,
     reverb, freeze, seed, media (runtime-switchable Digital/Vintage), and all
     four attenuverters.
2. Add `PERF_MEASURE("Mbeads")` around `processor.Process`, plus sections for
   granular, reverb and delay mode. Enable `PerfMonitor` logging.
3. Flash and measure at:
   - default patch, density 50%;
   - density 100%, reverb 0;
   - density 100%, reverb 100%;
   - delay mode (size = max);
   - Digital (44.1 kHz) and Vintage (22.05 kHz).
4. Raise the audio task stack to 6,144 only if the high-water mark requires it.

**Exit (go/no-go):**
- Default: no continuous CPU overload, Mbeads DSP ≤ 130k cycles/block.
- Extreme: ≤ 165k cycles/block, momentary overload indicator acceptable.
- If over: reduce grains to 24/16, default to half-rate, and re-measure.
- If still over after those cuts: stop and reassess (Mbeads Lite or no port).

### Phase 2 — Full controls + UI

1. Finish all params, CV/trig maps, `knowYourself()`.
2. Add `mui-Mbeads.jsn` and `mp-Mbeads.jsn`.
3. Verify preset save/load, MIDI CC/CV slots, freeze/seed trig polarity.
4. Verify dry/wet gain staging and limiter behaviour with a hot input.

**Exit:** plugin appears in the browser, loads as stereo, all params respond,
presets persist.

### Phase 3 — Deferred tape modes

1. Extend the UI `media` range to expose Clean Tape and Dirty Tape.
2. Validate the half-rate 12-bit and 8-bit paths, wow/flutter, mu-law, SRC.
3. Measure each mode and document the CPU cost.

**Exit:** all four modes work; switching does not produce sustained noise.
v1 ships only Digital and Vintage.

### Phase 4 — Delay mode

1. Validate `size > 0.99` → `DelayController` + `MultitapDelay`.
2. Measure 8-tap cost; keep if within budget, otherwise gate it behind a
   "Delay mode" bool or document it as a high-CPU mode.

### Phase 5 — Polish

1. Write `doc/mbeads-port.md` final version (measurements, CPU table).
2. Update `versions.md` and, if useful, `AGENTS.md` with the PSRAM/stack
   constraints.
3. Run the stability suite (§8).
4. Commit the submodule patch separately from the ctag changes.

---

## 8. Test plan

Functional:

- Dry signal passes through unchanged at `dry_wet = 0`.
- Grain pitch tracks the pitch param (measure with a sine sweep / FFT).
- Time/size/shape/density behave monotonically and match Beads' character.
- Freeze holds the buffer; unfreeze crossfades without a click.
- Seed triggers grains at the density rate.
- Feedback is stable at 100% (limiter/soft clip).
- Reverb tail decays; no runaway.
- Delay mode produces 8 taps and does not read outside the buffer.
- Media modes: 16-bit, 12-bit, 8-bit µ-law, half-rate paths.
- CV/trig polarity: gate high = 1 = active; freeze when high; seed on rising
  edge.

Stress / stability:

- 30 minutes with density/reverb/feedback at maximum.
- Rapid freeze/seed toggling.
- Media switch while audio is running (if enabled).
- Load/unload Mbeads 100 times; check PSRAM free size and fragmentation.
- Stereo chain Mbeads → another plugin (if chain mode is enabled).
- Power cycle with a saved Mbeads preset.
- Audio task stack high-water mark under worst-case density.
- No watchdog resets, no heap corruption, no `assert` in `DelayLine`.

Measurement:

- `PERF_MEASURE` sections: total, granular, reverb, delay mode, SRC.
- UART log of cycles/block and max spike over 10 s.
- CPU overload indicator (`drawPanelBar`) at top-right.

---

## 9. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| CPU over budget at 32 grains | High | Benchmark gate; drop to 24/16 grains; half-rate default; skip reverb when off |
| PSRAM random-access stalls | Medium | Sequential grain reads; measure cache misses; reduce grains/pitch extremes |
| Audio task stack overflow (pre-port VLAs, ~4.8 KB) | High (certain pre-port) | `Processor` scratch moved to fixed 32-element PSRAM members; half-rate path still ~1.8 KB, high-water measurement pending |
| 768 KB PSRAM allocation fails/fragments | Medium | Allocate at load; test 100 cycles; reserve on first use if needed |
| Media switch clears 768 KB in audio task | Medium | Mute + deferred switch, or fix media at load in v1 |
| App partition overflow | Low | Trim synth wavetables; monitor 3.5 MB limit |
| `processor.Process` in-place dry/wet bug (pre-port, dry captured after wet) | High if missed | Dry signal captured before the wet write; glue also uses separate buffers |
| Attenuverter/CV semantics confusing | Medium | UI labels "Time Mod" etc.; document in hint |
| Trig polarity mismatch | Low | Use `== 1` = active; test against gate generator |
| Flash/IRAM cache pressure | Low | Measure; keep hot code in flash like Elements |

---

## 10. Decisions

**Confirmed:**

- **Synth mode / wavetables:** out of scope for v1. `wav_integrated_waves` is
  not compiled; `lut_sine` stays for delay mode.
- **Media modes:** v1 ships **Digital (44.1 kHz, 16-bit)** and **Vintage
  (22.05 kHz, 12-bit)**. Tape modes deferred.
- **Attenuverters:** all four exposed (`time_mod`, `size_mod`, `shape_mod`,
  `pitch_mod`).
- **Runtime media switching:** accepted, with a short mute and a one-time
  buffer clear.
- **Delay mode:** kept, subject to the Phase 1 CPU gate.

Implementation is tracked in `doc/mbeads-implementation-plan.md`.

---

## 11. Implementation status and validation

Status of the v1 port (Tasks 1–3 complete on branch `feat/mbeads`). Everything
below is **measured**, except the hardware CPU/stack figures, which are
explicitly pending.

| Item | Measured value |
|---|---|
| `sizeof(beads::Processor)`, Xtensa, `-Os` | **29,496 bytes** (`nm` symbol size) |
| `sizeof(beads::Processor)`, x86-64 host, `-O2` | **30,736 bytes** (printed by the host test) |
| Delay buffer / reverb buffer | 786,432 / 65,536 bytes (PSRAM) |
| PSRAM per Mbeads instance | ~861 KB (buffers + object) |
| `build/ctag-tbd.bin` | **0x3158b0 = 3,233,968 bytes** |
| Free in app partition (0x380000) | **0x6a750 = 436,048 bytes (12%)** |
| Media-switch clear | **768 KB** (one channel layout), then a short mute |
| Host core test | **PASS** |
| Hardware CPU / stack | **not measured — flashing not yet approved** |

### Host validation

```
$ bash tests/host/run_mbeads_tests.sh
...
  [ ok ] Digital delivers ~130 grains/s through the output gate
  [ ok ] Vintage delivers ~130 grains/s through the output gate
  [ ok ] Digital and Vintage trigger the same grain rate
  [ ok ] granular engine alone is audible (no reverb, fully wet)
  [ ok ] default patch grain settings are audible (wet only, no reverb)
  default patch wet rms = 0.037146
  wet rms = 0.136539
...
PASS: mbeads core
```

The test builds the real Beads sources at 44.1 kHz in fixed 32-sample blocks and
covers: media rate table (44.1k/22.05k, ratio 1/2), trigger generator rate
(131 pulses/s), grain-rate integration through
`MixParameters::output_gate` (131 grains/s for both Digital and Vintage,
deterministic at density 0), fully wet granular output with `reverb = 0` and
`dry_wet = 1` (RMS ~0.13, floor 0.01), the shipped default-patch grain settings
(density 3072, seed 1, time 0.5), in-place `in == out` safety, freeze/seed, and
Clean/Dirty Tape smoke runs.
The existing modulation suite (`bash tests/host/run_tests.sh`) still passes.

### Default patch rationale

`spiffs_image/data/sp/mp-Mbeads.jsn` and the parameter atomics in
`ctagSoundProcessorMbeads.hpp` ship:

- `density = 3072` (0.75): above Beads' manual-tap zone (0.48..0.52), so the
  grain clock free-runs.
- `seed = 1`: the seed button holds the trigger generator's gate high. Upstream
  Beads only advances the density clock while the seed gate is high, so with
  the previous `density = 2048` / `seed = 0` defaults the wet path was silent on
  a fresh load (dry still passed).
- Everything else stays neutral: `time/size/shape = 2048`, all `*_mod = 2047`
  (centre), `pitch = 0`, `feedback = 0`, `reverb = 0`, `dry_wet = 2048`,
  `media = 0` (Digital), `freeze = 0`.

The host test asserts the shipped grain settings (density 3072, seed 1,
time 0.5 = 2048) are audible (wet RMS 0.037 with no reverb), and both parameters
remain fully exposed in the UI.

### Media-switch cost

`GranularProcessor::Reset(bool mono)` clears only the delay line of the active
channel layout (mono or stereo). Both layouts share the same 768 KB of buffer
memory with different item formats, so a single pass leaves the buffer clean for
either interpretation; the previous upstream `Reset()` cleared both (1.5 MB) for
no benefit. A media change therefore costs a **one-time 768 KB PSRAM clear**
(order 20 ms) plus a **two-block mute** (~1.5 ms) applied by the glue. The clear
can outlast the mute, so the switch is an accepted short one-time gap, not a
glitch-free crossfade. The parameter is not meant to be modulated at audio rate.

### Hardware measurement pending

Flashing has **not** been approved, so no CPU, stack or PSRAM-bandwidth numbers
exist yet. `/dev/ttyUSB0` exists on the development machine. When approved:

```sh
source /home/schrab/esp/esp-idf-v5.5.4/export.sh

# Full flash (bootloader + partition table + app + storage/SPIFFS)
python -m esptool --chip esp32 -b 460800 --port /dev/ttyUSB0 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xd000 build/ota_data_initial.bin \
  0x10000 build/ctag-tbd.bin \
  0x710000 build/storage.bin

# UART log capture (PERF_MONITOR logs every 5 s)
stty -F /dev/ttyUSB0 115200 raw -echo
timeout 60 cat /dev/ttyUSB0 2>/dev/null | tee /tmp/uart-mbeads.log
```

Expected lines, repeated every 5 s while Mbeads is loaded:

```
I (....) PERF_MONITOR: === Performance Statistics (CPU: 240 MHz) ===
I (....) PERF_MONITOR:   Mbeads                  : avg=... µs (... cyc)  min=... max=... n=...
I (....) PERF_MONITOR:   MbeadsGranular          : avg=... µs (... cyc)  min=... max=... n=...
```

The logging task runs on core 1 at priority 5, far below the audio task
(core 1, priority 23). Measurement checklist:

1. Default patch, 30 s: `Mbeads`/`MbeadsGranular` average and max.
2. Density 100%, reverb 0; density 100%, reverb 100%.
3. Delay mode (`size` at maximum).
4. Digital (44.1 kHz) and Vintage (22.05 kHz).
5. Audio-task stack high-water mark: not currently logged. `audioTaskH` is
   private to `SoundProcessorManager`; either add a getter in `SPManager` or log
   `uxTaskGetStackHighWaterMark(nullptr)` from inside the audio task, then check
   the Vintage/Dirty half-rate path (Task 1 flagged ~1.8 KB of fixed locals
   there).

Go/no-go thresholds from §4.2: no continuous CPU-overload indicator at the
default patch, `Mbeads` DSP average ≤ 130k cycles/block, extreme settings
≤ 165k.
