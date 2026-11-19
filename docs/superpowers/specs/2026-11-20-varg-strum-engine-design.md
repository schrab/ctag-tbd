# VARG Strum Engine — realistic guitar strumming for TBDings block chords

Date: 2026-11-20
Status: implemented (phases 1+2; A1S flashed & bench-verified 2026-11-20)
Research: `docs/guitar-strumming-voicing-research.md` (sections 2–4 are the
requirement source)

Implementation deltas vs. this doc (learned while building):

- Mask hexes corrected during test-writing: CAMP `0x8A2A`, BACK `0x8AA8`
  (the values below had stray odd bits — an 8th-grid cell must gate only
  even steps). DBEA `0x9090`, CRST `0x8A28` unchanged.
- Preset compat keys on `strumStyle == BLOCK` (the def), not on spread 0:
  with a strum style selected but spread 0, per-note envelopes all fire on
  the edge — musically the block chord, through the new path.
- Chord-change detection is ENGINE-side (last planned chord vs. now), so it
  also damps on manual/MIDI voicing changes; the companion needed only
  stroke + accent (`vargFlags` bit0), not a chord-change bit.
- Direction is computed from the POST-mutation mask (mutation may flip
  steps; the pendulum follows what actually plays).
- Two bench facts: `/link/seqSet` does NOT forward through the bench relay
  (DebugLink forwards `/debug/*` only — drive the ARP via the panel UI);
  and the pluck envelope's trigger block is silent (linear attack from
  level 0 starts at t=0 — measure from the NEXT block).

## Intent and why

VARG Chord (ARP algo 6) already sounds the full chord on every gated step,
but it fires all chord tones on ONE envelope edge — a keyboard block chord.
Research §2 defines the four rules that separate a real strum from that
block (direction, micro-timing spread, upstroke subset/velocity, pendulum
grid-lock), and §3/§4 add rhythmic cells and tone-order modes. This design
maps every rule onto the existing architecture without inventing new
transport or slot machinery.

The single hardest architectural fact: **all chord tones of a group share
one envelope** (`StringSynthPart::ProcessEnvelopes` →
`envelope_values[group]` scales the whole registration; the chord row is
read fresh each block). Per-note staggering therefore cannot be done from
the wrapper or the sequencer — it needs per-note envelopes inside
`StringSynthPart`. That is the one invasive change; everything else is
metadata plumbing.

## Goals

- A gated VARG step strums the chord: tones enter staggered low→high (or
  high→low), 0–45 ms spread, with grid-locked stroke direction.
- Upstrokes catch only the top half of the voicing and drop velocity.
- Spread = 0 reproduces today's block-chord sound bit-for-bit (preset
  compat by construction; `"def":0` per rule `1f35ccc3`).
- The strum engine also serves non-VARG strums (MIDI gate, manual trig) —
  direction falls back to engine-side alternation when no VARG companion.
- New research masks (campfire, backbeat, d-beat, crust) join the VARG
  RHYTHM table; arp order modes (Up/Down/Converge/Diverge/Random/broken
  chords) specified for both the note ARP and the strum order.

## Non-goals (v1)

- Ghost hits (muted thuds on silent steps) and pre-change fret transients —
  stretch phase; they need the companion to carry "ghost" steps and a noise
  source the string synth doesn't have.
- Triplet motif (needs a 24-step bar; SEQ is 16-step 4/4) and 3/4 waltz
  (needs polymeter).
- No new CV/trig slots (`N_CVS=106`, `N_TRIGS=51` untouched).
- No new panel page: strum params ride TBDings' existing param table push.

## How the research rules map (the core of the design)

### Rule 1 + 4 — direction is grid-locked and computable in `ArpBuildVarg`

Direction derives from the mask and the absolute 16th step, never from a
stroke counter — that IS the pendulum grid-lock rule (rests advance the
alternation because direction is a function of grid position):

- Grid classification: the mask is a **16th grid** if any odd step is gated
  (`mask & 0x5555 != 0` — odd step st ↔ bit `15-st` even), else an
  **8th grid**.
- 16th grid: `st` even → downstroke, `st` odd → upstroke
  (1/e/&/a: numbered beats and &s are D, e and a are U).
- 8th grid: `st % 4 == 0` → downstroke, `st % 4 == 2` → upstroke
  (numbered beats D, & offbeats U).

