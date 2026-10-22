# Mbeads — Port Plan (Mutable Instruments Beads → CTAG TBD)

Status: **v1 implemented, host-validated, and measured on hardware (A1S).**
The Size-dependent CPU overload is fixed. Delay mode is disabled in v1 because
its measured cost exceeds the audio-task budget (see §11).

Confirmed v1 scope decisions:

- **Media modes:** Digital (44.1 kHz, 16-bit) and Vintage (22.05 kHz, 12-bit).
  Tape modes are deferred.
- **Attenuverters:** all four exposed (Time/Size/Shape/Pitch Mod).
- **Media switching:** runtime switching accepted, with a short mute.
- **Delay mode:** **disabled in v1** after hardware measurement. The 8-tap
  pitch-shifting delay remains in the DSP core behind
  `SetDelayModeEnabled(false)` for tests and future work; the plugin never
  enters it. Size at maximum stays in the granular engine.
- **Synth mode:** out of scope; `wav_integrated_waves` is not compiled.
  `lut_sine` is kept because the retained delay-mode DSP needs it.

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
| App partition | 3.5 MB; current image **3,235,904 bytes (3.23 MB, `0x316040`)** |
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
| `beads::Processor` object (measured on x86-64 host, `-O2`; target allocates the same type in PSRAM) | 34,656 |
| Block scratch (fixed arrays, included above) | — |
| **Total** | **~886 KB** |

- Fits in PSRAM (~3.4 MB free). The 112 KB sound-processor arena is not
  required for the delay buffer.
- Measured `sizeof(beads::Processor)`: **34,656 bytes** on the x86-64 host
  (`-O2`, printed by the host test); the host-test guard is 40 KB. The
  cross-target `sizeof` is not printed by the firmware build, but the object is
  allocated in PSRAM and the active-grain list plus `HalfRateScratch` added
  about 2 KB to the earlier 32,784-byte host figure.
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

**This was the main risk.** Beads targets a 400 MHz H743; the ESP32 is 240 MHz
with a weaker memory system. The v1 mitigation, in order of measured impact:

1. **Render only active grains.** A compact per-block list replaces the
   unconditional 32-slot `Render()` loop. Done grains are skipped without a
   function call; newly-started grains refresh the list. Measured saving at the
   default patch: ~23k cycles/block.
2. **Skip the reverb network when its mix is zero.** The Dattorro network
   measured ~43–55k cycles/block. The default patch has `reverb = 0`, so it is
   bypassed; the reverb buffer is cleared at init and on re-enable. The bypass
   is the largest single saving.
3. **Cap concurrent grains.** The plugin admits at most **4 grains** when the
   reverb is off and **2** when it is audible. Triggers that arrive at the cap
   are dropped; lowering the cap fades excess grains out. This keeps the worst
   case inside 174,150 cycles/block without reducing `NUM_GRAINS` or changing
   interpolation quality.
4. Hermite interpolation is retained (no linear-quality fallback was needed),
   and the delay line stays stereo.

The delay-mode path was measured separately and is not within budget: after
skipping zero-gain pitch taps it still costs ~260k total cycles/block at the
default pitch (100% overload). It is therefore disabled in v1.

Measured totals (audio task, 32 samples @ 44.1 kHz): default ~70k, Size 3000
~98k, Size 3500 ~123k, Size 3800 ~128k, Size maximum ~128k. The tightest path
is Size maximum with reverb 100% at ~161k average. Full numbers and the
measurement caveats are in §11.

### 4.3 Flash

Measured on the completed v1 port (`feat/mbeads`, ESP-IDF 5.5.4):

- `build/ctag-tbd.bin`: **0x316040 = 3,235,904 bytes** (final v1 with the CPU
  fix and debug instrumentation).
- Smallest app partition: `0x380000` = 3,670,016 bytes.
- Free: **0x69fc0 = 434,112 bytes (11%)**.
- Beads DSP baseline Task 1 image: `0x308df0`. Linking the DSP in Task 2,
  Task 3 instrumentation, and the final CPU/stack fix brought the image to
  `0x316040`: net **+53,840 bytes**, slightly above the 30–50 KB estimate
  below.

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
from the audio task stack.

The half-rate path (Vintage/Clean/Dirty) previously kept its `d_freeze`,
`d_triggers`, `d_taps`, `d_in`, `d_out`, `downsampled` and `upsampled` arrays
as fixed locals, leaving only **20 bytes** of the 4,096-byte audio stack free.
They now live in a single `beads::HalfRateScratch` member of
`beads::Processor`, shared by the three half-rate engines. Measured worst-case
free stack is **~1,748 bytes** (Vintage at maximum Size); the Digital path
leaves ~1,844–1,924 bytes. No stack increase was needed.

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
- `beads::Processor::half_rate_scratch_` (`HalfRateScratch`: `freeze`,
  `triggers`, `taps`, `in`, `out`, `downsampled`, `upsampled`), shared by the
  Vintage/Clean/Dirty half-rate engines

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
- `lut_sine` (used by `sine_oscillator.h` for the retained delay-mode DSP) and
  the lookup-table arrays stay. Delay mode itself is disabled in the v1 plugin
  but is kept in the DSP core for tests and future optimization.

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
| `size` | int | 0..4095 | grain size; delay mode is disabled in v1, so the full range stays granular |
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
   - delay mode DSP (host test only in v1);
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

