# Elements × Warps: internal signal-hybridization stage (implementation plan)

Status: EXECUTED (2026-11-17, eurorack dadd66c) with two data-driven
deviations from the text below, both recorded in AGENTS.md: (1) the engine
scalings — `modulation_algorithm` is normalized 0..1 (not 0..5) and
`channel_drive` is the engine's own gain pot (not a 0..4 pre-gain); (2) the
stage is **BBA2-only at runtime** — on the A1S even the xmod zone saturates
core 0 while playing (helper > 1 ms share → link starvation; the whole
Modulator in PSRAM also produced 39 ms blocks). sizeof(Modulator) measured
72,660 B on-target (vocoder 65,760 B is split into a separate PSRAM-tolerant
allocation; the 7.4 kB core is internal-first). A1S storage updated via
writeFileChunk (user presets preserved by merge); BBA2 verification pending
its bench session.

## 1. Objective

Add Mutable-Intruments **Warps** cross-modulation as a stage inside the
Elements plugin, between the resonator mixdown and the reverb:

    exciter → resonator → mixdown → Warps (carrier=resonator, mod=raw exciter)
             → reverb ("Space") → out

All 7 Warps algorithms (crossfade, cross-fold, diode ring-mod, digital
ring-mod, XOR, octaver/comparator, 20-band vocoder). Zero cost when off.
Both DSPs (A1S classic ESP32 and BBA2/S3) get it from the same sources.

The Warps DSP is ALREADY in the eurorack submodule, unused:
`components/mutable/eurorack/warps/dsp/{modulator,vocoder,filter_bank,oscillator}.{h,cc}`,
`warps/resources.cc` (lut_ap_poles), warps' own `sample_rate_converter.h`.
`Modulator::Process(ShortFrame*, ShortFrame*, size)` — int16 interleaved
(carrier,modulator) in, processed mono out; kMaxBlockSize=96 (our Part block
is 32 → fits), kOversampling=6 (internal SRC up/down), Parameters via
`mutable_parameters()`: `channel_drive[2]`, `modulation_algorithm` (float
position 0..5, blends adjacent algorithms), `modulation_parameter` (timbre),
`carrier_shape` (0 = external).

## 2. Signal design

- **Carrier input** (left of the ShortFrame pair): `0.5*(main+aux) *
  warps_carrier_level` — mono sum of the resonator stereo pair.
- **Modulator input** (right): `raw_buffer_[j] * warps_mod_level` — the raw
  exciter signal Mutable exposes on Elements' AUX out (the canonical
  Elements→Warps patch). `mod_level > 1.0` gives the manual's "warm
  overdrive" (use 0..4×).
- **Output**: Warps is mono. Write `out` into both channels with a dry/wet:
  `main = main*(1-mix) + out*mix; aux = aux*(1-mix) + out*mix`.
  `mix=0` = untouched stereo, and `warps_algorithm == 0` bypasses with ZERO
  cost (no Process call, no buffer work).
- **Tap point**: in `elements/dsp/part.cc` AFTER the metering/panic block
  (so the panic guard keeps measuring the pre-Warps resonator level) and
  BEFORE `reverb_.Process(main, aux, size)`.

## 3. Params (0..4095 UI scale, cv/trig-capable automatically)