Validation against the research's own notation (MSB = step 1):

- GAL `0xBBBB` gates steps 0,2,3 per beat → D . D U — exactly the research's
  "Standard: (D . D U)".
- RGAL `0xDDDD` gates 0,1,2 → D U D . — the research's reverse gallop.
- TRM16 `0xFFFF` → D U D U … (un-muted tremolo "buzz").
- TRM8/QRT/HALF → all D or D-at-beats/U-at-& (8th-grid rule).
- CHOP `0xF555` → D U D U . U . U — "16th run into offbeat chops".

Crust is the one cell the parity rule reinterprets (research's "D - D U - D U"
is not strictly grid-locked; ours will read D-D-U-U-D-U). Acceptable — the
pendulum is what makes it feel strummed. If a mask ever needs to override the
parity rule, `ArpStep` bit 3 is reserved as an explicit upstroke flag.

### Rule 2 — micro-timing spread = per-note delayed envelopes

`bufSz = 32` (0.73 ms/block). A spread of 15–45 ms = 21–62 blocks, so
per-note triggers are just countdowns, and **no audio delay line is needed**:
the string-synth oscillators run continuously and the envelope only gates
amplitude — delaying a note's envelope trigger IS starting the note later.

Inside `StringSynthPart` (the file is already heavily modified by us:
BRYAN_CHORDS table, distortion stage, `ChordVoicing()` seam):

- Add `StringSynthEnvelope note_env_[kMaxStringSynthPolyphony][kMaxChordSize]`
  (≤ 32 tiny AD envelopes, same class as the group envelopes; the class is a
  handful of floats). They are processed with the same shape the group
  envelope uses (`ProcessEnvelopes` derives attack/decay from
  `patch.damping`), each with its own trigger phase.
