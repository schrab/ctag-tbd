# Parameter System

## Key Files
| File | Role |
|------|------|
| `main/SPManager.hpp` / `SPManager.cpp` | Orchestrates 2 channels, plugin switching, param setting |
| `components/ctagSoundProcessor/ctagSoundProcessor.hpp` | Base class: `SetParamValue()`, `pMapPar/Cv/Trig` maps |
| `components/ctagSoundProcessor/ctagSPDataModel.hpp` | Loads/saves UI model + preset JSON |
| `spiffs_image/data/sp/mui-<id>.jsn` | UI model (param schema per plugin) |
| `spiffs_image/data/sp/mp-<id>.jsn` | Preset data (saved values + CV/trig assignments) |
| `main/RestServer.cpp` | REST API for param control |
| `main/SerialAPI.cpp` | Serial API for param control |
| `main/menupages/UIMenuPageParams.hpp` / `.cpp` | Group-recursive param editor, CV map, dual-channel |
| `main/menupages/UIMenuPageMod.hpp` / `.cpp` | LFO/CC modulation source config page |
| `main/CVSlotNames.hpp` | 100-entry CV slot display name table (7-char max) |
| `main/ModEngine.hpp` / `.cpp` | LFO engine, SaveConfig/LoadConfig SPIFFS persistence |
| `main/IOCapabilities.hpp` | CV array extended to 100 entries (slots 90-99 = ModEngine) |

## Parameter Architecture
Each plugin has three mapping tables defined in its `knowYourself()` method:
```cpp
pMapPar.emplace("gain", [&](const int val){ gain = val; });   // Panel value
pMapCv.emplace("gain",  [&](const int val){ cv_gain = val; });  // CV source index
pMapTrig.emplace("gain", [&](const int val){ trig_gain = val; }); // Trig source index
```

### CV Slot Range (BBA)
N_CVS=100: slots 0-89 are MIDI-mapped (G_*, A_*, B_*, C_*, D_*), slots 90-97 are ModEngine dynamic CC outputs (CC1..CC8), slots 98-99 are LFO1/LFO2 outputs.

### ParamInfo / GroupInfo (SPIRAM)
`ParamInfo[256]` and `GroupInfo[32]` are allocated from SPIRAM via `heap_caps_malloc(MALLOC_CAP_SPIRAM)` in `UIMenuPageParams::init()`, keeping DRAM usage low (~21 KB freed vs static allocation).

## Data Flow: Set Parameter Value
```
API call → SoundProcessorManager::SetChannelParamValue(chan, id, key, val)
    → sp[chan]->SetParamValue(id, key, val)
        → setParamValueInternal(id, key, val)
            → dispatches via key:
                "current" → pMapPar[id](val)  + persist to model
                "cv"      → pMapCv[id](val)   + persist to model
                "trig"    → pMapTrig[id](val)  + persist to model
```
Stereo guard: `SetChannelParamValue` returns early if `chan==1` and ch0's plugin is stereo.

## Stereo Detection
```cpp
SoundProcessorManager::IsPluginStereo(id)  // public API, delegates to SPManagerDataModel::IsStereo()
```
Used by `UIMenuPageParams::init()` to set `hasDualCh = false` when ch0 is stereo — prevents showing "Ch1: Dust" in the parameter editor.

## Parameter UI Model (mui-*.jsn)
```json
{
  "id": "GVerb",
  "isStereo": true,
  "name": "G-Verb",
  "params": [
    {"id": "roomsize", "name": "Room Size", "type": "int", "min": 0, "max": 4095, "step": 1},
    {"id": "mono", "name": "Mono In", "type": "bool"}
  ]
}
```
Types: `int` (min/max/step), `bool` (on/off), `group` (nested params).

## Preset Data Model (mp-*.jsn)
```json
{
  "patches": [{
    "name": "Default",
    "params": [
      {"id": "trigger", "current": 0, "trig": -1},
      {"id": "position", "current": 2047, "cv": -1}
    ]
  }]
}
```
Each param stores: `current` (value), `cv` (CV source index, -1=none), `trig` (trig source index).

## Modulation Hook Points
During `Process()`:
```cpp
float fVal = param / 4095.f * scale;
if (cv_param != -1) fVal = data.cv[cv_param];  // CV overrides panel
```

## Group-Recursive Parsing (MODE_GROUP)

`parseParams()` recurses into `"type": "group"` entries in the mui JSON, building a `GroupInfo[]` array and flattening leaf params into `ParamInfo[]`. Groups are displayed as selectable items in MODE_GROUP (e.g. `Analogue BD (12)`). Entering a group drops into MODE_EDIT showing only that group's leaf params.

## CV Slot Mapping UI (MODE_MAP)

From a param in MODE_EDIT, a long-press enters MODE_MAP — a CV slot editor. Encoder scrolls -1 (None) through 99 (CV slot index). OK confirms via `SetChannelParamValue(chan, id, "cv", slot)`. BACK exits without saving. Slot names come from `CVSlotNames.hpp` (100 entries, 7-char max).

## Dual-Channel Editing

When two different mono plugins are loaded (detected via `GetStringID(0) != GetStringID(1)`), MODE_SELECT shows `Ch0:name` / `Ch1:name` cursors. A `chan` member in `UIMenuPageParams` drives all `SetChannelParamValue(chan, ...)` calls. MAP and PRESETS remain single-entry.

## Modulation Tab (PANEL_MOD)

Tab between HOME and PARAMS. Sub-pages:
- `SP_MAIN` — overview of LFO1, LFO2, and 8 CC slots
- `SP_LFO1` / `SP_LFO2` — shape (sine/saw/tri/square/random), rate (Hz), amp, output CV slot assignment
- `SP_CC_SLOTS` — scrolling list of 8 CC slots
- `SP_CC_EDIT` — CC number, channel, MIDI Learn

## Persistence

LFO/CC config saved to SPIFFS on every encoder change in PANEL_MOD. File: `/spiffs/data/mod-config.jsn`. Format:
```json
{"lfo1":{"shape":0,"rate":3.0,"amp":0.5,"cvSlot":98},"lfo2":{...},
 "ccSlots":[{"cc":-1,"chan":0,"cvSlot":90},...]}
```
Save uses RapidJSON `FileWriteStream` with a 512B stack buffer (no heap). `ModEngine::LoadConfig()` called from `Init()`.

## API Endpoints
- `GET /api/v1/setPluginParam/CV?ch=0&id=cutoff&cv=12` — map CV to param
- `GET /api/v1/setPluginParam/TRIG?ch=0&id=trigger&trig=0` — map trig to param
- `GET /api/v1/setPluginParam/current?ch=0&id=cutoff&val=2048` — set param value
- `GET /api/v1/getPluginParams/0` — get all params JSON for channel 0
