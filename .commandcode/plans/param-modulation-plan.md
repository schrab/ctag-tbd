# Parameter Groups, Channel Switching & Modulation UI — Refined Plan

## Current State vs Original Plan

| Original Plan (Phase 5) | Reality |
|-------------------------|---------|
| FLOAT/INT/ENUM/BOOL/TRIGGER param types | Only `int` and `bool` in JSON. Enums are `int` with `hint`. |
| EDIT / MAP / MAPEDIT / PSET sub-modes | **EDIT** works; **MAP** & **PSET** are placeholders; MAPEDIT doesn't exist |
| 2 LFOs (sine/tri/saw/sq/S&H) | ✅ Exist in `ModEngine`, CV slots 88/89, audio-block-rate Process() |
| 2 Trigger Sequencers | ❌ Does not exist |
| ModMatrix (source→param+gain, 50 slots) | ❌ No matrix. Routing = per-param `"cv"` index in preset JSON. Plugins hard-wire at compile time via `knowYourself()`. |
| MIDI CC as modulation source | ✅ `ModEngine` has 8 dynamic CC slots (CV 80-87) with MIDI Learn. No OLED UI. |
| 100Hz esp_timer | ModEngine runs at audio block rate (~1378 Hz) in `audio_task` |

---

## Problem Summary

| Issue | Root Cause | Impact |
|-------|-----------|--------|
| Empty params for DrumRack, Subbotnik, SpaceFX (50+ params) | `parseParams()` skips `type:"group"` entries + `MAX_PARAMS=64` too small (~134 leaf in DrumRack) | 7+ plugins show nothing on PARAMS page |
| No CV/trig routing UI | `MODE_MAP` is a placeholder | Can't route LFO/CC/modulation to params from OLED |
| No LFO/CC config UI | `ModEngine` has API but no OLED page | Can't set LFO shape, rate, amplitude from device |
| Dual mono ch0/ch1 editing | PARAMS hardcoded to `chan 0` | Can only edit one channel |

---

## How Modulation Actually Works

Each plugin parameter stores a `"cv"` index in its preset JSON (`mp-<id>.jsn`):

```json
{ "id": "gain", "current": 2047, "cv": -1 }
```

- `"cv": -1` → no modulation, uses `"current"`
- `"cv": 0` → reads CV slot 0 (e.g., MIDI note velocity)
- `"cv": 88` → reads LFO1 output

**The `"cv"` field IS the modulation routing.** There is no separate matrix — routing is simply assigning the right CV slot number to each parameter via `SetChannelParamValue(chan, id, "cv", slot)`.

### CV Slot Layout (BBA platform, N_CVS=90)

| Slots | Source | Content |
|-------|--------|---------|
| 0-79 | MIDI → Midi.hpp  | Fixed mapping: notes, velocities, CCs, pitchbend, aftertouch per voice A/B/C/D + global |
| 80-87 | ModEngine | Dynamic CC slots (MIDI-learned CCs), default mapping to 80-87 |
| 88 | ModEngine | LFO1 |
| 89 | ModEngine | LFO2 |

Slot names come from `main/IOCapabilities.hpp` (BBA build) — e.g. `A_NOTE`, `A_VELO`, `G_MW_1`, `LFO1`, `LFO2`.

---

## Phase A: Fix Large Plugin Parameter Display

### Root Cause

`UIMenuPageParams::parseParams()` iterates only the top-level `mui["params"]` array. When `type == "group"`, it does `continue` — skipping the group entirely. For plugins where ALL params are inside groups (DrumRack: 13 groups, 134 leaf params), `paramCount == 0` → **"No params"** displayed.

### Changes

**`UIMenuPageParams.hpp`:**
- Add `GroupInfo` struct: `{ char id[24], char name[24], int firstParamIdx, int paramCount }`
- `MAX_PARAMS` from 64 → **196+** (DrumRack needs ~134)
- Allocate `ParamInfo params[]` and `GroupInfo groups[]` via `heap_caps_malloc(SPIRAM)` instead of static arrays
- Add `MODE_GROUP` to the `Mode` enum

**`UIMenuPageParams.cpp` — `parseParams()` rewrite:**
```cpp
void parseParams() {
    // parse merged JSON from GetCStrJSONActivePluginParams(chan)
    // recurse into p["params"] when type == "group"
    // populate GroupInfo + ParamInfo arrays
    // standalone (non-group) leaf params go into an implicit group
}
```

**Navigation flow:**
```
MODE_SELECT
  → MODE_GROUP      (if groups present, show group list)
  → MODE_EDIT       (if no groups — same as current behavior)
     → MODE_VALUEEDIT (per-param value adjust)

MODE_GROUP:
  - 6-line scrollable list of group names + param count: e.g. `Analogue BD (12)`
  - OK on group → MODE_EDIT (shows only that group's leaf params)
  - BACK → MODE_SELECT

MODE_EDIT (per-group):
  - 6-line scrollable list of leaf params in current group
  - Same value editing as current implementation
  - Long-press on a param → enters MODE_MAP for that param
  - BACK → MODE_GROUP (or MODE_SELECT if no groups)
```