**Resolved (v1):** measured ~260k cycles/block dry and ~309k with reverb after
skipping zero-gain taps; disabled in the plugin. The DSP core remains behind
`SetDelayModeEnabled(false)` and is covered by the host test. See §11.

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
- Delay mode DSP core (enabled explicitly in the host test) produces 8 taps and
  does not read outside the buffer; the v1 plugin keeps delay mode disabled.
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

**Known follow-ups**

- The firmware glue (`ctagSoundProcessorMbeads`) is reviewed but not directly
  unit-tested: the host test drives `beads::Processor` only. A glue-layer
  host/simulator harness is still missing.
- Tape modes are smoke-tested only. The tape wet path has no dedicated
  coverage, and the tape UI is deferred.
- Hardware CPU and audio-task stack high-water are measured (§11). The 30-minute
  maximum-density/reverb endurance run and the load/unload fragmentation loop
  have not been run yet.
- The `cpuOverload` percentages in §11 are sampled through the debug UART,
  which can preempt the audio task. Treat the averages, not the spike count, as
  the steady-state load; the user listening test is the final gate for audible
  dropouts.
- Reverb + maximum Size is the tightest path (~161k average, 39% sampled
  overload). It is under the 174,150-cycle budget on average but has limited
  margin; the dry cap is 4 grains, the audible-reverb cap is 2.

---

## 9. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| CPU over budget with many overlapping grains | High (observed) | Active-grain render list, reverb bypass at mix 0, and a 4-grain dry / 2-grain reverb cap; measured Size sweep in §11 |
| Delay mode (8-tap pitch-shifting delay) over budget | High (observed) | Disabled in v1; DSP retained behind `SetDelayModeEnabled(false)` for tests/future work |
| PSRAM random-access stalls | Medium | Sequential grain reads; measure cache misses; reduce grains/pitch extremes |
| Audio task stack overflow (pre-port VLAs, ~4.8 KB) | High (certain pre-port) | `Processor` scratch and `HalfRateScratch` moved to PSRAM members; measured worst-case free stack ~1,748 bytes |
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
  not compiled; `lut_sine` stays for the retained delay-mode DSP.
- **Media modes:** v1 ships **Digital (44.1 kHz, 16-bit)** and **Vintage
  (22.05 kHz, 12-bit)**. Tape modes deferred.
- **Attenuverters:** all four exposed (`time_mod`, `size_mod`, `shape_mod`,
  `pitch_mod`).
- **Runtime media switching:** accepted, with a short mute and a one-time
  buffer clear.
- **Delay mode:** **disabled in v1**. The measured 8-tap pitch-shifting delay is
  ~260k cycles/block even after skipping zero-gain taps, far above the
  174,150-cycle budget. The DSP core remains behind `SetDelayModeEnabled(false)`
  (default) and is enabled explicitly by the host test. Size maximum stays in
  the granular engine.
- **CPU protection:** render only active grains; skip the reverb network when
  its mix is zero; cap concurrent grains at 4 (dry) / 2 (audible reverb). These
  are v1 behaviour, not debug-only knobs.
- **Stack:** the half-rate scratch buffers live in a `HalfRateScratch` member of
  `beads::Processor` (PSRAM), not on the audio-task stack.

Implementation is tracked in `doc/mbeads-implementation-plan.md`.

---

## 11. Implementation status and validation

Status of the v1 port on branch `feat/mbeads`: **implemented, host-validated,
and measured on hardware (ESP32-A1S / ESP32-Audio-Kit v2.2).** The
Size-dependent crackle was reproduced, traced to CPU overload, and fixed.

| Item | Measured value |
|---|---|
| `sizeof(beads::Processor)`, x86-64 host, `-O2` | **34,656 bytes** (host-test guard is 40 KB) |
| Delay buffer / reverb buffer | 786,432 / 65,536 bytes (PSRAM) |
| PSRAM per Mbeads instance | ~866 KB (buffers + object) |
| `build/ctag-tbd.bin` | **0x316040 = 3,235,904 bytes** |
| Free in app partition (`0x380000`) | **0x69fc0 = 434,112 bytes (11%)** |
| Host core test | **PASS** (`tests/host/run_mbeads_tests.sh`) |
| Modulation suite | **PASS** (`tests/host/run_tests.sh`) |
| Audio-task stack, worst measured | **1,748 bytes free** (Vintage, maximum Size), up from **20 bytes** |
| CPU budget | **174,150 cycles** per 32-sample block @ 44.1 kHz / 240 MHz |

