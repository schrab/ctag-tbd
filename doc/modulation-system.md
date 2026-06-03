# Modulation System

## Overview

The modulation system provides tempo-synced CV and trigger sources for sound
processor modulation. It runs at audio block rate (~1378 Hz) from within
`ModEngine::Process()`, called at the top of the audio task loop. All outputs
write into the shared `cv_buffer[0..N_CVS-1]` and `trig_buffer[0..N_TRIGS-1]`
arrays, which sound processors read via their `ProcessData::cv[]` / `trig[]`.

## Key Files

| File | Role |
|------|------|
| `main/ModEngine.hpp` / `.cpp` | Central modulation engine — LFOs, tempo, sequencers, gates, CC routing, persistence |
| `components/ctagSoundProcessor/helpers/ctagTempo.hpp` / `.cpp` | BPM engine with MIDI clock sync, tap tempo |
| `components/ctagSoundProcessor/helpers/ctagSeq16.hpp` / `.cpp` | 16-step CV sequencer with slew and direction modes |
| `components/ctagSoundProcessor/helpers/ctagGate16.hpp` / `.cpp` | 16-step gate generator with probability, swing, accent |
| `main/menupages/UIMenuPageMod.hpp` / `.cpp` | PANEL_MOD UI — all modulation source configuration |
| `main/CVSlotNames.hpp` | 100-entry CV slot display name table (7-char max) |
| `main/IOCapabilities.hpp` | N_CVS=100, N_TRIGS=40 array definitions |

## CV Slot Allocation

| Slots | Source | Description |
|-------|--------|-------------|
| 0–89 | MIDI-mapped | G_*, A_*, B_*, C_*, D_* from MIDI CC/note events |
| 90–97 | Dynamic CC | 8 MIDI-learnable CC-to-CV mappings, configurable channel+CC |
| 98 | LFO1 | Beat-synced or free-running LFO output |
| 99 | LFO2 | Beat-synced or free-running LFO output |

**Sequencer and Gate CV outputs** are configurable via their CV/trig slot
assignments (default: -1 = unassigned). They can write to any slot 0–99.

## Audio Call Flow

```
SPManager::audio_task()
  └─ ModEngine::Process(cv_buffer, trig_buffer)
       ├─ tempoEngine.Tick()                  // advance BPM phase
       ├─ LFO update (lfo0, lfo1)             // write to cv[lfoCVSlot]
       ├─ sequencer[0..1].Process(bpm, ...)   // write CV + trig to assigned slots
       └─ gate[0..1].Process(bpm, ...)        // write trig + accent to assigned slots
  └─ Codec::ReadBuffer()
  └─ DC cut, noise gate
  └─ sp[0]->Process(pd)                       // reads cv[] and trig[]
  └─ (stereo) sp[1]->Process(pd)
  └─ Codec::WriteBuffer()
```

## Tempo Engine (`ctagTempo`)

Namespace: `CTAG::SP::HELPERS::ctagTempo`

### Features
- BPM range: 20–300 (default 120)
- Two clock sources: `INTERNAL` and `MIDI_CLOCK`
- Phase output: `GetPhase()` returns 0.0–1.0 for one beat cycle
- Tap tempo via `OnTapTempo()` (averages last 4 taps)
- MIDI clock: `OnMidiClock()` (0xF8), `Start()` (0xFA), `Continue()` (0xFB), `Stop()` (0xFC)

### Key Methods
```cpp
void SetBPM(float bpm);
float GetBPM() const;
void SetSource(Source src);  // INTERNAL or MIDI_CLOCK
Source GetSource() const;
float GetPhase() const;      // 0.0–1.0, one beat
void Tick();                 // called every audio block
void OnTapTempo();
void OnMidiClock();
void Start();
void Stop();
void Continue();
```

### MIDI Clock Wiring (`main/Midi.cpp:1156-1172`)
```
0xF8 (Timing Clock)  →  tempoEngine.OnMidiClock()
0xFA (Start)         →  tempoEngine.Start()
0xFB (Continue)      →  tempoEngine.Continue()
0xFC (Stop)          →  tempoEngine.Stop()
```

### Persistence
```json
"tempo": {"bpm": 120.0, "source": "internal"}
```