| id | name | mapping |
|---|---|---|
| `warps_algorithm` | Warps Algo | 0 = off; else position `p = val/4095*5` (0..5 float, blends adjacent algorithms — the module's knob) |
| `warps_timbre` | Warps Timbre | `val/4095` → `modulation_parameter` |
| `warps_mod_level` | Warps Mod | `val/4095*4` → `channel_drive[1]` (0..4×) |
| `warps_carrier_level` | Warps Car | `val/4095*2` → `channel_drive[0]` (0..2×) |
| `warps_mix` | Warps Mix | `val/4095` dry/wet |

Defaults in the factory patch: all 0 (bypass, zero cost).

## 4. Code changes

### 4.1 Submodule `components/mutable/eurorack` (fork workflow, §9)

- `elements/dsp/patch.h`: add 5 floats:
  `warps_algorithm, warps_timbre, warps_mod_level, warps_carrier_level, warps_mix`
  (Patch is memcpy'd into HelperBlock — +20 B, fine).
- `elements/dsp/part.h`: member `warps::Modulator* warps_ = nullptr;` and
  `Init(uint16_t* reverb_buffer, warps::Modulator* warps)`; include
  `warps/dsp/modulator.h`.
- `elements/dsp/part.cc`:
  - `Init`: store `warps_`; `if (warps_) warps_->Init(sample_rate)` (the
    rate arrives in Init — 32 kHz on both boards; the internal oscillator
    is unused in phase 1, carrier_shape stays 0).
  - `Process`: after the metering/panic block, before the reverb block:
    ```cpp
    if (warps_ && patch_.warps_algorithm > 0.0f) {
      warps::ShortFrame in[kBlockSize], out[kBlockSize];
      const float car = patch_.warps_carrier_level;
      const float mod = patch_.warps_mod_level;
      const float mix = patch_.warps_mix;
      for (size_t i = 0; i < size; ++i) {
        float c = 0.5f * (main[i] + aux[i]) * car;
        float m = raw_buffer_[i] * mod;
        in[i].l = (short)(c > 1.f ? 32767 : (c < -1.f ? -32767 : c * 32767));
        in[i].r = (short)(m > 1.f ? 32767 : (m < -1.f ? -32767 : m * 32767));
      }
      warps::Parameters* wp = warps_->mutable_parameters();
      wp->modulation_algorithm = patch_.warps_algorithm;      // 0..5
      wp->modulation_parameter = patch_.warps_timbre;         // 0..1
      wp->channel_drive[0] = patch_.warps_carrier_level;      //  untested>1 ok
      wp->channel_drive[1] = patch_.warps_mod_level;
      wp->carrier_shape = 0;
      warps_->Process(in, out, size);
      for (size_t i = 0; i < size; ++i) {
        float w = static_cast<float>(out[i].l) * (1.0f / 32768.0f);
        main[i] += (w - main[i]) * mix;
        aux[i] += (w - aux[i]) * mix;
      }
    }
    ```
  - Verify against warps' actual `channel_drive` semantics in
    `modulator.cc` (`Process` applies drive to the int16 inputs before the
    float conversion — confirm the scaling; keep our pre-gain then drive=1
    if the module applies it internally).
- `warps/dsp/modulator.cc`: `#include "warps/drivers/debug_pin.h"` pulls
  STM32 headers and is used once. Replace with an empty platform stub
  (`#if defined(ESP_PLATFORM) ... #else include original`) in the submodule.
- `components/mutable/CMakeLists.txt` `SRCS_FILES`: add
  `eurorack/warps/dsp/modulator.cc`, `vocoder.cc`, `filter_bank.cc`,
  `oscillator.cc`, `eurorack/warps/resources.cc` (+ headers).
  Watch the documented LUT-macro collisions (LUT_* #undef sandwich in the
  generated aggregator; warps' resources introduce `lut_ap_poles` only —
  verify the build for "conflicting declaration"/macro redefinition).

### 4.2 Wrapper `components/ctagSoundProcessor/ctagSoundProcessorElements.cpp`

- 5 new members + `pMapPar.emplace("warps_*", …)` lambdas (pattern at
  lines ~806-844).
- In `Process`, map params → `patch.warps_*` (algorithm position =
  `val/4095.f*5`, timbre/mix = `/4095`, levels = `/4095.f*4` and `*2`).
- `Init`: allocate the Modulator OUTSIDE the arena (§5) and pass it to
  `part.Init(reverb_buffer, warps_mod)`.
- `~`/`Stop`: free it after the helper is stopped.

## 5. RAM / arena strategy (hard constraint)

The plugin arena is EXACTLY full today: Elements = 81,536 B object +
30,080 B blockMem = 111,616 B (`ctagSPAllocator`). `warps::Modulator`
(src buffers 2×576 f + 3×96 f + vocoder/filter-bank state ≈ **8–12 KB**)
MUST NOT be added to `Part`/the wrapper object — instead:

- `heap_caps_malloc(sizeof(warps::Modulator), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`
  — internal-first, SAME 48 KB headroom-yield rule as the reverb buffer
  (2026-11-16 boot-loop fix): if the largest remaining internal block after
  the alloc is < 48 KB, free and take PSRAM instead. PSRAM placement of the
  Warps state is a *fallback* — it is per-sample memory; measure its CPU
  hit if it ever lands there.
- FIRST TASK of execution: `sizeof(warps::Modulator)` — print it from a
  host build or a one-line `ESP_LOGI` at Init; feeds the headroom math.

## 6. CPU budget & gates

- A1S (helper core 0, 52 modes @ 32 kHz, budget 1000 µs/block, currently
  352–460 µs used): measure per algorithm with `bench_elements_perf_a1s.py`
  (pinned 52): delta helper µs at algorithm 0 vs each algorithm position.
  **Gate: total ≤ 800 µs** in the worst algorithm under noise/strikes.
  Expected: crossfade/xor/octaver cheap; oversampled diode/digital ring-mod
  mid; vocoder heavy (20+20 third-octave bands).
- BBA2 (single-core, 24 modes, 240k cycles): same measurement via
  `getAudioStats` + PERF windows. **Gate: ≤ 200k cycles** worst algorithm.
  If the vocoder breaks the gate: BBA2-only exclusion of the vocoder
  algorithm (map it to octaver) or pin BBA2 Elements to 20 modes — decide
  with data, record in AGENTS.

## 7. Storage update WITHOUT a full reflash

The two factory JSONs gain the 5 params; update them on-device via the bench
file API (no `idf.py flash`, storage untouched):

1. Backup: `/debug/listFiles` + `/debug/readFileChunk` for
   `/spiffs/data/sp/mui-Elements.jsn` and `mp-Elements.jsn`
   (or the existing `spiffs_preset_dump.py` flow).
2. Repo: edit `spiffs_image/data/sp/mui-Elements.jsn` (append 5
   `{"id","name","type":"int","min":0,"max":4095}` entries) and
   `mp-Elements.jsn` (append 5 `{"id","current":0,"cv":-1,"trig":-1}`).
3. Device: `/debug/writeFileChunk` both files, then
   `/debug/setActivePlugin {"ch":0,"id":"Elements"}` to reload.
4. Repeat for the BBA2's storage when that board is tested.

## 8. Build / flash / verify checklist

- [ ] A1S: `idf.py build` (v6.1.1) green → `idf.py -p /dev/ttyUSB0 app-flash`
- [ ] boot: no abort, `LinkEp` handshake, resolution 52, reverb placement sane
- [ ] JSON update via §7, plugin reload
- [ ] C3 params page lists the 5 params; CV mapping works (map MB.X1 to
      warps_algorithm, hear the algorithm scan)
- [ ] audio per algorithm (user listen: kick + synth pairing); mod_level
      >1.0 overdrive audible
- [ ] ladder stays 52 (`getAudioStats` resolution + overload 0)
- [ ] perf gates §6 recorded; AGENTS.md updated; commits pushed
- [ ] BBA2: build + same verification when its board is on the bench

## 9. Submodule fork workflow (eurorack)

Per the standing rule: commit inside `components/mutable/eurorack` on its
branch, push to the **schrab fork** (org push is 403), then commit the
updated submodule pointer in the parent repo. See the
`unpublished-submodule-commits` notes. Do NOT bulk-rewrite CRLF dirt.

## 10. Risks & fallbacks

- **LUT macro collisions** (documented deferred issue): warps' resources
  may clash with elements/braids LUT_* macros. If the build breaks, add
  per-TU `#undef` sandwiches in warps' resource header, not global renames.
- **32 kHz vs Warps' 48 kHz tuning**: phase 1 uses external carrier only
  (no internal oscillator, no V/oct) — the SRCs are rate-generic; the
  vocoder's own oscillator only runs if carrier_shape != 0 (we keep 0).
  Verify the vocoder sounds right at 32 kHz; if the filter bank assumes
  48k, retune `kSampleRate` in `Modulator::Init` call (we pass the actual
  rate already).
- **Mono collapse**: Warps is mono; the mix param keeps the stereo path
  intact when dialled back. Accepted by design.
- **Arena**: any accidental member addition to Part/wrapper grows the
  arena object and assert-crashes TBDings/Freakwaves — the Modulator goes
  heap-only, pointer in Part.
- **No new arena/patch storage for warps params in presets**: patch JSON
  entries carry defaults 0; old presets load unchanged (params default to
  0 = bypass).

## 11. Out of scope (phase 2, separate plan)

- Internal oscillator as carrier/modulator (Warps INT. OSC: sine/tri/saw,
  phased noise) with V/oct from Elements' `performance_state.pitch`.
- Easter-egg frequency-shifter mode (`ProcessEasterEgg`).
