# Modulation System (Marbles port)

## Overview

The modulation system is a port of Mutable Instruments **Marbles** (Émilie
Gillet, MIT license) replacing the former home-grown LFO / CV-sequencer /
gate-generator engine. It generates random **clocks, gates and quantized CV**
and writes them into the shared `cv_buffer[]` / `trig_buffer[]` arrays at
audio block rate (~1378 Hz), which sound processors read via
`ProcessData::cv[]` / `trig[]`.

Sections (same structure as the hardware module):

| Section | Outputs | What it does |
|---------|---------|--------------|
| **t** | T1, T2, T3 → trig slots 40–42 | T2 is the master clock (50 % duty). T1/T3 are random gates derived from it: 7 models — Coins (complementary Bernoulli), Clusters, Drums, Independent Bernoulli, Divider, 3-States and **Markov** (implemented upstream but unreachable from the panel UI; exposed here as model 6). |
| **X** | X1, X2, X3 → cv slots 98–100 | Random CV quantized to a selectable scale, with spread/bias/steps (distribution shape), register (ASR) mode, control mode (identical/bump/tilt) and déjà-vu loop. |
| **Y** | Y → cv slot 101 | Random CV derived from X2's clock through a divider (1/64 … 1/1), with its own spread/bias/steps/range. |

Clocking: the t master clock is either **internal** (rate ±60 semitones
around 0.5 / 2 / 8 Hz by range) or **external = MIDI clock** (24 PPQN, fed
through Marbles' own `RampExtractor`, which tolerates jitter and predicts the
next edge). The X section can clock on T1, T2, T3, all three, or MIDI clock.
MIDI Start/Continue reset the patterns to the downbeat.

CV scaling: Marbles thinks in volts (±5 V full range). TBD's cv slots are
roughly ±1.0, so outputs are scaled by **0.2** (FULL ±5 V → ±1.0, POSITIVE
0…5 V → 0…1, NARROW 0…2 V → 0…0.4).

## Key Files

| File | Role |
|------|------|
| `main/MarblesEngine.hpp` / `.cpp` | Engine wrapper: parameter state, MIDI-clock input, cv/trig output mapping, JSON persistence, CC-slot routing |
| `components/mutable/eurorack/marbles/` | Original Marbles sources compiled directly (random/, ramp/, resources.cc) |
| `components/mutable/CMakeLists.txt` | Adds the marbles sources to the build |
| `main/menupages/UIMenuPageMod.hpp` / `.cpp` | PANEL_MOD UI — Clock / T / X / Y / CC Slots subpages |
| `main/CVSlotNames.hpp` | 102-entry CV slot name table + marbles trig slot names |
| `main/IOCapabilities.hpp` | N_CVS=102, N_TRIGS=43 (BBA); web-UI slot lists include MB_* entries |
| `components/ctagSoundProcessor/helpers/ctagSlotBounds.hpp` | Slot-index clamping shared by every CV/trig setter |

## Slot Index Safety

`cv_buffer` and `trig_buffer` are **stack locals of `audio_task`**
(`main/SPManager.cpp`). An out-of-range slot index therefore writes over live
audio-task stack variables on every block. Every setter routes through
`ClampCVSlot()` / `ClampTrigSlot()` in `ctagSlotBounds.hpp`, which keep valid
indices and map anything outside `[0, N_CVS)` / `[0, N_TRIGS)` to `-1`
(unassigned) — so a hand-edited or truncated `mod-config.jsn` cannot corrupt
the stack. Bounds are verified by host tests in `tests/host/`.

## Slot Allocation