## LFO Engine (built into `ModEngine`)

Two independent LFOs, each configurable:

| Param | Range | Description |
|-------|-------|-------------|
| Shape | 0–4 | Sine, Triangle, Saw, Square, S&H |
| Rate | 0.01–100 Hz | Free-running oscillation frequency |
| Amplitude | 0.0–1.0 | Output scaling |
| CV Slot | -1–99 | Which cv_buffer slot to write to |
| Sync | on/off | When on, phase follows tempo engine (one beat = one cycle) |

### LFO Sync Mode
When `lfoSync=true`, the LFO phase is replaced by `tempoEngine.GetPhase()`
every audio block, making the LFO beat-synced. The rate parameter is ignored
in sync mode.

## Sequencers (`ctagSeq16`)

Namespace: `CTAG::SP::HELPERS::ctagSeq16`

Two identical sequencers (`sequencer[0]`, `sequencer[1]`), each with 16 steps.

### Per-Step Data
- CV value: `float` (−5V to +5V range typical)

### Global Parameters
| Param | Range | Description |
|-------|-------|-------------|
| Step Length | 0.25–4.0 beats | Duration of each step in beats |
| Direction | 0–3 | FWD, BWD, PENDULUM, RANDOM |
| Slew | 0.0–1.0 | Linear interpolation between step values (0 = instant) |
| CV Slot | -1–99 | Which cv_buffer slot to write to |
| Trig Slot | -1–39 | Which trig_buffer slot to fire on step change |

### Direction Modes
- `FWD`: steps 0→1→2→...→15→0
- `BWD`: steps 15→14→...→0→15
- `PENDULUM`: 0→1→...→15→14→...→0 (back and forth)
- `RANDOM`: random step each time

