# BLE MIDI Central Mode Implementation

## Goal
Replace the existing NimBLE GATT **peripheral** (advertising) implementation with a GATT **central** (client) that can scan for, connect to, and receive MIDI data from a BLE MIDI controller (M-VAVE SMC-PAD).

## Why Central, Not Peripheral
The SMC-PAD is a BLE **peripheral** (advertises a MIDI GATT service). TBD must act as the **central** (GATT client) to discover and connect to it.

## Files Changed

| File | Change |
|------|--------|
| `main/BtMidiReceiver.hpp` | Remove peripheral declarations, add device list state |
| `main/BtMidiReceiver.cpp` | Complete rewrite: GATT server → GAP client |
| `sdkconfig.defaults.a1s` | Core 0 pinning, EXTERNAL alloc, central-only roles, max conn=1 |
| `doc/midi-system.md` | Add BLE MIDI data flow section |

## NimBLE Central Architecture

```
app_main → SPManager::StartSoundProcessor()
  → BtMidiReceiver::Init()
    → nvs_flash_init()
    → nimble_port_init()
    → ble_hs_cfg.sync_cb = on_sync (NimBLE ready)
    → nimble_port_freertos_init(host_task)

on_sync()
  → NimBLE host synced with controller, ready for operations

UI: user presses SCAN → StartScan()
  → ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 0, gap_event_cb, NULL)
  → BLE_GAP_EVENT_DISC: collect advertising reports
    → filter: any device (SMC-PAD or any BLE MIDI)
    → store name + address in device list

UI: user selects device → Connect(idx)
  → ble_gap_connect(0, &peer_addr, NULL, gap_event_cb, NULL)
  → BLE_GAP_EVENT_CONNECT: success
    → ble_gattc_disc_all_svcs(conn_handle, svc_disc_cb, NULL)

svc_disc_cb()
  → BLE_GAP_EVENT_DISC_SVC: find MIDI service UUID
    → ble_gattc_disc_all_chrs(conn_handle, start, end, chr_disc_cb, NULL)

chr_disc_cb()
  → Find MIDI characteristic with NOTIFY property
  → Write 0x0001 to CCCD (subscribe to notifications)

BLE_GAP_EVENT_NOTIFY_RX
  → Parse Apple BLE-MIDI format (skip 2-byte timestamp headers)
  → Push raw MIDI bytes to ring buffer

Midi::Update()
  → BtMidiReceiver::Read(buf, &len)
  → Same pipeline as UART MIDI
```

## Config Changes (`sdkconfig.defaults.a1s`)

### Core Pinning
```
CONFIG_BTDM_CTRL_PINNED_TO_CORE_0=y
CONFIG_BT_NIMBLE_PINNED_TO_CORE_0=y
CONFIG_NIMBLE_PINNED_TO_CORE_0=y
```
Moved from Core 1 — the original I2C ISR conflict (legacy `driver/i2c.h`) no longer applies. The display was migrated to the modern `i2c_master` driver. Core 0 keeps BT away from audio processing on Core 1.

### Memory Optimization
```
CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y    # NimBLE heap → PSRAM
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1            # single device
```

### Role Restrictions (central-only)
```
# CONFIG_BT_NIMBLE_ROLE_PERIPHERAL is not set
# CONFIG_BT_NIMBLE_ROLE_BROADCASTER is not set
# CONFIG_BT_NIMBLE_ROLE_OBSERVER is not set
CONFIG_BT_NIMBLE_ROLE_CENTRAL=y
```

## Memory Impact

| Resource | Amount | Notes |
|----------|--------|-------|
| NimBLE host stack | 4096 bytes (DRAM) | Default, sufficient |
| MIDI ring buffer | 2048 bytes (DRAM) | Static, same as before |
| NimBLE heap allocs | → PSRAM | `MEM_ALLOC_MODE_EXTERNAL` |
| BLE controller firmware | ~20-25KB IRAM | Build-time check |
| Device list | 16 × 38 = 608 bytes (DRAM) | Fixed array |

## Apple BLE-MIDI Format

Notifications from the SMC-PAD use the Apple BLE-MIDI format:
```
[ts_high | 0x80] [ts_low] [MIDI messages...]
[ts_high | 0x80] [ts_low] [MIDI messages...]
```
Each MIDI message group is prefixed by a 2-byte timestamp header (the first byte has bit 7 set). The receiver skips these headers and passes raw MIDI bytes to the standard MIDI parser.
