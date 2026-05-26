# MIDI System

## Overview
MIDI is received via UART1 (GPIO18 TX, GPIO19 RX) at 31250 baud. USB MIDI (TinyUSB) driver exists but is currently commented out in `Midi::Update()`.

## Key Files
| File | Role |
|------|------|
| `components/drivers/midiuart.cpp` | UART MIDI driver (ESP32 HW UART1) |
| `components/drivers/tusbmidi.cpp` | USB MIDI driver (TinyUSB, currently disabled) |
| `main/Midi.hpp` / `Midi.cpp` | Core MIDI parser/dispatcher + voice mode manager + CV/trigger mapping |
| `main/Control.hpp` / `Control.cpp` | Platform-agnostic control layer; bridges MIDI to sound processors |

## Data Flow
```
UART1 (GPIO19) → Midi::Update() → cv/trig buffers → Control::Update()
    → ProcessData.cv[] / .trig[] → sp[0/1]->Process(pd)
```

## CV/Trigger Buffer
- BBA (Black Box Audio): N_CVS=90, N_TRIGS=40 (defined in root CMakeLists.txt)
- MIDI events are mapped to fixed CV buffer indices via compile-time constexpr tables

## MIDI Channel Architecture
| Channel | Mode |
|---------|------|
| 1 | Global / Monophonic |
| 2-5 | Individual Voices A-D |
| 6-9 | Ignored (configurable via CC 111) |
| 10-13 | Percussion / Trigger |
| 14 | Duophonic A/B |
| 15 | Duophonic C/D |
| 16 | 4-Voice Polyphonic |

## CC-to-CV Routing
- Voice channels (2-5): CC 0,32,1,2,71-78 map to fixed CV slots
- Global channel (1): CC 1,2,4,6,7,8,10,11,12,13,64-67,69 map to global CV slots
- Each CV slot index corresponds to a `Control_element_cv_id` enum value

## MIDI Message Handling
- `0x80` Note Off → `handleNoteOff()`
- `0x90` Note On → `handleNoteOn()` (vel=0 treated as note off)
- `0xA0` Poly Key Pressure → ignored
- `0xB0` Control Change → `controlChange()` (routes via ccToCVid_glob/abcd)
- `0xC0` Program Change → `programChange()`
- `0xD0` Channel Pressure → `channelPressure()`
- `0xE0` Pitch Bend → `pitchBend()`
- `0xF8-0xFF` Real-time → do not reset running status

## Special CCs
- CC 111: toggle ignore_channels_6to9 (value >= 64 enables)
- CC 120: All Sounds Off
- CC 123: All Notes Off