### The Size crackle: cause and fix

At a fixed Density, raising Size lengthens each grain. The number of grains
overlapping at any instant is roughly `trigger_rate * grain_duration`; at the
default Density 3072 (~4.09 Hz), Size 3500 produces 4–5 concurrent grains and
Size 3800 produces ~8. Each Hermite-interpolated grain costs roughly
14–17k cycles/block, and the Dattorro reverb adds 43–55k. The audio task
therefore crossed the 174,150-cycle budget, and the resulting I2S underruns
were heard as harsh crackle/digital distortion. The default patch was fine
because it uses 0–1 grains and reverb = 0.

Before the fix (Digital, Density 3072, reverb 0): default ~148k, Size 3000
~160k, Size 3500 ~217k, Size 3800 ~260k, Size maximum (delay mode) ~386k.
Overload samples: 32/100 at default, 88/100 at Size 3800, 100/100 at maximum.
The old default already overloaded 32% of sampled blocks but was not audibly
broken because the spikes were short; at long Size the overload was sustained.

The v1 fix has four parts:

1. **Render only active grains** (`granular_player.h`,
   `delay_granular_player.h`). A compact list is rebuilt once per block and
   after each new trigger. Done grains are no longer passed through the
   `Render()` call every sample. Measured: default 148k → 125k; Size 3000
   160k → 143k.
2. **Bypass the reverb network when its mix is zero** (`processor.cc`). The
   reverb buffer is zeroed at plugin init and cleared again on the first
   re-enable. The default patch has reverb = 0, so this removed the network’s
   ~43–55k cycles. Measured: default 125k → 68.6k; Size 3000 143k → 94.3k.
3. **Cap concurrent grains** (`SetMaxActiveGrains`). The plugin admits at most
   **4** grains with reverb off and **2** when reverb is audible. A trigger that
   arrives at the cap is dropped; lowering the cap fades excess grains out
   quickly. Measured: Size 3800 211.7k → 125.0k; Size 3800 + Density 0 (fastest
   trigger rate) 488.8k → 127.5k; Size 4095 386k → 127.5k.
4. **Move half-rate scratch off the audio stack** (`HalfRateScratch` in
   `processor.h`). The Vintage/Clean/Dirty path previously had ~1.8 KB of
   fixed locals on the 4 KB audio-task stack, leaving 20 bytes free. It now
   lives in the `beads::Processor` PSRAM object. Worst measured free stack:
   1,748 bytes.

Delay mode is a separate case. Even after skipping pitch-shifting taps whose
gain is zero (a 3× read reduction at pitch 0), the 8-tap multitap delay costs
~260k cycles/block dry and ~309k with reverb. It is **disabled in the v1
plugin** (`SetDelayModeEnabled(false)` is the default); the DSP code is retained
and the host test enables it explicitly for coverage. Size maximum now stays in
the granular engine.

### Measured CPU after the fix

All figures are total audio-task cycles per 32-sample block, measured through
`/debug/getAudioStats` while the debug UART was active. `overload` is the
number of the 100 sampled blocks whose last-block flag exceeded the budget;
the flag is conservative and can be set by UART/Wi-Fi preemption, so the
average is the steady-state figure.

| Case | Average | Max | Overload |
|---|---:|---:|---:|
| Default Digital (Size 2048, Density 3072, reverb 0) | **70.1k** | 129.6k | 0/100 |
| Size 3000, reverb 0 | **98.1k** | 172.2k | 0/100 |
| Size 3500, reverb 0 | **122.8k** | 234.6k | 4/100 |
| Size 3800, reverb 0 | **128.3k** | 193.8k | 10/100 |
| Size 3800, Density 0 (fastest) | **129.9k** | 221.6k | 12/100 |
| Size 4095 (maximum, granular) | **127.5k** | 273.6k | 12/100 |
| Size 4095 + reverb 4095 | **161.4k** | 304.2k | 39/100 |
| Size 3000 + reverb 4095 | **149.5k** | 405.6k | 28/100 |
| Vintage default | **83.4k** | 142.7k | 0/100 |
| Vintage Size 3000 | **97.9k** | 202.1k | 1/100 |
| Vintage Size 3800, Density 0 | **116.4k** | 168.0k | 0/100 |
| Vintage Size 4095 | **116.0k** | 188.3k | 1/100 |

Every averaged case is under the 174,150-cycle budget. The tightest path is
**reverb + maximum Size** at ~161k average, with 39% sampled overload spikes;
it has ~13k cycles of average headroom but limited transient margin. The
listening test at Size 3500/3800/4095 with reverb 0 is the primary acceptance
gate for the reported bug.

