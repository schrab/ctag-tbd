# SEQ Modulators — GATE ×2 + ARP ×2 (rack port) — SDD

Status: approved design, pending implementation plan
Date: 2026-11-13
Source material: `~/mi-rp2350/rack-rp2350/src/mod_engine.h` (T3 modulation engine) + `gen_tables.h`
Branch: `ctag-bba-v2-s3` (targets both BBA2/S3 and legacy A1S builds)

## 1. Summary

Port the rack's **rhythmic gate** and **generative ARP** modulators into ctag-tbd as a
second modulation engine family that runs **simultaneously with Marbles** (no engine
switching). Two instances of each, same design as the rack: GATE1/GATE2, ARP1/ARP2
with a shared root/scale. New PANEL_SEQ page after PANEL_MOD. The transport
(BPM / MIDI clock / EMA) is hoisted out of MarblesEngine into a shared `ModTransport`
both engines read. Includes one bug fix: Marbles' T-gate trig outputs are inverted
relative to the system-wide active-low convention.

Resource assessment (2026-11-12, `idf.py size` on build-bba2): DSP flash ~257 KB free
of the 0x380000 ota slot; new code ≈ +30–40 KB. DSP static DIRAM 36.9 % used; engine
adds ≈ 1.5 KB. Audio CPU: generators are event-driven (step math on division wraps,
pattern rebuilds on parameter change / bar wrap), < 1 % of the block budget. C3 flash
2.4 MB free. No blockers.

## 2. Goals / Non-goals

**Goals**
- GATE (×2): 16 canonical trance-gate presets + Barlow metric-indispensability
  generative mode + Turing-machine bit-mutation (TMR) + Hamming-Markov preset
  transitions (MRKV), Sioros-Guedes syncopation, gate length, 13 clock divisions
  (incl. triplets). Dual output: slewed level CV **and** active-low gate/trig output
  (the GATE doubles as a TRIG generator).
- ARP (×2): 5 algorithms × 3 octave ranges (Acid 303, Phrase Motif, Markov Walker,
  Metropolix Accumulator, Déjà-Vu Shift), 20 scales, shared root/scale/neo-op across
  both ARPs, W7 functional drift + Neo-Riemannian triad drift, fill bars / cadential
  turnarounds / mutation cadence. Per-instance outputs: pitch CV, gate trig,
  **accent trig and slide trig on every algorithm** (all five emit the step flags).
- TBD03 compatibility: pitch at 1 V/oct, accent sampled at the retrigger edge,
  slide gate-follow with TBD03's own `slide_level` blend doing the glide.
- PANEL_SEQ (full names, no rack-style 4-char abbreviations) after PANEL_MOD.
- `ModTransport` hoist; host tests; bench verification on hardware.

**Non-goals (deferred)**
- MIDI-note root follow (root modes ship as fixed note + W7 drift + Neo drift).
- Euclid / LFO / ADSR sources from the rack engine.
- Engine switching — both engine families always run.

## 3. Trig convention (system rule) + Marbles fix

The system-wide convention is **active-low: 0 = active, 1 = inactive, idle = 1**:

- `main/Midi.hpp:51-52`: `TRIG_ON 0`, `TRIG_OFF 1`; `main/Midi.cpp:831` memsets the
  plugin-facing trig buffer to 1.
- Every plugin reader is active-low: shared macro `ctagSoundProcessor.hpp:29`
  (`== 1 ? false : true`), TBD03 trigger/accent/slide, Claude, CDelay, EChorus,
  Bjorklund, APCpp, BCSR; Hihat1/MoogFilt retrigger on any edge.
- `doc/modulation-system.md` documents "trig==1 → OFF".

**Bug fix included**: `main/MarblesEngine.cpp:362-364` writes T1/T2/T3 **active-high**
(`gates ? 1 : 0`) — the only outlier. Fix: invert to `gates ? 0 : 1` so Marbles T
gates behave like MIDI gates (retrigger on the gate-start edge for TBD03-style
consumers, no double-firing on both-edge consumers). Existing T-gate mappings flip
to the correct behavior; note this in the commit message and re-verify one T→trig
mapping on the bench.

**SEQ trig outputs follow the convention**: idle 1, active 0, written **every block**
for every owned slot so the idle level is always well-defined (no dependence on
buffer init).

## 4. Architecture

### 4.1 ModTransport — `main/ModTransport.{hpp,cpp}` (new)

Hoists what `MarblesEngine.cpp` owns today into a shared transport:

- `bpm` (20–480, 0.1 steps), tap-tempo state (moves with the panel clock page).
- MIDI clock funnel: the atomic tick counter that `Midi.cpp` F8 handler increments;
  FA/FB/FC transport state (running flag, tick odometer); measured-BPM EMA.