**Standalone (non-group) params:** If a plugin has both top-level leaf params AND groups, the top-level leafs are shown first in MODE_EDIT when entering from MODE_SELECT, then a "— Groups —" separator, or they form an implicit "General" group.

**DRAM caution:** The merged JSON (~17KB for largest plugin) is held in `StringBuffer` (internal DRAM). With ~22KB DRAM remaining, this is tight. The `ParamInfo`/`GroupInfo` arrays go to SPIRAM.

---

## Phase B: Dual Channel Support for PARAMS

### Behavior

When entering PARAMS panel (PANEL_IN → page init):

- **Stereo plugin on ch0 (or ch0 only):** Same as current — single channel editing
- **Two different mono plugins:** Show channel selector in MODE_SELECT

### MODE_SELECT with Dual Mono

```
EDIT Ch0: WTOsc     ← cursor 0
EDIT Ch1: SubSynth  ← cursor 1
MAP                   ← cursor 2
PRESETS               ← cursor 3
```

Encoder scrolls through all items. OK on a channel line enters MODE_EDIT for that channel. Selected channel `ch` flows through:

```cpp
SoundProcessorManager::GetCStrJSONActivePluginParams(ch)
SoundProcessorManager::SetChannelParamValue(ch, id, "current", val)
SoundProcessorManager::GetStringID(ch)
SoundProcessorManager::GetCStrJSONGetPresets(ch)
// etc.
```

**Long-press on channel line:** Currently unused, could enter a different action (e.g. reload).

### Detection in `init()`:

```cpp
void UIMenuPageParams::init() {
    chan = 0;
    string id0 = SoundProcessorManager::GetStringID(0);
    string id1 = SoundProcessorManager::GetStringID(1);
    hasDualCh = (id0 != id1 && !id0.empty() && !id1.empty());
    // if stereo plugin loaded on ch0, id1 is empty or same as id0
    parseParams(chan);
}
```

---

## Phase C: MODE_MAP — Per-Parameter CV/Trig Assignment

### Current State

Placeholder: `Display::DrawString(0, 16, "Not implemented")`

### Implementation

**Entry point:** Long-press on any param in MODE_EDIT → enter MODE_MAP for that param.

```
WTOsc Gain          ← param name (read-only)
────────────────
CV slot: None       ← current CV slot, encoder changes it
     ↑ scroll through available slot names
```

Encoder scrolls through all CV slot names (90 + "None"). Selected value updates immediately? Or OK to confirm? Better: encoder changes display, OK confirms and exits set.

**Option 1 (MODE_SELECT → MAP):**

```
MAP — Parameter CV Assignment

Encoder selects a param, shows its CV slot on the right.
OK on param → enters slot editor (same as above).
```

Scrolling 50+ params with 6-line OLED would show 6 at a time. Could also use group-navigation (reuse MODE_GROUP concept) to reduce the list.

**Trigger assignment:** For `bool` params, the same flow applies for `"trig"` slot assignment instead of `"cv"`. Triggers are indexed differently (N_TRIGS=40 on BBA).

### CV Slot Name Table

New shared header `main/CVSlotNames.hpp`:

```cpp
// CV slot 0-89 display names (7 chars max for OLED)
static const char* cvSlotNames[90] = {
    "A_Note", "A_Velo", "A_Bank", "A_SBnk", "A_Prog",
    "A_PB", "A_PB_Lg", "A_AT", "A_MW1", "A_BC2",
    // ... ~80 more ...
    "LFO1", "LFO2"
};
```

Derived from `IOCapabilities.hpp` BBA list + ModEngine slots 88-89. Trigger slot names from the same file (N_TRIGS=40 for BBA).

`SetChannelParamValue(chan, id, "cv", slot)` writes the new slot index. SetChannelParamValue(chan, id, "trig", slot)` for triggers.

---

## Phase D: New PANEL_MOD Tab

### Panel Registration

```cpp
// UIMenu.hpp
enum Panel : uint8_t {
    PANEL_MIX = 0,
    PANEL_TAPE = 1,
    PANEL_HOME = 2,
    PANEL_MOD = 3,        // ← new: modulation sources
    PANEL_PARAMS = 4,
#if CONFIG_BT_ENABLED
    PANEL_MIDI = 5,
#endif
};
```

PANEL_MOD at index 3, between HOME and PARAMS. Navigation flow: MIX → TAPE → HOME → MOD → PARAMS → (MIDI).

### Page Structure

New files: `main/menupages/UIMenuPageMod.hpp`, `main/menupages/UIMenuPageMod.cpp`

```cpp
enum SubPage { SP_MAIN, SP_LFO1, SP_LFO2, SP_CC_SLOTS };
```

**SP_MAIN (overview):**
```
Modulation Sources
─ LFO1: Sine  3.0Hz   ← cursor 0, OK enters LFO1 edit
  LFO2: Tri   0.5Hz   ← cursor 1, OK enters LFO2 edit
  CC Slots              ← cursor 2, OK enters CC slot list
  ──────────────
  Outputs: CV 88, 89   ← info line (read-only)