| Slots | Source | Description |
|-------|--------|-------------|
| 0–89 | MIDI-mapped | G_*, A_*, B_*, C_*, D_* from MIDI CC/note events |
| 90–97 | Dynamic CC | 8 MIDI-learnable CC-to-CV mappings (unchanged from the old engine) |
| 98–100 | Marbles X1–X3 | Quantized random CV |
| 101 | Marbles Y | Divided random CV |
| 102–103 | SEQ G1/G2 | GATE level, slewed 0..1 |
| 104–105 | SEQ A1/A2 | ARP pitch, 1 V/oct: `(note − root)/12 × 0.2` |
| 40–41–42 (trig) | Marbles T1/T2/T3 | T1 random gate, T2 master clock, T3 random gate |
| 43–44 (trig) | SEQ G1/G2 | GATE window, active-low |
| 45–46 (trig) | SEQ A1/A2 GATE | ARP step gate window (55 %, 100 % on slide steps), active-low |
| 47–48 (trig) | SEQ A1/A2 ACC | 0 on accent steps, idle 1 |
| 49–50 (trig) | SEQ A1/A2 SLIDE | 0 on slide steps, idle 1 |

All Marbles and SEQ output slots are re-assignable (−1 = off) from the UI;
pointing them at MIDI-owned slots follows last-writer-wins (both engines
write first in the block, before the sound processors run).

## Audio Call Flow

```
SPManager::audio_task()
  └─ ModTransport::Process(dt)                        // shared transport
  |    ├─ drain the MIDI-clock tick counter ONCE (one funnel, no double drain)
  |    ├─ measured-BPM EMA, running flag, FA start-reset latch
  |    └─ publish TicksThisBlock / IsRunning / ExternalGateFlags
  └─ SeqEngine::Process(cv, trig, dt)
       ├─ clock bus: per-source Global/Internal/MIDI, 13-division PPQ table
       ├─ 2× GATE: 19 modes (Barlow gen, 16 presets, TMR, MRKV) → mask,
       |  gate window slew, cv 102..103 + trig 43..44
       └─ 2× ARP: 5 generative algos → 16-step pattern, pitch/gate/acc/slide
          → cv 104..105 + trig 45..50
  └─ MarblesEngine::Process(cv_buffer, trig_buffer)   // ~1378 Hz
       ├─ RampExtractor feed = ModTransport::ExternalGateFlags() (1 stream/block)
       ├─ t_generator_.Process(...)        // master clock + T1/T3 gates
       ├─ xy_generator_.Process(...)       // X1..X3 + Y voltages
       └─ write cv 98..101 (volts × 0.2), trig 40..42 (ACTIVE-LOW)
  └─ Codec::ReadBuffer() → DC cut → noise gate
  └─ sp[0]->Process(pd)                    // reads cv[] and trig[]
  └─ (mono) sp[1]->Process(pd)
  └─ Codec::WriteBuffer()
```

The marbles generators are initialized with `sr = 44100/32 = 1378.125` and
run with `size == 1`, so all their internal rate constants stay in absolute
Hz at the block tick.

## MIDI Wiring (`main/Midi.cpp`)

```
0xF8 (Timing Clock) → ModTransport::OnMidiClock()   // the ONE tick funnel
0xFA (Start)        → ModTransport::OnMidiStart()   // running + re-phase latch
0xFB (Continue)     → ModTransport::OnMidiStart()
0xFC (Stop)         → ModTransport::OnMidiStop()    // MIDI-clocked SEQ sources freeze
```

Clock edges are queued in an atomic counter by the MIDI task and drained
**once per block by ModTransport::Process** — Marbles (RampExtractor gate
stream) and SeqEngine (integer PPQ ticks) both read the published per-block
state; nobody drains the raw counter themselves. Edits to BPM (panel/tap/
measured) also land in ModTransport; `MarblesEngine::GetBPM/SetBPM` are
delegating wrappers.

## SEQ Modulators (GATE ×2 + ARP ×2)

A second modulation engine family (rack T3 port, `main/SeqEngineCore.hpp`
pure generators + `main/SeqEngine.{hpp,cpp}` shell) running **alongside
Marbles** — no engine switching. Two instances each, shared root/scale:

- **GATE**: 16 canonical trance-gate presets + Barlow metric-indispensability
  generative mode (0) + Turing-machine bit mutation (17, TMR) + Hamming-Markov
  preset transitions (18, MRKV), Sioros-Guedes syncopation (0–100 %), gate
  length (0–100 % of the division cycle), 13 divisions (8/1 … 1/32T incl.
  triplets). Dual output: slewed level CV **and** active-low gate trig.
