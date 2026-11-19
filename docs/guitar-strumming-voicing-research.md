# Guitar Voicing Topology, Strumming Rules, and Rhythmic/Arpeggio Patterns

Source: user-supplied research (2026-11-20), companion to
`docs/Black Metal Chords, Scales, Harmonies, and Progressions.md`.
Feeds the VARG strum-engine design
(`docs/superpowers/specs/2026-11-20-varg-strum-engine-design.md`).

## 1. String Pitch Topology and Voicing Constraints

Unlike a piano chord, a six-string guitar voicing acts as a 6-voice pitch
matrix ordered monotonically from lowest to highest, where each string can
sound at most one pitch at a time (or remain silent/muted):

**Standard 6-Voice Pitch Reference (6 -> 1)**: E2 – A2 – D3 – G3 – B3 – E4.
Adjacent voices sit a perfect fourth (5 semitones) apart, except between
voices 3 and 2 (G3 to B3), which span a major third (4 semitones).

**Common Transposed & Open-Drone Layouts**: Black metal and dark metal
voicings frequently shift this matrix down to D# Standard
(D#2–G#2–C#3–F#3–A#3–D#4), D Standard (D2–G2–C3–F3–A3–D4), C# Standard
(C#2–F#2–B2–E3–G#3–C#4), Drop D (D2–A2–D3–G3–B3–E4), Drop C
(C2–G2–C3–F3–A3–D4), or modal/drone sets such as Open A minor
(E2–A2–E3–A3–C4–E4) and DADGAD (D2–A2–D3–G3–A3–D4).

**Black Metal Voicing Rules**:

- *Static Open-String Pedal + Parallel Planing*: Keep one voice fixed on an
  unfretted open pitch — typically the lowest bass voice (e.g., E2) or an
  upper-middle voice (e.g., G3) — while translating a fixed power chord or
  barred minor shape along two or three adjacent voices. Because the pedal
  remains static while the upper shape moves in parallel, this automatically
  generates shifting slash chords, b2/b9 friction, and ambiguous minor
  harmonies.
- *Skipped-String & Moving Fifths*: Voice perfect fifths across non-adjacent
  string voices for wider harmonic spread, or hold a root voice fixed while
  shifting its fifth voice by a semitone (P5 to b5 or b6).
- *Dyad & Single-Note Implied Harmony*: Rapid black metal single-note or
  two-note lines function structurally as broken chord progressions
  (Im – bVII – bVI – V in natural minor, Im – bII – Im – bII in Phrygian, or
  harmonic minor V7 – bVI deceptive shifts) by alternating a lower root pedal
  with upper melodic steps featuring minor seconds (b2), minor thirds (b3),
  and tritones (#4/b5).

## 2. Musical Rules of a Strum (Order, Spread, Coverage, and Velocity)

Triggering all chord tones at the exact same timestamp produces a sterile
keyboard block chord. A realistic strummed or tremolo-strummed chord obeys
four music-theoretic rules:

1. **Directional Pitch Ordering**: A downstroke (D) triggers active voices
   sequentially from lowest pitch to highest (Voice 6 -> 1), while an
   upstroke (U) reverses the order from highest pitch to lowest (Voice 1 -> 6).
2. **Micro-Timing Spread**: Notes within a single strum stagger across a
   15 to 35 ms total window (3 to 6 ms per string), compressing to 12 to 18 ms
   for fast, aggressive strokes and expanding to 35 to 45 ms for slow,
   atmospheric strums. Only the first note of the strum locks to the rhythmic
   grid; remaining voices follow sequentially by the spread interval.
3. **Asymmetric Voice Subset & Velocity**: Downstrokes normally sound all
   available chord voices (up to 6 notes) at full dynamic weight. Upstrokes
   sound lighter because they usually catch only the top 3 or 4 highest-pitched
   voices (omitting the bass strings) and drop by 10 to 20 velocity points
   relative to downstrokes.
4. **Pendulum Grid-Lock Rule**: Stroke direction (D vs. U) is bound to the
   metric grid rather than to consecutive sounding notes. On an 8th-note grid
   (1 & 2 & 3 & 4 &), numbered beats are always D and & offbeats are always U;
   on a 16th-note grid (1 e & a), numbered beats and &s are D while e and a
   are U. When a rest or tie occurs, no notes fire, but the directional
   alternation keeps advancing with the grid.
5. **Ghost Hits & Pre-Change Transients**: Silent grid steps can optionally
   trigger short, damped muted chord thuds (X) to sustain rhythmic drive,
   while subtle transient slide/fret noises sit immediately before a chord
   transition rather than directly on the beat.

## 3. Rhythmic Subdivisions, Arpeggio Orders, and Strumming Cells

| Style / Pattern | Metric Grid & Subdivision | Note / Chord / Strum Sequence Rules |
|---|---|---|
| Un-muted Tremolo ("Buzz") | 4/4 continuous 8ths, 16ths, 32nds, or triplets | Alternates D-U-D-U continuously across single notes, dyads, or full 4–6 voice minor/diminished chords; overlapping sustain/gate creates a dense harmonic drone rather than staccato separation |
| Quarter + Triplet Motif | 4/4 mixed beat divisions | Alternates a quarter-note chord strike (or pedal) with three triplet notes or tremolo chords (1 \| 2-trip-let \| 3 \| 4-trip-let) |
| Standard & Reverse Gallop | 4/4 16th-note grid (1 e & a) | Standard: triggers 1, &, a (D . D U), omitting e. Reverse: triggers 1, e, & (D U D .), omitting a. Alternating both across beats yields driving asymmetrical momentum |
| D-Beat / Crust Strumming | 4/4 fast 8ths or 16ths (180+ BPM feel) | Continuous slash: straight D-U-D-U. Wolfbrigade/Tragedy cell: eighth – quarter – eighth (x - x -- x - x - x), repeating twice per bar with chord changes optionally offset from cell boundaries. Crust cell (x - xx - xx): D - D U - D U |
| Odd Polymetric Riff Cells | Asymmetric 16ths cycling over 4/4 | Loops an odd 16th-note grouping (such as 5/16 or 21/16) across 4/4 bars, accenting note 1 of every cycle and truncating/adjusting the final cycle to align at 2-bar or 4-bar boundaries |
| Atmospheric / DSBM / Blackgaze Strums | 4/4 8th-note grid (1 & 2 & 3 & 4 &) or 3/4 | Campfire pattern: D . D U . U D U. Backbeat push: D . D U D U D . (accenting beats 2 and 4). Steady 8th rock: D D D D D D D D (accents on 2 and 4). 3/4 waltz strum: D . D U D U |
| Broken Chords & Arpeggio Cycles | 3/4, 6/8, or 12/8 8th-note grids | Broken chords sustain each tone into the next so the full chord rings; arpeggios trigger one non-overlapping voice at a time. Using Bass (B), Lower-mid (L), Upper-mid (M), and Top (H) voices: B – L – M – H – M – L or B – L – M – H – L – H – M, or a 2-voice broken triad [M+H] – B – [M+H] – B |

**Blast-Beat Rhythmic Coupling**: In fast 4/4 blast-beat passages (such as
16th-note blasts near 100–120 BPM or 8th-note double-time equivalents around
200+ BPM), drums either alternate kick and snare on every 16th note (with
cymbal hits on quarter notes or uneven beats) or trigger kick, snare, and
cymbal in unison ("hammer blast"). Subtly attenuating kick velocity on even
16ths relative to odd 16ths avoids mechanical flatness, while half-time
transitions shift emphasis to beats 1 and 3.

## 4. Synth, Keyboard, and Generative Arpeggiator Patterns

When translating black metal textures directly to synthesizers and
sequencers, subgenre traditions split into three distinct compositional
architectures:

**Symphonic Black Metal** (e.g., Emperor, Dimmu Borgir):

- *Register Partitioning*: Low strings, basses, and low choir voices lock to
  root pedals beneath the guitars; mid-register strings or cathedral pipe
  organs double guitar riffs and rhythmic accents; upper strings, woodwinds,
  or lead synths execute countermelodies or foreground arpeggiated chord
  patterns above sustained choir/string pads.
- *Harmonic & Tempo Structure*: Combines natural/harmonic minor with Phrygian
  and Locrian inflections, diminished, augmented, and major-7th triads,
  tritone pedals, and chromatic mediant pivots, contrasting blast-beat
  intensity with 110–140 BPM "epic stride" mid-tempos so harmonic shifts
  remain legible.

**Dungeon Synth**:

- *Modal & Drone Structure*: Built from Aeolian, Dorian, Phrygian, or
  Phrygian Dominant scales anchored by sustained root-and-fifth drones and
  parallel fifths, fourths, and octaves (mimicking medieval organum), favoring
  stepwise cadences and melodic 2nds, octaves, and tritones over dense
  extended chords.
- *Sequencing & Timing*: Frequently beatless or supported by sparse martial
  snare/timpani ostinati at 40–80 BPM in A–B vignettes or looped cyclical
  motifs. Early Norwegian works deliberately favor loose, expressive rubato
  phrasing over strict grid quantization.

**Industrial Black Metal & Algorithmic Arpeggiators**:

- *Clockwork Integration*: Locked tightly to 130–200 BPM grids, pairing
  blast beats, skank beats, or four-on-the-floor pulses with EBM-style
  sequenced bass ostinati, syncopated synth arps, cluster voicings, and
  sparse mantra-like hooks that mirror or counterpoint guitar lines.
- *Arpeggiator Pattern Modes & Polyrhythms*: Cycle held chords through
  **Up** (low-to-high), **Down** (high-to-low), **Converge** (outer voices
  alternating inward), **Diverge** (middle voices expanding outward),
  **Thru** (entry order), or **Random** modes across 1/8, 1/16, or 1/32
  subdivisions. Layering two concurrent arpeggiators at different rates
  (1/8 paired with 1/16) generates shifting polyrhythmic accents, while
  varying gate length between 60% and 80% balances percussive attack against
  harmonic wash.
- *Pedal-Anchored Arpeggio Inversions*: Voice-lead triads across two octaves
  while keeping the lowest pitch fixed across chord changes via close
  inversions (analogous to shifting from root position to 1st and 2nd
  inversions while retaining a common bass tone).