- `ModTransport::Process(dt)` — called once per audio block at the top of
  `SPManager::audio_task`, **before** both engines. Drains the tick counter,
  updates the EMA/transport, publishes `ticksThisBlock()` and `IsRunning()`.
- Both engines consume from it: Marbles' `RampExtractor` is fed the drained ticks
  one at a time (exactly what `OnMidiClock()` did, now sourced from the funnel —
  no double-drain); SeqEngine advances MIDI-clocked sources by
  `ticksThisBlock()` integer PPQ ticks.
- `Midi.cpp` F8/FA/FB/FC handlers call `ModTransport::OnMidiClock/OnMidiStart/
  OnMidiStop` (renamed forwards; MarblesEngine's equivalents are removed).
- Panel setters (tap tempo, BPM edit on the MOD CLOCK page) route to ModTransport.
- Persistence: `mod-config.jsn` gains a top-level `"transport": {"bpm": ...}` object;
  `LoadConfig` falls back to legacy `marbles.bpm`/`tFine` when `"transport"` is
  absent so existing cards keep their tempo. Ownership of bpm/tFine persistence
  moves to ModTransport.

### 4.2 SeqEngine — `main/SeqEngine.{hpp,cpp}` (new)

Static class, same contract shape as MarblesEngine:
`Init / Process(cv_buffer, trig_buffer) / GetOutputSnapshot / SaveConfig / LoadConfig`
+ scalar getters/setters for every parameter. No heap, no allocations, no delays
(`CONFIG_FREERTOS_HZ=100` rules irrelevant — pure compute). Setters run on the
UIMenu/link task; `Process` on the audio task; shared scalars are the same
benign-race class as MarblesEngine's.

State (~1.5 KB static): 2× `GateSource` (mode/dens/sync/glen/mask/step/rng_state
≈ 24 B each), 2× `ArpSource` (algo/range/root/scale/p[4]/step/note/acc/slide/hold
≈ 32 B each), 2× `ArpExt` (~112 B each: 16-step pattern `flags/off/oct` + algo state
union + Q16.16 pitch), shared root/scale/neo-op, per-source division phase accumulators.

**Clock bus**: per-source 13-division table from the rack (`8/1`…`1/32T`, 24 PPQ
basis). Per-source clock selection: Global (follows the transport clock source) /
Internal / MIDI. Internal clock: fractional PPQ phase advance `24 × bpm/60 × dt`.
MIDI clock: integer ticks from `ModTransport::ticksThisBlock()`. FA re-phases
MIDI-clocked sources to step 0 (rack behavior). Internal-clocked sources ignore
MIDI transport.

**GATE**: mask recomputed setter-side on any mode/dens/sync change (engine reads the
uint32 atomically — rack discipline). Per step: level from mask bit, gate window =
`glen` fraction of the division cycle, asymmetric 1 ms attack / 3 ms release slew on
the level CV, trig output = window edges (active-low).

**ARP**: pattern rebuilds setter-side on parameter change and engine-side on bar wrap
(rack cadence: chord drift every 4/8 bars driven by p4, fill bar with cadential
turnaround on the bar before, HYP-1 in-place step mutation on loop wrap, other algos
full rebuild). Note calculation via `degree_to_midi_folded` into the shared 20-scale
table. Root modes: 0 = fixed note (12 chromatic), 1 = W7 functional drift,
2 = Neo-Riemannian triad drift with selectable op (rand/P/L/R/N/S/H/PLR).
MIDI-follow root: deferred (§2).

**GLIDE parameter** (per ARP, 0–100): 0 = instant pitch on every step (default —
TBD03's `slide_level` blend performs the 303 glide on slide steps); 1–100 = engine
slew in ms replacing the rack's fixed ~55 ms exponential glide, for plugins without
a slide input. Slide steps always assert the SLIDE trig regardless of GLIDE.

**Outputs** (per block, every owned slot written):

| Slot | Type | Content |
|---|---|---|
| cv 102 / 103 | `SQ.G1` / `SQ.G2` | gate level, slewed 0..1 |
| cv 104 / 105 | `SQ.A1` / `SQ.A2` | arp pitch, 1 V/oct: `(note − root)/12 × 0.2` (±1.0 = ±60 st; holds last note between gated steps) |
| trig 43 / 44 | `SQ.G1` / `SQ.G2` | gate window, active-low |
| trig 45 / 46 | `SQ.A1 GATE` / `SQ.A2 GATE` | step gate window (55 %, 100 % on slide steps), active-low |
| trig 47 / 48 | `SQ.A1 ACC` / `SQ.A2 ACC` | 0 on accent steps (any algo), idle 1 |
| trig 49 / 50 | `SQ.A1 SLIDE` / `SQ.A2 SLIDE` | 0 on slide steps, idle 1 |

All writes via `ClampCVSlot`/`ClampTrigSlot`. Slot space grows: `N_CVS` 102 → 106,
`N_TRIGS` 43 → 51 (top-level `CMakeLists.txt` BBA defines + `ctagSlotBounds.hpp`
host defaults; audio-task stack +4 floats + 8 bytes). Names added to
`main/CVSlotNames.hpp` and `main/IOCapabilities.hpp` — the MOD SOURCE pages pick
them up through the existing capability-filtered lists.

**TBD03 wiring** (verified against `ctagSoundProcessorTBD03.cpp`):
`pitch` cv → cv 104/105 (plugin multiplies ×12×5 semitones, pitch param units are
128/semitone); `trigger` → trig 45/46 (retrigger on 1→0 edge = step start); `accent`
→ trig 47/48 (read as 0 at that edge); `slide` → trig 49/50 (gate-follow while 0,
plugin blends from `pre_pitch_val` with `slide_level`). Accent "capacitor" stacking
and the HYP-1 accent-probability stay engine-side (they shape *which* steps assert
accent), TBD03's `accent_level`/`slide_level` params stay panel-owned.

**Output snapshot**: 32-bit word for the live UI strip — bits 0–1 G1/G2 gate, 2–3
A1/A2 gate, 4–5 A1/A2 accent, 6–7 A1/A2 slide, 8–11 A1 step index, 12–15 A2 step
index, 16–23 A1 current note, 24–31 A2 current note. Published alongside the
Marbles snapshot over the existing 25 Hz stream (new `"sq"` field).

### 4.3 Link protocol

- New `/link/seqSet {"key": v}` scalar setter table in `LinkEndpoint::handleLinkCommand`,
  mirroring the `marblesSet` pattern: flat string keys, one scalar each, no DOM
  allocation on the C3 (C3 heap ceiling rule). Key names = the C3 DspProxy surface
  (e.g. `g1mode`, `g1dens`, `a1algo`, `a1p1`, `sharedRoot`, `sharedScale`, …).
- Seq state joins the 10 Hz hash-diff push as a flat ≤ ~300 B object
  (`part`/`last` framing rules unchanged) so the page shows live values and
  external mutations (bench) reflect.
- Trig/cv slot assignment rows reuse the existing generic slot-assign mechanism
  (values are plain integers over `seqSet`).

### 4.4 Persistence — `mod-config.jsn`

New `"seq"` object (hand-editable file → every member type-checked on load, clamped
to valid ranges, unknown members ignored):

```json
"seq": {
  "gates": [
    {"mode": 1, "dens": 8, "sync": 0, "glen": 0.5, "clk": 0, "div": 7,
     "trigSlot": 43, "cvSlot": 102},
    { ... gate 2, mode 3 ... }
  ],
  "arps": [
    {"algo": 0, "range": 2, "p": [62, 40, 50, 0], "glide": 0,
     "clk": 0, "div": 7, "pitchSlot": 104, "gateSlot": 45,
     "accSlot": 47, "slideSlot": 49},
    { ... arp 2, algo 1 ... }
  ],
  "shared": {"rootMode": 0, "root": 48, "neoOp": 0, "scale": 6}
}
```

Defaults follow the rack's `mod_engine_init` (gate1 = preset 01 "Classic 1/8th
Alternate", gate2 = preset 03 "4-on-the-Floor Ducking Pump"; arp1 = Acid 303 range 2,
arp2 = Phrase Motif range 2; scale = Aeolian; root = fixed C3 = MIDI 48). Slots
default to the table above so the feature works with zero configuration.

### 4.5 Panel — PANEL_SEQ

Wheel: MIX, TAPE, HOME, MOD, **SEQ**, PARAMS. `PANEL_COUNT` 5 → 6 in
`uimcu/ui/UIMenu.hpp`; indicator-bar segments recompute (128/6 px); top-level scroll
still clamps at the ends. New `uimcu/ui/menupages/UIMenuPageSeq.{hpp,cpp}` following
the `UIMenuPageMod` pattern (list rows, value-encoder live edit, OK = enter/confirm,
BACK via double-click, page-owned sub-page depth via `onBack`).

Sub-pages: **MAIN** (entry list: Gate 1, Gate 2, ARP 1, ARP 2 + live output strip),
**GATE 1**, **GATE 2**, **ARP 1**, **ARP 2**. All labels are full words:

- GATE page rows: Mode (Generative, 16 preset full names — Classic Alternating,
  Trance Offbeat, Floor Pump, Late Pump, Forward Gallop, Reverse Gallop, Rolling
  Gate, Anthem Gate, Dotted Eighths, Tresillo, Son Clave, Rumba Syncopator,
  Chord Stabs, Call & Response, Breakbeat Chop, Accelerating Riser — Turing Mutate,
  Markov Set), Density (1–16), Syncopation (0–100 %), Gate Length, Clock
  (Global / Internal / MIDI), Division (13 entries), Trig Slot, Level Slot, plus a
  16-step `o x` pattern preview row fed from the pushed mask.
- ARP page rows: Algorithm (Acid 303 / Phrase Motif / Markov Walker / Metropolix
  Accumulator / Déjà-Vu Shift) × Range (1–3 oct), P1–P4 with algo-dependent labels
  (Density, Slide/Form/Contour/Pulses/Déjà-Vu, Accent/Transform/Pedal/Ratchet/Steps,
  Mutate/Gravity/Accum/Spread — the rack's per-algo mapping), Root (C..B fixed,
  W7 Drift, Neo-Plagial; Neo-Op sub-row visible only in Neo mode), Scale (20 full
  names), Glide (Instant, 1–100 ms), Clock (Global / Internal / MIDI), Division,
  Pitch Slot, Gate Slot, Accent Slot, Slide Slot.
- BPM and tap tempo stay on the existing MOD CLOCK page (now routing to
  ModTransport over the link — key names unchanged where possible).
- C3 `DspProxy` gains the seq surface (static scalar mirror, same pattern as the
  marbles one); no JSON parsing on the UI task.

### 4.6 Marbles T-gate fix

Invert the three writes in `MarblesEngine::Process` (lines 362–364) to
`gates ? 0 : 1`; update the `doc/modulation-system.md` slot table note. Behavior
change is intentional and must be called out in the commit message.

## 5. Testing

**Host tests** (`tests/host/`, extend `run_tests.sh`; the generators are compiled
from the new engine sources with plain g++, no ESP-IDF):
- Gate mask invariants: each of the 16 presets produces its exact 16-bit pattern;
  GEN/Barlow density ordering (density n ⊇ density n−1 on strong beats); TMR
  determinism given a seed; MRKV transitions only to Hamming-adjacent presets.
- Clocking: division/step advance correctness against `dt` and integer MIDI ticks;
  FA re-phase; triplet divisions.
- ARP: per-algo determinism with a seeded PRNG; scale quantization (all 20 scales,
  folded octaves); pitch CV convention (`(note − root)/12 × 0.2`, 1 V/oct); accent/
  slide flag propagation for all five algos; trig idle level = 1.
- Slot clamping at the new `N_CVS=106` / `N_TRIGS=51` bounds.

**Bench** (single `screenshot.py`-style console session per drive; console open
resets the C3):
- PANEL_SEQ navigation + row editing screenshots; output strip reacts.
- Load TBD03, map pitch/accent/slide/trigger, audition ARP1 → audio OK, no
  `loopHist` anomalies, `u`/`d` counters flat.
- Marbles T→trig mapping after the inversion fix.
- Both A1S and BBA2 builds; flash headroom re-checked via `idf.py size`.

## 6. Risks / Mitigations

- **Marbles T behavior change** — intentional convention fix; documented + bench
  re-check of one existing mapping.
- **ModTransport refactor touches Marbles clocking** — the RampExtractor feed must
  stay per-tick; bench-verify external MIDI clock sync after refactor
  (`/link/midiOut` + tap tempo regression too).
- **Slot space growth** — clamp macros derive from the two defines; host tests
  cover the new bounds; audio-task stack growth is 32 B.
- **UI-task edit bursts** — gate/arp pattern rebuilds are O(16 steps) worst case,
  setter-side only; no per-block allocation.

## 7. File inventory

**New**: `main/ModTransport.{hpp,cpp}`, `main/SeqEngine.{hpp,cpp}`,
`uimcu/ui/menupages/UIMenuPageSeq.{hpp,cpp}`, `tests/host/test_seq_mod.cpp`.

**Modified**: `main/MarblesEngine.{hpp,cpp}` (transport hoist + T-gate inversion),
`main/Midi.cpp` (realtime handlers → ModTransport), `main/SPManager.cpp`
(ModTransport::Process + SeqEngine::Process call order),
`main/LinkEndpoint.cpp` (`/link/seqSet` + seq state push + `"sq"` stream field),
`main/CVSlotNames.hpp`, `main/IOCapabilities.hpp`,
`components/ctagSoundProcessor/helpers/ctagSlotBounds.hpp`, top-level
`CMakeLists.txt` (N_CVS/N_TRIGS), `uimcu/ui/UIMenu.{hpp,cpp}`,
`uimcu/ui/DspProxy.hpp` / `DspProxyLink.cpp` (seq surface),
`uimcu/main/CMakeLists.txt` (new page), `doc/modulation-system.md`,
`tests/host/run_tests.sh`.