- **ARP**: 5 algorithms × 3 octave ranges (Acid 303 / Phrase Motif / Markov
  Walker / Metropolix accumulator / Déjà-Vu shift), 20 scales, shared
  root/scale/neo-op, W7 functional drift + Neo-Riemannian triad drift,
  fill bars / cadential turnarounds / mutation cadence. Per-instance pitch
  CV, gate trig, **accent + slide trig on every algorithm**.
- **GLIDE** (per ARP, 0–100): 0 = instant pitch (default — TBD03's
  `slide_level` blend performs the 303 glide on slide steps); 1–100 = engine
  slew in ms for plugins without a slide input. Slide steps always assert
  the SLIDE trig regardless.
- **TBD03 wiring**: `pitch` cv → cv 104/105 (1 V/oct), `trigger` → trig
  45/46 (retrigger on the 1→0 edge = step start), `accent` → trig 47/48
  (sampled at that edge), `slide` → trig 49/50 (gate-follow; the plugin's
  `slide_level` blends the glide).
- **Clock**: per source Global (follows the MOD CLOCK page's MIDI-vs-internal
  choice, `ModTransport::GetGlobalClockMidi`) / Internal (fractional PPQ
  accumulator at the global BPM) / MIDI (integer ticks from the funnel;
  frozen while the transport is stopped). FA re-phases.
- **Link surface**: `/link/seqSet {"k":…,"v":…}` scalar setters (keys
  `g1mode g1dens g1sync g1glen g1clk g1div g1tslot g1cvslot` + `g2*`,
  `a1algo a1p1..a1p4 a1glide a1clk a1div a1pslot a1gslot a1aslot a1sslot`
  + `a2*`, `shmroot shmrootmode shmneoop shmscale`, `globalext`, `_save`);
  `link/pushSeq` state push on the 10 Hz hash-diff (613 B, flat build);
  stream field `"sq"` (32-bit snapshot: bits 0-1 gate, 2-3 arp gate, 4-5
  accent, 6-7 slide, 8-11/12-15 step index, 16-23/24-31 note).
- **Panel**: PANEL_SEQ (wheel slot after MOD): MAIN (4 sources + live strip)
  → Gate 1/2 + ARP 1/2 setup pages, full-word labels.

## Parameters

## Parameters

Engine parameters are plain clamped setters on `MarblesEngine` (see the
header for the full list). The PANEL_MOD UI mirrors the hardware module:

- **Knobs page** — exactly the module's 8 knobs + the DEJA VU length button,
  in panel order: Deja Vu amount, Loop Length, Clock Rate (BPM), T Bias,
  T Jitter, X Spread, X Bias, X Steps, Y Rate (divider). Labels follow the
  manual's terminology.
- **Clock page**: t clock source (Int/MIDI), **BPM** — the GLOBAL transport
  tempo (20–480; set manually, via [Tap], or measured from the MIDI clock
  at 24 ticks/beat and adopted while the external clock runs; readable by
  other plugins via `MarblesEngine::GetBPM()`), fine Rate (±60 st shown in
  Hz — the Marbles clock = BPM × range × 2^(fine/12)), Clock Range
  (0.25×/1×/4× — multiplies the Marbles clock only, never the BPM),
  X clock source (T123/T1/T2/T3/MIDI), **[Tap]** (average of last 4 taps
  sets the BPM, 3 s gap restarts the sequence), [Reset] (pattern reset).
- **T Setup page** (the module's button/switch equivalents): model (incl.
  Markov), Clock Range, Gate Length (+ variance — the hold-[E] knob
  remap on hardware), t déjà-vu, T1–T3 trig slot assignment.
