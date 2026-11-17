# VARG Block Chords — polyphonic chord engine driven by the ARP

Date: 2026-11-17
Status: design (awaiting approval)

## Intent and why

The Euporack VARG ARP algorithm (algorithm 5 of the ARP ×2 family) already
encodes the black-metal harmony vocabulary from `docs/Black Metal Chords,
Scales, Harmonies, and Progressions.md`: 11 progressions (10 documented
archetypes + a weighted Markov generator), 9 chord qualities, 16 guitar riff
masks with per-bar weak-step mutation, and a RAW/TRIAD/DISS quality bias.

Today it spends that vocabulary **sequentially**: `ArpBuildVarg` assigns one
chord tone to each of the 16 steps, and `ArpCalcNote` emits one tone per gated
step. Driving TBDings' chord engine with the ARP therefore produces an
**arpeggio** — the root jumps through the chord's tones. The human wants the
opposite: each gated step is a **strum**, and every strum sounds **all tones of
the chord at once** — a block chord — with the exact guitar picking pattern
(the 16 masks and their mutation) preserved as the rhythm.

This is a new *flexible* algorithm, not a special-case patch: all 11
progressions, 9 qualities, 16 masks, mutation and bias must remain selectable,
and the set of soundable voicings must cover the doc's chord types.

## Goals

- A gated VARG step sounds a full chord (root + voicing) simultaneously.
- The 16 riff masks and per-bar mutation stay the picking/gate pattern.
- Chord changes once per bar from the progression; quality/bias/mutation
  decide *which* chord.
- Every doc chord type has a real voicing: `m`, `5`, `5b5`, `dim`/`dim7`,
  `m7b5`, `aug`, `add9`/`add6`, `m+b9` pedal, `M`, plus the existing
  extensions (m7/m9/m11/69/M7/M9/sus4/OCT).
- The existing VARG arpeggio (algo 5) stays intact for existing presets.

## Non-goals

- No new CV/gate slots in the rack modulation space (`N_CVS` stays 106,
  `N_TRIGS` stays 51). The ARP family already has pitch CV + gate/accent/slide
  trigs per source, which is enough.
- No new panel page. The new algorithm is selected as an extra entry in the
  existing ARP algorithm row.
- Scales/diatonic folding do not apply — block chords are semitone-exact, as
  VARG already is.
- No change to the sibling engines (Marbles, GATE/ARP gate modulators).

## Anti-goals (what would make this fail even if it plays)

- A "chord" that is actually a fast arpeggio (the thing we are replacing).
- A quality that sounds like a transposed major/minor triad because Rings has
  no matching voicing (e.g. `dim7` or `add9` collapsing to `m`).
- Breaking existing VARG presets or the panel's algo encoding.

## Constraints

- **ARP algo encoding** is `v = algo * 3 + (range - 1)`, with `range` 1..3;
  clamp maximum is `kArpAlgoCount * 3 - 1`. Adding one algo raises the clamp
  from 17 to 20 and the panel name table from 18 to 21 entries; the per-algo
  param rows grow from 6 to 7 (same four P rows each).
- **CV/trig slot indices are memory-safety critical**: every setter routes
  through `ClampCVSlot`/`ClampTrigSlot`. Adding no slots avoids touching the
  two name tables (`main/CVSlotNames.hpp`, `uimcu/ui/CVSlotNames.hpp`) and the
  top-level CMake `N_CVS`/`N_TRIGS` defines.
- **TBDings fills the plugin arena exactly** (105,708 B object + blockMem);
  the extended chord table must stay `const` in flash, not a Part member.
- **Preset compatibility**: a new TBDings param needs a `"def"` member in the
  mui spec so `LoadPreset` pre-fills it; otherwise presets predating the param
  silently drop edits to it (see `1f35ccc3`).
- **A1S/BBA2 build parity**: the Rings chord table is shared source compiled
  on both boards; the TBDings wrapper is one TU.

## Decisions (my calls, recorded here rather than agreed with the human)

### 1. New algo id 6, `kArpAlgoVargBlock`

Keep `kArpAlgoVarg` (5) byte-for-byte. The new algo reuses VARG's `st.varg`
state and all four P params (PROG / RHYTHM / QUALITY / MUTATE) and its
progression/quality/mutate machinery. `VargNextChord`, `VargLoadChord`,
`VargBiasQuality` and the mask mutation are shared, not copied.

### 2. One CV carries a packed chord index; no new slots

`SQ.A1`/`SQ.A2` pitch CVs are floats. For the block algo, `SeqEngine::Process`
writes the packed index **directly to `cv[arpPitchSlot[i]]`**, bypassing the
normal `pitchCv` derivation (which goes through the 8-bit `s.cur_note` and
would quantize the index into a wrong chord). The encoding is:

```
index   = curOff * VQ_COUNT + curQual          // 0 .. 11*9+8 = 107
cv      = index / 108.0f                        // 0 .. ~0.99
```

- `curOff` is the progression chord's root offset (semitones above the arp
  root), `curQual` the post-bias quality (0..8). Both are already in
  `ext.st.varg`.
- The ARP's glide (`e.current_pitch_q16`) is irrelevant to the block CV
  because that CV is written directly; `ArpCalcNote` still returns the
  *audible root* MIDI note for `s.cur_note`, which keeps the panel's SEQ note
  display meaningful without coarsening the CV.
