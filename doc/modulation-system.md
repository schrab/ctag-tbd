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
| 40–41–42 (trig) | Marbles T1/T2/T3 | T1 random gate, T2 master clock, T3 random gate |

All Marbles output slots are re-assignable (−1 = off) from the UI; pointing
them at MIDI-owned slots follows last-writer-wins (Marbles writes first in
the block, before the sound processors run).

## Audio Call Flow

```
SPManager::audio_task()
  └─ MarblesEngine::Process(cv_buffer, trig_buffer)   // ~1378 Hz
       ├─ MIDI clock edges → GateFlags stream (1-tick pulses)
       ├─ t_generator_.Process(...)        // master clock + T1/T3 gates
       ├─ xy_generator_.Process(...)       // X1..X3 + Y voltages
       └─ write cv 98..101 (volts × 0.2), trig 40..42
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
0xF8 (Timing Clock) → MarblesEngine::OnMidiClock()   // queues a clock edge
0xFA (Start)        → MarblesEngine::OnMidiStart()   // one-shot pattern reset
0xFB (Continue)     → MarblesEngine::OnMidiStart()
0xFC (Stop)         → (edges simply stop arriving)
```

Clock edges are queued in an atomic counter by the MIDI task and drained one
per block tick by the audio task into a gate stream for `RampExtractor`.
Edges closer together than one block (> 1378 Hz clock rate) collapse.

## Parameters

Engine parameters are plain clamped setters on `MarblesEngine` (see the
header for the full list). Highlights:

- **Clock page**: t clock source (Int/MIDI), rate (±60 st, shown as Hz),
  range (0.25×/1×/4×), X clock source (T123/T1/T2/T3/MIDI), [Reset].
- **T page**: model (incl. Markov), bias, jitter, pulse-width mean/deviation,
  t déjà-vu (Off/On/Lock), T1/T2/T3 trig slot assignment, shared déjà-vu
  length (1–16) and amount (0–1).
- **X page**: scale (Major/Minor/Pentatonic/Pelog/Bhairav/Shri — the 6
  preset scales from `marbles/settings.cc`), voltage range, control mode,
  spread/bias/steps, register mode + register value, X déjà-vu, X1..X3 slot
  assignment.
- **Y page**: divider (1/64…1/1), voltage range, spread/bias/steps, Y slot.

Not ported (hardware-only features): CV recording of custom scales
(`scale_recorder`), clock self-patching detection (X clock source is a menu
parameter instead), the external "note" CV for X register/transposition
(replaced by the register-value parameter).

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
  A Marbles T gate HIGH therefore switches a bool parameter OFF. This is
  long-standing firmware behavior (MIDI CC < 64 sets the same convention);
  changing it would alter existing presets.
- Trig slots 0–39 reuse the CV-table names (A.Note, G.Sust, …) — as gates
  they mean the note-on/CC≥64 *gate*, not the value.

## UI Navigation (PANEL_MOD)

Main menu: Clock / T / X / Y / CC Slots. All parameter subpages are
scrolling row lists: encoder moves the cursor, OK toggles edit mode, encoder
changes the value, BACK exits edit → subpage → main. `[Reset]` (Clock) and
`[Learn]` (CC Edit) are action rows triggered with OK. The CC slots are
labelled CC1..CC8, matching their names in the parameter mapping list
(cv slots 90–97).

## Persistence

`/spiffs/data/mod-config.jsn` via RapidJSON (type-checked reads, 512-byte
stream buffers — SaveConfig runs on the 4 KB UIMenu task stack):

```json
{
  "marbles": {
    "tExternal": false, "tModel": 0, "tRange": 1, "tRate": 0.0,
    "tBias": 0.5, "tJitter": 0.0, "tPwMean": 90, "tPwStd": 0, "tDejaVu": 0,
    "xyClockSource": 0,
    "xScale": 0, "xRange": 2, "xControl": 0, "xRegister": false,
    "xRegisterValue": 0.5, "xSpread": 0.5, "xBias": 0.5, "xSteps": 0.5,
    "xDejaVu": 0,
    "yRange": 2, "ySpread": 0.5, "yBias": 0.5, "ySteps": 0.5, "yDivider": 11,
    "dvAmount": 0.0, "dvLength": 8,
    "tSlot": [40, 41, 42], "xSlot": [98, 99, 100], "ySlot": 101
  },
  "ccSlots": [ { "cc": -1, "chan": 0, "cvSlot": 90 }, ... ]
}
```

`MarblesEngine::SaveConfig()` runs from the page's `deinit()` when dirty;
`LoadConfig()` runs from `Init()` (called from `Midi::Init()`). Old config
files with `lfo1/2`, `seq*`, `gate*`, `tempo` keys simply ignore them.

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
déjà-vu period-N looping, and slot bounds for the new 98–101 / 40–42 slots.