- **X Setup page**: scale, Voltage Range in octaves (2/5/10 — the module's ±V
  ranges expressed for 1 V/oct, since TBD slots are note-scaled), Diversity
  (the module's channel-diversity switch), X déjà-vu, X1–X3 slot assignment.
- **Y Setup page**: Voltage Range in octaves, Y Spread/Bias/Steps (hidden
  settings on the hardware module — hold-[N] knob remaps), Y slot
  assignment. Y is never affected by déjà-vu, matching upstream
  `marbles.cc` and the manual.

Not ported (hardware-only features): CV recording of custom scales
(`scale_recorder`), clock self-patching detection (X clock source is a menu
parameter instead), and the X external-processing/register mode ([O] — it
samples the SPREAD CV input jack; the BBA has no CV inputs, so the engine
hardcodes `x.register_mode = false` like upstream's Y section).

## Dynamic CC Routing (unchanged)

8 slots (0–7) map incoming MIDI CC messages to CV buffer positions 90–97,
with MIDI learn from the CC Slots subpage — identical to the previous
engine, and old `mod-config.jsn` `ccSlots` entries carry over.

## Mapping Modulation Sources to Parameters (OLED)

Long-press a parameter in the params editor (or select **MAP** in the channel
menu, which drops into the param list with a transient "hold param = map src"
hint). The **MOD SOURCE** page shows a single, capability-filtered list:

- Numeric parameters (CV consumers) see: `[Learn CC]` → None → MB.X1/X2/X3/Y
  → learned CC1..CC8 → the fixed MIDI CV sources (0–89).
- Bool parameters (gate consumers) see: None → MB.T1/T2/T3 → the fixed MIDI
  gate sources (trig 0–39).

There is deliberately **one list, not separate CV/TRIG fields**: every
parameter consumes exactly one source kind (`MK_FLT_PAR` reads `cv[]`,
`MK_BOOL_PAR` reads `trig[]`), so the page shows only the sources that param
can actually use. The row also shows the param's previous assignment
(`was:…`). OK selects and saves, BACK exits without saving.

**`[Learn CC]`**: OK starts a MIDI capture ("Send CC now..."); the next
incoming CC (any channel/number) is routed through a free dynamic slot
(90–97) and bound to the parameter automatically — no visit to the CC Slots
page needed. Requires a free slot (all 8 taken → "All 8 CC slots full!").

The MOD → **CC Slots** page remains as the review/cleanup screen for the 8
dynamic (channel, CC) bindings — it is the only route for CCs the fixed
MIDI table doesn't cover (~20 standard CC numbers on channels 1–4/global).

### Gotchas

- **Gate polarity is active-low**: `MK_BOOL_PAR` maps `trig == 1` to *false*.
  A gate HIGH therefore switches a bool parameter OFF. This is long-standing
  firmware behavior (MIDI CC < 64 sets the same convention); changing it
  would alter existing presets.
- **Marbles T gates were INVERTED to match (2026-11-13)**: T1/T2/T3 wrote
  active-high (`gates ? 1 : 0`) — the only active-high writers in the tree.
  They now write `gates ? 0 : 1` (idle 1) so a Marbles T gate retriggers
  TBD03-style consumers on the gate-START edge like every other source.
  Existing T→trig mappings flip to the correct behavior.
- **Trig idle = 1, written every block**: SEQ (and Marbles) write every owned
  slot every block, so an idle trig reads 1 even over a zeroed buffer.
- Trig slots 0–39 reuse the CV-table names (A.Note, G.Sust, …) — as gates
  they mean the note-on/CC≥64 *gate*, not the value.

## UI Navigation (PANEL_MOD)

Main menu: Knobs / Clock / T Set / X Set / Y Set / CC Slots. All parameter subpages are
scrolling row lists: encoder moves the cursor, OK toggles edit mode, encoder
changes the value, BACK exits edit → subpage → main. `[Reset]` (Clock) and
`[Learn]` (CC Edit) are action rows triggered with OK. The CC slots are
labelled CC1..CC8, matching their names in the parameter mapping list
(cv slots 90–97).

## Persistence

`/spiffs/data/mod-config.jsn` — one shared file, **read-merge-write per
section** through `ModTransport::ReadSectionJSON/WriteSectionJSON` so the
owners never clobber each other (every member type-checked + clamped on
load; unknown members ignored):

```json
{
  "transport": { "bpm": 120.0, "ext": 0 },
  "marbles": {
    "tExternal": false, "tModel": 0, "tRange": 1, "tFine": 0.0, "tRate": 0.0,
    "tBias": 0.5, "tJitter": 0.0, "tPwMean": 90, "tPwStd": 0, "tDejaVu": 0,
    "xyClockSource": 0,
    "xScale": 0, "xRange": 2, "xControl": 0, "xSpread": 0.5,
    "xBias": 0.5, "xSteps": 0.5, "xDejaVu": 0,
    "yRange": 2, "ySpread": 0.5, "yBias": 0.5, "ySteps": 0.5, "yDivider": 11,
    "dvAmount": 0.0, "dvLength": 8,
    "tSlot": [40, 41, 42], "xSlot": [98, 99, 100], "ySlot": 101
  },
  "ccSlots": { "slots": [ { "cc": -1, "chan": 0, "cvSlot": 90 }, ... ] },
  "seq": {
    "gates": [
      {"mode": 1, "dens": 8, "sync": 0, "glen": 0.5, "clk": 0, "div": 7,
       "trigSlot": 43, "cvSlot": 102},
      { "... gate 2, mode 3 ...": 0 }
    ],
    "arps": [
      {"algo": 0, "range": 2, "p": [62, 40, 50, 0], "glide": 0, "clk": 0,
       "div": 7, "pitchSlot": 104, "gateSlot": 45, "accSlot": 47, "slideSlot": 49},
      { "... arp 2, algo 1 ...": 0 }
    ],
    "shared": {"rootMode": 0, "root": 48, "neoOp": 0, "scale": 6}
  }
}
```

`"transport".bpm` is the global tempo (legacy fallback: `marbles.bpm`, or
the pre-global-BPM `marbles.tRate` migration); `"ext"` is the SEQ Global
clock source. `"shared".root` is the MIDI note 48+pc (spec example: 48 =
C3). `MarblesEngine::SaveConfig` writes its own `marbles` + `ccSlots`
sections (`{"slots":[...]}` wrapper — the section IO hands out objects);
`SeqEngine::SaveConfig` writes `seq` from the page's `deinit()` when dirty;
`ModTransport::SaveConfig` writes `transport` (marblesSet/seqSet `_save`
and the `globalext` setter). Loads run from `Init()` (called from
`Midi::Init()`).

## DRAM / flash

| Object | Size |
|--------|------|
| `TGenerator` (incl. `RampExtractor` ~1.5 KB) | ~2.2 KB static DRAM |
| `XYGenerator` (4 × `OutputChannel`, 2nd `RampExtractor`) | ~6.5 KB static DRAM |
| `RandomStream` (128-word ring) + misc | ~0.6 KB |
| `resources.cc` LUTs (60 × 387-float ICDF tables etc.) | ~95 KB flash (.rodata) |

## Testing

`tests/host/run_tests.sh` compiles the **real marbles submodule sources**
with plain g++ and runs 7 marbles tests + 4 input-source tests: determinism
(same seed → same sequence), internal master clock rate (2 Hz at default),
external clock extraction from a synthetic 24-PPQN gate stream, X
quantization to the major-scale grid, Markov model reachability/variation,
déjà-vu period-N looping, and slot bounds. `tests/host/test_seq_mod.cpp`
adds the SEQ suites: slot bounds at the new 106/51 space, ModTransport
(tick drain-once, start/stop, BPM clamp + EMA, gate-flag contract, config
section merge), the gate core (preset patterns, Barlow density ordering,
TMR determinism, MRKV transitions, division table with index 7 = 1/16),
the ARP core (20-scale quantization, 1 V/oct pitch convention, per-algo
determinism, accent/slide flags, root modes), and the engine shell (trig
idle = 1, gate window duty, arp pitch/trig, config roundtrip + hostile
input clamping).