```

**SP_LFO1 / SP_LFO2 (per-LFO config):**
```
LFO1 Config
  Shape:  Sine          ← encoder scrolls sine/tri/saw/sq/S&H
  Rate:   3.0 Hz        ← encoder adjusts 0.01-100 Hz
  Amp:    0.50          ← encoder adjusts 0.0-1.0
  Output: CV 88         ← encoder selects output CV slot
```

All changes call `ModEngine::SetLFOShape(0, ...)` etc. in real-time. BACK steps up.

**SP_CC_SLOTS (dynamic CC slots):**
```
CC Slots
  0:  CC 1  (MW)       ← OK to edit CC number
  1:  CC 7  (Vol)       ← long-press to clear (set -1)
  2:  —                 ← empty slot
  3:  CC 11 (Expr)
  ...
  7:  — 
```

Select a slot → OK → encoder changes CC number (1-127) + MIDI channel (1-16). A "Learn" option: `ModEngine::StartLearn()` with timeout. While learning, show "Waiting for CC...". When captured → display updates with the learned CC number.

### Persistence (Deferrable)

LFO shape/rate/amp and CC slot assignments are currently RAM-only in ModEngine. To persist across reboots:

- Save/load from SD: `data/mod-config.json`
- JSON format:
  ```json
  {
    "lfo1": { "shape": 0, "rate": 3.0, "amp": 0.5, "cvSlot": 88 },
    "lfo2": { ... },
    "ccSlots": [
      { "cc": 1, "chan": 0, "cvSlot": 80 },
      ...
    ]
  }
  ```
- Save on param change, load in `ModEngine::Init()`

---

## Implementation Order

| # | Step | Scope | Depends On |
|---|------|-------|------------|
| 1 | Fix `parseParams()` group recursion + SPIRAM allocation + MODE_GROUP | `UIMenuPageParams.hpp/.cpp` | — |
| 2 | Increase `MAX_PARAMS` to 196+ → verify DrumRack/SpaceFX render | `UIMenuPageParams.hpp` | 1 |
| 3 | Build CVSlotNames.hpp (shared name table) | New file `main/CVSlotNames.hpp` | — |
| 4 | Implement MODE_MAP (per-param CV/trig assignment) | `UIMenuPageParams.cpp` | 3 |
| 5 | Dual channel select in MODE_SELECT | `UIMenuPageParams.cpp` | 1 |
| 6 | New PANEL_MOD tab (LFO + CC config) | New `UIMenuPageMod` files, update `UIMenu.hpp/.cpp`, `CMakeLists.txt` | 3 |
| 7 | Persist mod config to SD | `ModEngine` + new reader/writer | 6 |

---

## Decisions (Resolved)

1. **PANEL_MOD position** → Index 3, between HOME and PARAMS: `MIX → TAPE → HOME → MOD → PARAMS → (MIDI)`

2. **CV assignment UX** → Long-press on a param in MODE_EDIT enters MODE_MAP for that param

3. **MODE_GROUP display** → Show group name + param count: `Analogue BD (12)`

4. **CV slot naming for OLED** → 7-char max per slot name. Use abbreviations from `IOCapabilities.hpp`:
   - `A_Note` → `A.Note`
   - `G_MW_1` → `G.MW`
   - Slots 80-87 → `CC1`..`CC8`
   - Slots 88-89 → `LFO1`, `LFO2`
   - `G_SUST_64` → `Sust`

5. **Dual channel MAP/PRESETS** → MAP and PRESETS are per-channel. MODE_SELECT shows entries per channel when dual mono: "Ch0 MAP", "Ch1 MAP".

6. **Persistence** → Acceptable to lose LFO/CC config on reset. Deferred.

---

## File Manifest

### New Files
| File | Purpose |
|------|---------|
| `main/CVSlotNames.hpp` | CV slot name lookup table (shared by MODE_MAP + PANEL_MOD) |
| `main/menupages/UIMenuPageMod.hpp` | Modulation panel header |
| `main/menupages/UIMenuPageMod.cpp` | Modulation panel implementation |

### Modified Files
| File | Changes |
|------|---------|
| `main/menupages/UIMenuPageParams.hpp` | Add MODE_GROUP, GroupInfo struct, SPIRAM alloc, MAX_PARAMS→196+ |
| `main/menupages/UIMenuPageParams.cpp` | Group-recursive parseParams(), MODE_MAP impl, dual channel |
| `main/UIMenu.hpp` | Add PANEL_MOD to enum, update PANEL_COUNT |
| `main/UIMenu.cpp` | Create UIMenuPageMod instance, update panel array |
| `main/CMakeLists.txt` | Add new source files |
