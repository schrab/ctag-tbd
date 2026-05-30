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
- BBA (Black Box Audio): N_CVS=100, N_TRIGS=40 (defined in root CMakeLists.txt)
- MIDI events are mapped to fixed CV buffer indices via compile-time constexpr tables
- Slots 0-89: MIDI-mapped (G_*, A_*, B_*, C_*, D_*)
- Slots 90-97: ModEngine dynamic CC outputs (CC1..CC8)
- Slots 98-99: LFO1/LFO2 outputs

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

## BLE MIDI (via NimBLE Central)

BLE MIDI is received via NimBLE GATT **central** mode (TBD connects to a BLE MIDI peripheral like the M-VAVE SMC-PAD). The implementation is in `BtMidiReceiver` (central) and exposed through `UIMenuPageMidi` (SCAN/CONNECT/DISCONNECT UI).

### Data Flow
```
SMC-PAD (BLE peripheral) → advertising → BtMidiReceiver::StartScan()
  → BLE_GAP_EVENT_DISC: collect device name + address
  → UI picks device → BtMidiReceiver::Connect(idx)
    → ble_gap_connect() → service discovery
    → find MIDI service → subscribe to notifications
    → BLE_GAP_EVENT_NOTIFY_RX → parse Apple BLE-MIDI format
      → skip 2-byte timestamp headers → raw MIDI bytes
      → ring buffer (2KB, static DRAM)

Midi::Update()
  → BtMidiReceiver::Read(buf, &len)
  → same processing pipeline as UART MIDI
  → CV/trig buffers → Control::Update() → sp[0/1]->Process()
```

### Key Files
| File | Role |
|------|------|
| `main/BtMidiReceiver.hpp` / `.cpp` | NimBLE central implementation: scan, connect, subscribe, ring buffer |
| `main/menupages/UIMenuPageMidi.hpp` / `.cpp` | UI: device list, connect, disconnect, status, UART/BLE source toggling |

### GATT Service
- **MIDI Service UUID:** `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`
- **MIDI Characteristic UUID:** `03B80E5A-EDE8-4B33-A751-6CE34EC4C702`
- **TBD role:** GATT client (central) — subscribes to characteristic notifications
- **Peripheral role:** SMC-PAD (or any BLE MIDI device) — sends MIDI data via notification

### Init Ordering
`BtMidiReceiver::Init()` is called from `SoundProcessorManager::StartSoundProcessor()` **after** `Codec::InitCodec()` and **before** `UIMenu::Init()`. On ESP32 Rev 3, BT coex calibration can lock I2C/I2S critical sections — initializing the codec first avoids this erratum.

### Configuration (sdkconfig.defaults.a1s)
- BLE-only mode (`BTDM_CTRL_MODE_BLE_ONLY`)
- 1 connection max (controller + host)
- NimBLE heap → PSRAM (`MEM_ALLOC_MODE_EXTERNAL`)
- Central role only (no peripheral/broadcaster/observer)
- Controller + host pinned to Core 0 (audio on Core 1)

## MIDI Source Switching

UART and BLE MIDI inputs can be independently toggled from the MIDI page UI (cursor 3 = UART ON/OFF, cursor 4 = BLE ON/OFF). `Midi::SetUartEnabled(bool)` and `Midi::SetBleEnabled(bool)` set flags checked in `Midi::Update()` — if disabled, the respective source is skipped during read.

## Special CCs
- CC 111: toggle ignore_channels_6to9 (value >= 64 enables)
- CC 120: All Sounds Off
- CC 123: All Notes Off