### Host validation

```
$ bash tests/host/run_mbeads_tests.sh
...
  [ ok ] default patch grain settings are audible (wet only, no reverb)
  [ ok ] delay mode wet path is audible
  [ ok ] delay-to-granular return is non-silent
  [ ok ] max Size stays granular and is audible
  [ ok ] active grains stay within the cap
  active grains = 4
...
PASS: mbeads core
```

The test builds the real Beads sources at 44.1 kHz in fixed 32-sample blocks
and covers: media rate table (44.1k/22.05k, ratio 1/2), trigger generator rate
(131 pulses/s), grain-rate integration through `MixParameters::output_gate`
(131 grains/s for both Digital and Vintage, deterministic at density 0), fully
wet granular output with `reverb = 0` and `dry_wet = 1`, the shipped default
patch (density 3072, seed 1, time 0.5), in-place `in == out` safety, freeze/seed,
Clean/Dirty Tape smoke runs, the delay-mode DSP core with
`SetDelayModeEnabled(true)`, **Size maximum staying granular with delay mode
disabled**, and **the grain cap holding at 4 with Density 0 / Size 1.0**.
`tests/host/run_tests.sh` passes all modulation tests.

### Default patch rationale

`spiffs_image/data/sp/mp-Mbeads.jsn` and the parameter atomics in
`ctagSoundProcessorMbeads.hpp` ship:

- `density = 3072` (0.75): above Beads’ manual-tap zone (0.48..0.52), so the
  grain clock free-runs.
- `seed = 1`: the seed button holds the trigger generator’s gate high. Upstream
  Beads only advances the density clock while the seed gate is high, so with
  `density = 2048` / `seed = 0` the wet path was silent on a fresh load (dry
  still passed).
- Everything else stays neutral: `time/size/shape = 2048`, all `*_mod = 2047`
  (centre), `pitch = 0`, `feedback = 0`, `reverb = 0`, `dry_wet = 2048`,
  `media = 0` (Digital), `freeze = 0`.

The default `reverb = 0` is now also the reverb-bypass case, so the fresh-load
patch has the largest CPU margin. The host test asserts the shipped grain
settings are audible (wet RMS 0.037 with no reverb), and both `density` and
`seed` remain exposed in the UI.

### Media-switch cost

`GranularProcessor::Reset(bool mono)` clears only the delay line of the active
channel layout (mono or stereo). Both layouts share the same 768 KB of buffer
memory with different item formats, so a single pass leaves the buffer clean for
either interpretation; the previous upstream `Reset()` cleared both (1.5 MB) for
no benefit. A media change therefore costs a **one-time 768 KB PSRAM clear**
(order 20 ms) plus a **two-block mute** (~1.5 ms) applied by the glue. The clear
can outlast the mute, so the switch is an accepted short one-time gap, not a
glitch-free crossfade. The parameter is not meant to be modulated at audio rate.

The reverb buffer is 64 KB and is zeroed once at plugin init (outside the audio
task). Enabling reverb after a muted period clears it again before the first
processed block; this is a one-time 64 KB clear, not a per-block cost.

### Measurement procedure

The measurements used the debug firmware endpoints:

- `/debug/setPluginParam?ch=0&id=<param>&value=<0..4095>` (also used for
  `media`: 0 = Digital, 1 = Vintage).
- `/debug/getPluginParams?ch=0` to read back the atomics.
- `/debug/getAudioStats` for `audioCycles`, `cpuOverload` and
  `stackHighWater`.

The helper scripts live in `/tmp/opencode` (`mbeads_stats.py`,
`mbeads_delay.py`, `mbeads_serial.py`) and are not part of the repository.
Each case set the parameters, waited for the engine to settle, then sampled 100
blocks. The `overload` count is intentionally reported rather than hidden: it
shows where transient margin is thin, even when the average is comfortably
inside the budget.

### Known limitations

- **Delay mode is not available in v1.** It is retained in the DSP core and
  covered by the host test, but its measured cost is far over budget.
- **Grain-overlap cap.** At extremely dense/long settings the texture is
  thinner than upstream Beads: 4 grains dry / 2 with audible reverb. Normal
  settings (including the default patch and Size up to ~3500 at default
  density) are below the cap and match the intended Beads character.
- **Reverb + maximum Size** is the tightest CPU path (~161k average) and may
  still show transient overload spikes in the debug build. The dry path at
  maximum Size has substantially more margin (~128k).
- The 30-minute endurance run, load/unload fragmentation loop, and a glue-layer
  host harness remain open follow-ups.
- `cpuOverload` sampled through the debug UART is not a substitute for a
  listening test; the user’s ear is the final gate for audible dropouts.
