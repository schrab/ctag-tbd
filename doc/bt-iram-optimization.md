# Bluetooth IRAM Optimization on ESP32

## Problem
ESP32 has 192KB IRAM (iram0_0_seg). Bluedroid Classic BT SPP needs ~17KB more IRAM than available when all other firmware is loaded.

## Root Cause
The BT controller's **SCO/eSCO synchronous connection paths** reserve ~20-30KB IRAM for voice/audio profiles. MIDI SPP doesn't need these at all.

## Solution Summary
Disable SCO + minimize ACL connections + zero DRAM reserve:

```ini
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=y
CONFIG_BT_SPP_ENABLED=y
CONFIG_BT_SSP_ENABLED=y
CONFIG_BT_A2DP_ENABLED=n
CONFIG_BT_HFP_ENABLED=n
CONFIG_BT_BLE_ENABLED=n

# Controller: Classic only, 1 ACL, 0 SCO, zero extra DRAM
CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y
CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN=1
CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN=0
CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH=0
CONFIG_BT_RESERVE_DRAM=0
CONFIG_BT_ALARM_MAX_NUM=20

# Memory: push to SPIRAM, tiny stack
CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST=y
CONFIG_BT_BTC_TASK_STACK_SIZE=2048
```

## IRAM Consumers (ranked)
| Component | IRAM Cost | Mitigation |
|-----------|-----------|------------|
| SCO/eSCO paths | ~20-30KB | `MAX_SYNC_CONN=0`, `SCO_DATA_PATH=0` |
| BLE link layer | ~20-25KB | `esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` |
| BT ACL paths | ~12-15KB | `MAX_ACL_CONN=1` |
| A2DP/AVRCP | ~10-15KB | Disabled |
| BT Controller DRAM buffer | ~50-70KB DRAM | `BT_RESERVE_DRAM=0` |

## Key Insight
The ESP32-S3 (BBA platform) has 384KB DRAM — IRAM is still the bottleneck, but the S3's extra DRAM gives more headroom. PSRAM helps with DRAM but does NOT help with IRAM — BT controller code always lives in internal SRAM.
