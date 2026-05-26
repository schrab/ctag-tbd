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

## Parameter Architecture
Each plugin has three mapping tables defined in its `knowYourself()` method:
```cpp
pMapPar.emplace("gain", [&](const int val){ gain = val; });   // Panel value
pMapCv.emplace("gain",  [&](const int val){ cv_gain = val; });  // CV source index
pMapTrig.emplace("gain", [&](const int val){ trig_gain = val; }); // Trig source index
```

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

## API Endpoints
- `GET /api/v1/setPluginParam/CV?ch=0&id=cutoff&cv=12` — map CV to param
- `GET /api/v1/setPluginParam/TRIG?ch=0&id=trigger&trig=0` — map trig to param
- `GET /api/v1/setPluginParam/current?ch=0&id=cutoff&val=2048` — set param value
- `GET /api/v1/getPluginParams/0` — get all params JSON for channel 0