- Gate/accent/slide trigs are unchanged: gate = strum, accent = downbeat.

### 3. TBDings decodes the index on a new CV-capable param `vargChord`

Add `vargChord` (0..1, `cv`-capable, `"def":0`) to TBDings' mui spec. When its
CV is mapped, `updateParams` decodes:

```
idx  = clamp(round(cv * 108), 0, 107)
off  = idx / 9;  qual = idx % 9;
performance_state.note  += off;                 // root offset in semitones
performance_state.chord  = kVargToVoicing[qual];// quality -> Rings voicing
```

The manual `frequency` param remains the base register; `chords` remains the
manual voicing selector when no `vargChord` CV is mapped. The decode is a few
integer ops in the audio block — no logging, no allocation.

A shared header (`components/ctagSoundProcessor/helpers/varg_chord.h`, C++11
constexpr, no ESP-IDF dependency) holds the quality enum, the tone sets, the
`kVargVoicing` table and the encode/decode helpers. Both `SeqEngineCore.hpp`
(DSP) and `ctagSoundProcessorTBDings.cpp` include it. The host tests compile
it with plain g++ (include path already covers `components/`).

### 4. Widen Rings' chord table to cover the doc

`kNumChords` (`rings/dsp/performance_state.h`) goes from 11 to the size of the
extended table. `string_synth_part.cc` gains voicing rows for the qualities
Rings lacks, in all four polyphony variants, using the doc's shapes:

- `5b5` — root + tritone (dropped-fifth voicing)
- `dim` / `dim7` — `0,3,6` and `0,3,6,9`
- `m7b5` — `0,3,6,10`
- `aug` — `0,4,8`
- `add9` / `add6` — `0,3,7,14` and `0,3,7,9`
- `m+b9` — `0,1,7` (the doc's pedal/minor-second friction)
- optionally a pedal/drone voicing (`1, b2, 5` open-string clash)

`kVargToVoicing[VQ_COUNT]` maps each quality to the appropriate row. The
table stays `const float[...]` in flash. `cv_scaler.cc`'s `kNumChords` scaling
follows automatically.

### 5. Voicing layout across the four polyphony variants (builder check)

`StringSynthPart::Process` sets
`chord_size = min(kStringSynthVoices / polyphony_, kMaxChordSize)` = **8 / 6 /
3** notes at polyphony 1 / 2 / 4. New voicing rows follow the existing
table's convention: slots are ordered by *identity priority*, not by pitch.
Slot 0 is the root (lower-octave root at `-12` where the existing rows use
it), then the tones that define the quality, then extensions, then
octave-duplicated root/fifth to fill the remaining slots. Concretely:

- `5b5`: `0, 6, 12, 18, ...` — root + tritone, root doubled upward.
- `dim7` (`0,3,6,9`): poly 1 gets all four + octave doubles; at poly 4 (3
  slots) the `bb7` (9) drops first, leaving the diminished triad `0,3,6`.
- `m7b5` (`0,3,6,10`): poly 4 keeps `0,3,6`; the `b7` (10) drops first.
- `add9` (`0,3,7,14`): poly 4 keeps the minor triad `0,3,7`; the 9th drops
  first.
- `aug` (`0,4,8`) and `m+b9` (`0,1,7`): exactly 3 tones, survive poly 4 whole.

The rule — **core quality-defining tones first, extensions next, octave
doubles last** — guarantees the quality stays audible even at polyphony 4,
where only three slots exist. This is my call, not a question for the human.

### 6. Migration behavior (builder check)

Switching an existing ARP from algo 5 to algo 6 without remapping its pitch
CV from `frequency` to `vargChord` is accepted: the packed index on
`cv_frequency` decodes to a root offset of `0..11*5 = 0..55` semitones, so
the chord root wanders and the quality stays at the manually selected
`chords` value. Chords still sound; the progression/quality/mutation are
inert until `vargChord` is mapped. This needs only a bench/usage note, not a
graceful-fallback implementation — it is a new algorithm, not a migration of
existing patches. My call.

## What's left to the builder

- Implement algo 6 in `SeqEngineCore.hpp` (shared VARG state, packed index in
  `ArpCalcNote`, glide bypass, audible-root `cur_note`), plus the
  `kArpAlgoCount`/clamp/panel-table/param-row mechanical bumps.
- Create `varg_chord.h` and switch VARG to its shared definitions where
  sensible (keep `kVargProgressions`/`kVargMasks` in the engine).
- Extend the Rings chord table + `kVargToVoicing`; bump `kNumChords`.
- Add `vargChord` to TBDings' hpp/cpp/mui spec with `"def":0` and the decode.
- Host tests: encode/decode roundtrip for every `(off, qual)`, table vs doc
  chord examples, one-chord-per-bar + mask mutation unchanged, preset
  compatibility (old VARG preset still arpeggiates).
- Build A1S + BBA2; bench-verify on the flashed A1S: a TRM16/BURST mask with
  `chords` sounding multiple simultaneous voices (verify with output capture
  or the MIX VU), quality changes audible as the right chord shape, 0
  overload / 0 I2S errors.
- Store/push no factory preset changes unless intentionally changing defaults.

## Open questions for the human (none currently)

All design choices above are recorded as my calls. Nothing was agreed with the
human beyond the intent: block chords, exact guitar mask rhythm, flexible.
