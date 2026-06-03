# SRAM Optimization Research

## Root Cause
The 112 KB Arena (`CONFIG_SP_FIXED_MEM_ALLOC_SZ=114688`) requests contiguous internal DRAM at boot via `heap_caps_malloc` with `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`. After bootloader, FreeRTOS, and early init, the ESP32-D0WD-V3 has ~320 KB total DRAM, but the largest contiguous free block is only ~17 KB. This forces the allocator to fall back to `MALLOC_CAP_SPIRAM`.

## Disabling WiFi
Disabling WiFi completely (`CONFIG_ESP_WIFI_ENABLED=n`) will absolutely free up the required memory. WiFi consumes ~150–200 KB of internal DRAM for static/dynamic RX/TX buffers, management buffers, and task stacks. Disabling it provides enough contiguous space for the 112 KB Arena allocation.

## Task Stack Optimizations
Minor gains (~2–3 KB total) can be achieved by reducing task stack sizes, which helps slightly with heap fragmentation:
- `input_task`: 2048 bytes → **1536 bytes** (only polls ADC/encoder, very lightweight).
- `ui_menu`: 4096 bytes → **3072 bytes** (already optimized from 8192, can be tested lower).
- `audio_task`: 4096 bytes → **3072 bytes** (local vars use ~500 bytes, plenty of headroom).

## Configuration Optimizations (If WiFi is Required)
If network functionality must be retained, we can reduce WiFi buffer counts in `sdkconfig.defaults.a1s` to free ~100 KB of internal DRAM. This may be enough to allow the 112 KB allocation:
- `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=10` (from 32)
- `CONFIG_ESP_WIFI_CACHE_TX_BUFFER_NUM=10` (from 32)
- `CONFIG_ESP_WIFI_MGMT_SBUF_NUM=10` (from 32)

*Note: The project already has `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` and `CONFIG_WIFI_LWIP_ALLOCATION_FROM_SPIRAM_FIRST=y`, but WiFi MAC/PHY still requires critical internal DRAM.*

## Code Optimizations
- No large static arrays (>1 KB) were found in `main/` or `components/` that could be trivially moved to PSRAM.
- The `audio_task` stack is already lean. Ensure no new large local variables are added to Core 1 tasks.
- `ParamInfo[256]` and `GroupInfo[32]` are already correctly allocated in SPIRAM.
