# Memory

## Project Overview
See @README.md for project overview and @package.json for available npm/pnpm commands for this project.

## Code Style Guidelines
- Use descriptive variable names
- Follow existing patterns in the codebase
- Extract complex conditions into meaningful boolean variables

## Known Issues
- **GPIO0 = I2S MCLK, MUST NOT be used as GPIO input**: On BBA, GPIO0 is configured as I2S MCLK output by the I2S peripheral. Calling `gpio_config()` with `GPIO_MODE_INPUT` or `gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT)` disconnects the I2S peripheral output, killing MCLK and stopping all audio. `UserInput::Init()` and `StartSoundProcessor()` must NOT touch GPIO0. BTN2 (OK) is unavailable. Button mapping (BTN1 only, PANEL_IN state): short press = OK (BTN2_SHORT), long press = MOD mapping (BTN2_LONG), double-click = BACK. Double-click detection is in UserInput.cpp (300ms window). Single tap has 300ms delay to allow for second tap — handled by pendingShort timer in inputTask().
- **Interrupt Watchdog**: ESP32-D0WD-V3 rev3.1 + PSRAM + dual-core requires `CONFIG_ESP_INT_WDT=y`. BT coex calibration during codec I2S init can exceed the default 300ms timeout — set `CONFIG_ESP_INT_WDT_TIMEOUT_MS=5000` in `sdkconfig.defaults.a1s`.
- SD card FAT32: use `esp_vfs_fat_sdmmc_mount()` with `gpio_cd = GPIO_NUM_NC` to avoid card detect hang.
- Sound processor allocator: 112KB internal buffer may not fit in DRAM when BT is active — falls back to PSRAM via `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`.
- **DRAM exhaustion → silent UI freeze**: After audio init, only ~22KB DRAM remains (largest free block ~17KB). `UserInput::Init()` must be called from `UIMenu::TaskFunction` (post `xTaskCreatePinnedToCore`), not from `UIMenu::Init()` — the input task's 2KB stack fragments the heap so the UIMenu task's stack can't allocate contiguously. UIMenu stack is 4096 (not 8192) since it only polls a queue + draws display. See `doc/ui-menu-architecture.md` for full root cause.
- **Elements output level overdrives codec**: The Elements DSP engine (Mutable Instruments port) outputs float values well beyond TBD's expected ±1.0 range. Elements' `SoftLimit(x) = x*(27+x²)/(27+9x²)` is NOT a hard limiter — it becomes linear (gain~1/9) for large x, allowing outputs of ±11+ from the modal resonator's 52 SVF modes summing coherently. TBD's `Codec::WriteBuffer` multiplies by 32767 and clamps to int16 range, producing hard digital clipping. The dense multi-harmonic signal creates intermodulation artifacts that sound like "8-bit reduction" + distortion. Model 1 (single K-S string) stays clean because the feedback loop gain is < 1.0, self-limiting amplitude. Fixed by applying 0.125 master gain in `ctagSoundProcessorElements.cpp:179-182` (`out[i] * 0.125f`). Root cause files: `part.cc:208-211` (SoftLimit), `codec.cpp:312-325` (float→int clamping).

## Architecture Notes
- UI architecture: See doc/ui-menu-architecture.md for full details. Norns-style navigation with ROOT/PANEL_IN states. Page-owned sub-page depth via onBack(). Norns-style indicator bar (no text labels) with fade timer.
- Display framebuffer: main/Display.cpp has a 1024-byte framebuffer (`fb[1024]`) and per-page dirty tracking (`dirtyPages[8]`). Physical display is SSD1309 (driver compatible with SSD1306/SSD1309, 0xC8 COM scan). DrawPixel handles page/bit reversal. Only FONT_5X7 used in UI. DrawString/DrawVUMeter/DrawScrollbar do NOT call Flush() internally — redraw methods batch then flush once. Clear() does memset+finger only (no direct I2C). ShowFWVersion() uses framebuffer + FONT_5X7.
- Encoder driver: components/drivers/encoder.cpp uses PCNT unit 0 on GPIO23/GPIO5. Polled at 1kHz, sensitivity threshold 2, ReadDelta() returns accumulated count per poll.
- User input: main/UserInput.cpp runs Core 0 task at idle+3. GPIO36 (BTN1) with 20ms debounce, 500ms long-press threshold. FreeRTOS queue (64 slots) dispatched via GetEvent(). EnableISR() must be called AFTER audio init. **GPIO0 is NOT used for BTN2** — it's the I2S MCLK output. Double-click detection built into inputTask(): if two short releases within 300ms, emits BTN1_DOUBLE instead of two BTN1_SHORT. Single tap has 300ms delay before BTN1_SHORT fires (to allow for second tap). UIMenu dispatch: short = OK (BTN2_SHORT), long = MOD (BTN2_LONG), double = BACK.
- Parameter editing: main/menupages/UIMenuPageParams.cpp parses plugin parameter JSON via RapidJSON, renders 6-item scrollable list on OLED, dispatches value changes through SoundProcessorManager::SetChannelParamValue(). Encoder adjusts values in MODE_VALUEEDIT, OK confirms, BACK exits.
- Plugin browser: main/menupages/UIMenuPageHome.cpp parses available processors JSON, renders 6-item scrollable plugin list, S/M type indicators. Stereo loads to ch0 directly. Mono shows Ch0/Ch1/Both submenu. BACK navigates through sub-pages. OK (BTN2_SHORT via BTN1 short press) is the primary select action.
- SD card: components/drivers/fs.cpp InitSD() initializes SDMMC slot 1 (4-bit, GPIO34 CD), mounts as LittleFS on /sd in v5.x (was FAT on older IDF). CONFIG_LITTLEFS_SDMMC_SUPPORT must be enabled for SD detection.
- UIMenu task: 4096 byte stack, runs on Core 0 at idle+3, 20ms period. Created after StartSoundProcessor() completes. UserInput::Init() called from TaskFunction (not from Init()) to avoid DRAM fragmentation before task creation. Old Favorites::ui_task is DISABLED (GPIO5 conflict with encoder).
- Panel bar: Norns-style dashed horizontal line at y=0 with active/inactive segment indicators. No text. Fades after ~1s. 128/PANEL_COUNT px per segment.

## Common Workflows
- **Build**: `source /home/ubuntu/esp-idf/export.sh && idf.py build`
- **Full flash (all partitions + storage)**: `source /home/ubuntu/esp-idf/export.sh && python -m esptool --chip esp32 -b 460800 --port /dev/ttyUSB0 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xd000 build/ota_data_initial.bin 0x10000 build/ctag-tbd.bin 0x610000 build/storage.bin`
- **Flash main app only** (no storage update): `source /home/ubuntu/esp-idf/export.sh && python -m esptool --chip esp32 -b 460800 --port /dev/ttyUSB0 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x10000 build/ctag-tbd.bin`
- **UART log capture**: `source /home/ubuntu/esp-idf/export.sh 2>/dev/null && stty -F /dev/ttyUSB0 115200 raw -echo && timeout 15 cat /dev/ttyUSB0 2>/dev/null | tee /tmp/uart.log`