- Pitch order: the voicing rows are ordered by **identity priority, not
  pitch** (e.g. add9 has 14.0 before 12.0; 1-5-1 ends …,24,12,24), so build a
  per-row pitch-sorted index permutation at `Init()` (insertion sort over
  ≤ 8 floats × 18 rows — trivial; lives in the same TU, ~144 B RAM inside
  StringSynthPart, no arena impact since StringSynthPart is separately
  `heap_caps_malloc`'d, NOT the plugin arena).
- On a strum edge: for each note i in strum order, `note_env_` gets
  RISING_EDGE + GATE after `delay_i = i * spread / (n-1)` samples (down:
  pitch-ascending, up: pitch-descending over the subset only). Notes NOT
  re-triggered simply keep decaying — that is the research's "overlapping
  sustain/gate creates a dense harmonic drone rather than staccato
  separation".
- **Chord change** (voicing row differs from the last strum): fall ALL notes
  at that strum (the fresh shape damps the old strings) before the staggered
  rise. Within one chord, notes overlap freely.
- When the strum feature is engaged, per-note envelopes own articulation; the
  group envelope keeps its legacy role (registration + drone) but its
  FALLING_EDGE must NOT cut the group in strum mode (it would kill the
  overlap tails). With `spread == 0` the legacy path runs unchanged.
- Guard: clamp the effective spread to < 70 % of the measured inter-strum
  interval (the engine can time strum edges) so fast tremolo at high BPM
  never smears into the next stroke.
- Timing detail a builder would hit: `acquisition_delay_ = 3` (≈ 2.2 ms)
  before a group adopts the new tonic/chord. First strum notes would catch a
  stale chord. In strum mode, apply tonic/chord immediately on the strum
  edge for the active group (or start note delays at ≥ 3 blocks).

### Rule 3 — upstroke subset + velocity

- Upstroke subset = top `ceil(n/2)` voices by pitch order (research: top
  3–4 of 6); at poly 1 (8 slots) that is 4, at poly 4 (3 slots) 2.
- Velocity: accent steps (`ArpStep` bit 1 — already emitted on downbeats by
  `ArpBuildVarg`) = 1.0; non-accent downstroke ≈ 0.85; upstroke ≈ 0.7
  (non-accent up ≈ 0.6). Roughly the research's 10–20 velocity points;
  exact numbers are a bench-tuning knob.
- Amplitude scaling multiplies each note's own envelope value into its
  harmonic amplitudes (the `notes[i].amplitude * harmonics[]` product in
  `StringSynthPart::Process`).

### Companion: 2 bytes of new metadata (the only SEQ→DSP addition)

`ProcessData` (components/ctagSoundProcessor/include/ctagSoundProcessor.hpp)
gains, next to `vargVoicing[2]`:

```cpp
int8_t  vargStroke[2];   // +1 downstroke, -1 upstroke, 0 = no VARG companion
uint8_t vargFlags[2];    // bit0: accent (downbeat), bit1: chord change
```

Plumbing mirrors the existing pattern exactly:

- `ArpBuildVarg` writes the direction bit into `ArpStep.flags` (bit 3).
- `SeqEngine::Process` latches the current gated step's stroke/accent into
  engine state; new getters `GetVargStroke(i)` / `GetVargFlags(i)` beside
  `GetVargVoicing(i)` (SeqEngine.cpp:366). **Timing requirement: the getters
  must describe the step whose gate just opened THIS block** — the wrapper
  sees the trig edge and the companion in the same audio block (SPManager
  fills pd after `SeqEngine::Process` in the same pass). `curQual` is
  bar-stable so quality never had this constraint; stroke does.
- `SPManager.cpp` (~line 186, beside the `vargVoicing` copy) copies both into
  `pd` per block, with the same `-1/0` reset for inactive ARPs.
- `chord change` flag: `curOff`/`curQual` differ from the previous gated
  step's values (engine-side one-line compare).

## Where the knobs live (and why)

`strumSpread` (0..4095 → 0–45 ms, `"def":0`) and `strumStyle`
(enum, `"def":0`) become TBDings params + CV pairs, added to the mui spec
with `"def"` members. Rationale:

- Strumming is a property of the INSTRUMENT (Rings), not of the sequencer —
  it then works identically for VARG Chord, a MIDI keyboard gate, or a
  manual trig mapping. The SEQ engine stays a rhythm/direction provider.
- No panel row growth: params appear via the existing param-table push
  (remember `17aa37ef`: a mui push needs a panel restart to rebuild the
  cached param table).
- Arena-exact caution: the last param addition (`vargChord`, 2 ×
  `atomic<int32_t>`) was absorbed after the "arena-exact" note
  (`32d3af1f`), but the TBDings object still targets an exact arena fit —
  the builder must verify the object size / boot arena line on both boards
  after adding 4 more atomics. Fallback if it breaks: pack spread+style into
  one 0..4095 value (style in the low 3 bits) — ugly but zero-growth.

`strumStyle` enum (research §4 arp orders land here — they are per-STROKE
orders, which is why they fit the strum engine better than the sequencer):

- 0 **Block** — legacy, spread ignored (def; current sound).
- 1 **Pendulum** — grid-locked D/U from the companion; engine-side
  alternation when no companion (MIDI strums).
- 2 **Down** — every stroke low→high (the research's atmospheric 8th-rock
  feel).
- 3 **Converge** — outer voices first, alternating inward.
- 4 **Diverge** — middle voices outward.
- 5 **Random** — research §4 "Random" mode (seeded per stroke, engine RNG).

Upstroke subset/velocity applies in styles 1 (only on U strokes); styles
3–5 are always full-voice (they replace the D/U notion).

## VARG sequencer: new masks (research §3)

Grow `kVargMasks` 16 → 20 (`(s.p[1] * 20) / 101`), appending:

| Idx | Name | Mask | Cell (8th grid) | Research source |
|---|---|---|---|---|
| 16 | CAMP | `0x8A2A` | D . D U . U D U | campfire (DSBM/blackgaze) |
| 17 | BACK | `0x8AA8` | D . D U D U D . | backbeat push |
| 18 | DBEA | `0x9090` | 16th hits 1, a, 2, a (x..x...x..x...) | d-beat |
| 19 | CRST | `0x8A28` | D - D U - D U (pendulum-read) | crust |

Mechanical bumps that ride along (both known patterns):

- Panel name strings `uimcu/ui/menupages/UIMenuPageSeq.cpp:69,72` — append
  `/CAMP/BACK/DBEA/CRST` to both literals.
- Old presets: a stored RHYTHM value remaps ((p·20)/101 vs (p·16)/101) to a
  neighboring mask of the same family — acceptable, documented here.

## Arp tone-order for the note ARP (algo 5) — phase 3, decision open

The research's Up/Down/Converge/Diverge/Random + broken-chord cycles
(B–L–M–H–M–L, B–L–M–H–L–H–M) apply to the note-level VARG arp too. The four
P params are taken (PROG/RHYTHM/QUALITY/MUTATE), so this needs a 5th P param
(`ArpSource.p[4]`, `SetArpP` k-range 0..4, per-algo row bump 7→8, panel
table + `"def"` members) — the same mechanical class as the algo-6 bump.

Two landing options:

- **(a) Shared 5th P param ORDER on algos 5+6** — algo 5 orders the
  successive chord tones (the arp modes), algo 6 orders the strum stagger
  (redundant with `strumStyle` but knob-reachable from the SEQ page).
  Coherent semantics: "how voices are ordered".
- **(b) Defer** — `strumStyle` already covers block chords; algo 5 keeps its
  ascending walk. Zero panel work now.

Recommendation: (b) for v1, (a) as a fast follow if the broken-chord
cycles prove wanted on the note arp. Both ARPs at different rates with
different orders (research §4's polyrhythm trick) is already possible
today — A1/A2 are independent sources; nothing to build.

## Changes by file (v1 = strum engine)

1. `main/SeqEngineCore.hpp` — `ArpStep.flags` bit 3 = upstroke;
   `ArpBuildVarg` computes grid class + parity direction per gated step and
   stores it (accent bit unchanged).
2. `main/SeqEngine.hpp/.cpp` — engine-side latch of stroke/accent/chord-change
   for the step whose gate opened this block; `GetVargStroke`,
   `GetVargFlags` getters.
3. `components/ctagSoundProcessor/include/ctagSoundProcessor.hpp` —
   `ProcessData` += `vargStroke[2]`, `vargFlags[2]`.
4. `main/SPManager.cpp` — copy the companions beside the `vargVoicing` copy
   (reset to 0 when the ARP is inactive).
5. `rings/dsp/string_synth_part.{h,cc}` — pitch-order permutation at Init;
   per-note envelopes; strum plan (order/delay/subset/velocity/chord-change
   fall); spread clamp; immediate tonic/chord adoption on the strum edge;
   legacy path untouched when spread == 0. New API:
   `set_strum(spread_samples, style)` + a per-block
   `apply_strum_companion(stroke, flags)` from the wrapper.
6. `components/ctagSoundProcessor/ctagSoundProcessorTBDings.{hpp,cpp}` +
   mui spec — `strumSpread`/`strumStyle` params + CV pairs with `"def":0`;
   decode companions; feed the engine; no logging (BBA2 USJ rule).
7. `uimcu` — nothing for v1 (params arrive via the param push; panel restart
   after mui push is a known gotcha).

## What's left to the builder

- Host tests (`tests/host/`, plain g++): direction parity per mask
  (GAL=D.DU, RGAL=DUD., TRM16=DUDU, 8th-grid all-D at beats); permutation
  sortedness per voicing row; strum-plan simulation (delays monotonic,
  subset on up, velocity table); spread=0 → legacy-equal plan.
- Build A1S + BBA2; verify the TBDings arena/object size line on both.
- Bench: TRM16 + spread ≈ 15 ms — audible stagger, MIX VU shows per-strum
  re-triggers; upstroke subsets audible on TRM8; chord-change damping at
  bar boundaries; 0 overload / 0 I2S errors; spread=0 identical to
  today's block sound.
- Masks phase (if approved together): `kVargMasks` + panel strings +
  host test for the new cells' gate patterns.

## Phases

1. **Strum engine** (rules 1–4 + companion + params) — the core.
2. **New masks** (CAMP/BACK/DBEA/CRST + panel strings).
3. **ORDER 5th P param** for algo 5 (arp modes + broken-chord cycles) —
   decision (a)/(b) above.
4. **Stretch**: ghost hits (X), pre-change transients, triplet motif
   (24-step bar), waltz (polymeter).

## Open questions for the human

1. Approve the two new TBDings params (`strumSpread`/`strumStyle`) vs.
   pack-into-one if the arena fit breaks (fallback is uglier but
   zero-growth).
2. Masks phase 2 together with phase 1, or after the strum engine is
   bench-verified?
3. Algo-5 ORDER param (option a) now, later, or never (option b)?