### Process
`Process(bpm, cv_buffer, trig_buffer, block_size)` advances an internal phase
accumulator proportional to BPM × step length. At each step boundary:
1. Direction determines next step index
2. `currentOutput` slews toward `targetOutput` (new step's value)
3. CV written to `cv_buffer[cvSlot]`
4. Trigger fired on `trig_buffer[trigSlot]`

## Gate Generators (`ctagGate16`)

Namespace: `CTAG::SP::HELPERS::ctagGate16`

Two identical gate generators (`gate[0]`, `gate[1]`), each with 16 steps.

### Per-Step Data
| Field | Range | Description |
|-------|-------|-------------|
| Enabled | bool | Whether this step can fire a gate |
| Probability | 0–100% | Chance that gate fires when this step is reached |

### Global Parameters
| Param | Range | Description |
|-------|-------|-------------|
| Step Length | 0.25–4.0 beats | Duration of each step in beats |
| Direction | 0–3 | FWD, BWD, PENDULUM, RANDOM |
| Swing | 0.0–1.0 | Timing offset on even steps (0=straight) |
| Gate Length | 0.1–1.0 | Fraction of step the gate stays high |
| Trig Slot | -1–39 | Which trig_buffer slot to write gates to |
| Accent Slot | -1–99 | Which cv_buffer slot to write accent CV to |
| Accent Amount | 0.0–1.0 | CV level written when gate fires |

### Process
At each step boundary:
1. If step is enabled, roll `rand() % 100` against probability
2. If gate fires: `trig_buffer[trigSlot] = 1` for `gateLength` fraction of step
3. Accent CV: `cv_buffer[accentSlot] = accentAmount` while gate is high
4. Swing shifts timing on even-numbered steps by `swingAmount × 0.1`

## Dynamic CC Routing

8 slots (0–7) that map incoming MIDI CC messages to CV buffer positions 90–97.

| Config | Range | Description |
|--------|-------|-------------|
| CC | -1–127 | MIDI CC number to listen for (−1 = disabled) |
| Channel | 0–15 | MIDI channel to listen on |

### MIDI Learn
```cpp
ModEngine::StartLearn();  // enters learn mode
ModEngine::StopLearn();   // captures next CC into first free slot
ModEngine::IsLearning();
```

When a CC arrives matching a configured slot:
```cpp
cv_buffer[90 + slot_idx] = value / 127.0f;
```

## UI Navigation (PANEL_MOD)

`main/menupages/UIMenuPageMod.cpp` — the modulation configuration panel.

### Main Menu (SP_MAIN)
8 items, scrollable:
| # | Label | Navigates to |
|---|-------|-------------|
| 0 | LFO1 | SP_LFO1 |
| 1 | LFO2 | SP_LFO2 |
| 2 | Tempo | SP_TEMPO |
| 3 | CC Slots | SP_CC_SLOTS |
| 4 | Seq1 | SP_SEQ1 |
| 5 | Seq2 | SP_SEQ2 |
| 6 | Gate1 | SP_GATE1 |
| 7 | Gate2 | SP_GATE2 |

### Subpage: LFO1/LFO2
- Shape (encoder-select, OK to edit)
- Rate (Hz, OK to edit)
- Amplitude (0–1, OK to edit)
- Output CV slot (−1–99, OK to edit)

### Subpage: Tempo
- BPM (edit: 20–300)
- Source (Internal / MIDI Clk)
- [Tap Tempo] (action)

### Subpage: CC Slots
Scrollable list of 8 slots. OK enters CC Edit for selected slot.

### Subpage: CC Edit
- CC number (−1 = disabled, 0–127)
- MIDI Channel (0–15)
- [Learn] / [StopLearn] toggle

### Subpage: SEQ1/SEQ2 — Step List
Scrollable list of 16 steps + [Params]. Each step shows CV value.
- OK: enter CV edit mode (encoder adjusts value)
- Long press: (reserved for trig toggle)
- OK on [Params]: enter global params sub-screen

### Subpage: SEQ1/SEQ2 — Params
- Step Length (0.25–4.0 beats)
- Direction (Fwd/Bwd/Pend/Rand)
- Slew (0.0–1.0)
- CV Slot (−1–99)
- Trig Slot (−1–39)

### Subpage: GATE1/GATE2 — Step List
Scrollable list of 16 steps + [Params]. Each step shows ON/OFF + probability.
- OK: toggle step enabled
- Long press: enter probability edit mode (encoder adjusts 0–100%)
- OK on [Params]: enter global params sub-screen

### Subpage: GATE1/GATE2 — Params
- Step Length (0.25–4.0 beats)
- Direction (Fwd/Bwd/Pend/Rand)
- Swing (0.0–1.0)
- Gate Length (0.1–1.0)
- Trig Slot (−1–39)
- Accent Amount (0.0–1.0)

## Persistence

All modulation config saved to `/spiffs/data/mod-config.jsn` via RapidJSON.

```json
{
  "lfo1": {"shape": 0, "rate": 1.0, "amp": 0.5, "cvSlot": 98, "sync": false},
  "lfo2": {"shape": 0, "rate": 2.0, "amp": 0.5, "cvSlot": 99, "sync": false},
  "tempo": {"bpm": 120.0, "source": "internal"},
  "ccSlots": [
    {"cc": -1, "chan": 0, "cvSlot": 90},
    {"cc": -1, "chan": 0, "cvSlot": 91}
  ],
  "seq0": {
    "steps": [0.0, 0.1, ...],
    "stepLength": 1.0,
    "direction": 0,
    "slew": 0.0,
    "cvSlot": -1,
    "trigSlot": -1
  },
  "gate0": {
    "enabled": [true, false, ...],
    "probs": [100, 0, ...],
    "stepLength": 1.0,
    "direction": 0,
    "trigSlot": -1,
    "accentSlot": -1,
    "swing": 0.0,
    "gateLength": 0.5,
    "accentAmount": 0.5
  }
}
```

`ModEngine::SaveConfig()` called from `deinit()` when `dirty=true` (any
encoder change). `ModEngine::LoadConfig()` called from `Init()`.

## DRAM Usage

The modulation engine uses static class members (no heap):

| Object | Size |
|--------|------|
| `tempoEngine` | ~32 bytes |
| `sequencer[2]` | ~160 bytes |
| `gate[2]` | ~120 bytes |
| `lfoPhase/rate/amp/shape/cvSlot/hold/sync` | ~64 bytes |
| `dynCC/dynChan/dynTargetSlot` (8 each) | ~96 bytes |
| **Total** | **~480 bytes static DRAM** |
