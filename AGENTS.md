# Memory

## Project Overview
See @README.md for project overview and @package.json for available npm/pnpm commands for this project.

## Code Style Guidelines
- Use descriptive variable names
- Follow existing patterns in the codebase
- Extract complex conditions into meaningful boolean variables

## Known Issues
- **I2C legacy driver** (`driver/i2c.h`) hangs after BT coex calibration on ESP32 Rev 3. The ISR conflicts with BT controller interrupts causing I2C FSM lockup (`i2c_hw_fsm_reset` loops in `i2c_master_cmd_begin`). Must migrate `components/drivers/ssd1306_i2c.c` from legacy `driver/i2c.h` to modern `driver/i2c_master.h` API. This is a blocking issue for BT coexistence.
- **Interrupt Watchdog**: ESP32-D0WD-V3 rev3.1 + PSRAM + dual-core requires `CONFIG_ESP_INT_WDT=y`. BT coex calibration during codec I2S init can exceed the default 300ms timeout — set `CONFIG_ESP_INT_WDT_TIMEOUT_MS=5000` in `sdkconfig.defaults.a1s`.
- SD card FAT32: use `esp_vfs_fat_sdmmc_mount()` with `gpio_cd = GPIO_NUM_NC` to avoid card detect hang.
- Sound processor allocator: 112KB internal buffer may not fit in DRAM when BT is active — falls back to PSRAM via `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`.
- **DRAM exhaustion → silent UI freeze**: After audio init, only ~22KB DRAM remains (largest free block ~17KB). `UserInput::Init()` must be called from `UIMenu::TaskFunction` (post `xTaskCreatePinnedToCore`), not from `UIMenu::Init()` — the input task's 2KB stack fragments the heap so the UIMenu task's stack can't allocate contiguously. UIMenu stack is 4096 (not 8192) since it only polls a queue + draws display. See `doc/ui-menu-architecture.md` for full root cause.

## Architecture Notes
- UI architecture: See doc/ui-menu-architecture.md for full details. Norns-style navigation with ROOT/PANEL_IN states. Page-owned sub-page depth via onBack(). Norns-style indicator bar (no text labels) with fade timer.
- Display framebuffer: main/Display.cpp has a 1024-byte framebuffer (`fb[1024]`) and per-page dirty tracking (`dirtyPages[8]`). SSD1306 with 0xC8 COM scan requires page/bit reversal in DrawPixel. Only FONT_5X7 used in UI. DrawString/DrawVUMeter/DrawScrollbar do NOT call Flush() internally — redraw methods batch then flush once. Clear() does memset+finger only (no direct I2C). ShowFWVersion() uses framebuffer + FONT_5X7.
- Encoder driver: components/drivers/encoder.cpp uses PCNT unit 0 on GPIO23/GPIO5. Polled at 1kHz, sensitivity threshold 2, ReadDelta() returns accumulated count per poll.
- User input: main/UserInput.cpp runs Core 0 task at idle+3. GPIO36 (BTN1 = BACK) and GPIO0 (BTN2 = OK) with 20ms debounce, 500ms long-press threshold. FreeRTOS queue (64 slots) dispatched via GetEvent(). EnableISR() must be called AFTER audio init.
- Parameter editing: main/menupages/UIMenuPageParams.cpp parses plugin parameter JSON via RapidJSON, renders 6-item scrollable list on OLED, dispatches value changes through SoundProcessorManager::SetChannelParamValue(). Encoder adjusts values in MODE_VALUEEDIT, OK confirms, BACK exits.
- Plugin browser: main/menupages/UIMenuPageHome.cpp parses available processors JSON, renders 6-item scrollable plugin list, S/M type indicators. Stereo loads to ch0 directly. Mono shows Ch0/Ch1/Both submenu. BACK navigates through sub-pages. OK (BTN2_SHORT) is the primary select action.
- SD card: components/drivers/fs.cpp InitSD() initializes SDMMC slot 1 (4-bit, GPIO34 CD), mounts as LittleFS on /sd in v5.x (was FAT on older IDF). CONFIG_LITTLEFS_SDMMC_SUPPORT must be enabled for SD detection.
- UIMenu task: 4096 byte stack, runs on Core 0 at idle+3, 20ms period. Created after StartSoundProcessor() completes. UserInput::Init() called from TaskFunction (not from Init()) to avoid DRAM fragmentation before task creation. Old Favorites::ui_task is DISABLED (GPIO5 conflict with encoder).
- Panel bar: Norns-style dashed horizontal line at y=0 with active/inactive segment indicators. No text. Fades after ~1s. 128/PANEL_COUNT px per segment.

## Common Workflows
Document frequently used workflows and commands here.
- **I2C legacy driver** (`driver/i2c.h`) hangs after BT coex calibration on ESP32 Rev 3. The ISR conflicts with BT controller interrupts causing I2C FSM lockup (`i2c_hw_fsm_reset` loops in `i2c_master_cmd_begin`). Must migrate `components/drivers/ssd1306_i2c.c` from legacy `driver/i2c.h` to modern `driver/i2c_master.h` API. This is a blocking issue for BT coexistence.
- **Interrupt Watchdog**: ESP32-D0WD-V3 rev3.1 + PSRAM + dual-core requires `CONFIG_ESP_INT_WDT=y`. BT coex calibration during codec I2S init can exceed the default 300ms timeout — set `CONFIG_ESP_INT_WDT_TIMEOUT_MS=5000` in `sdkconfig.defaults.a1s`.
- SD card FAT32: use `esp_vfs_fat_sdmmc_mount()` with `gpio_cd = GPIO_NUM_NC` to avoid card detect hang.
- Sound processor allocator: 112KB internal buffer may not fit in DRAM when BT is active — falls back to PSRAM via `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`.
- Display framebuffer: main/Display.cpp has a 1024-byte SPIRAM framebuffer (`fb[1024]`) and per-page dirty tracking (`dirtyPages[8]`). Drawing primitives write to fb, Flush() sends dirty pages to SSD1306 over I2C. Two fonts: 8x8 (font8x8_basic.h, 16x8 chars) and 5x7 (font5x7.h, ~25x9 chars). All existing text-only functions (ShowFWVersion, etc.) are unchanged.
- Encoder driver: components/drivers/encoder.cpp uses PCNT unit 0 on GPIO23/GPIO5. Polled at 1kHz, sensitivity threshold 2, ReadDelta() returns accumulated count per poll.
- User input: main/UserInput.cpp runs Core 0 task at idle+3. GPIO36 (BTN1) and GPIO0 (BTN2) with 20ms debounce, 500ms long-press threshold. FreeRTOS queue (64 slots) dispatched via GetEvent().
- Parameter editing: main/menupages/UIMenuPageParams.cpp parses plugin parameter JSON via RapidJSON, renders 6-item scrollable list on OLED, dispatches value changes through SoundProcessorManager::SetChannelParamValue().
- Plugin browser: main/menupages/UIMenuPageHome.cpp parses available processors JSON, renders 6-item scrollable plugin list, stereo flag shown. BTN2 short backs to main menu, BTN2 long loads plugin on channel 0. SDMMC slot 1, 4-bit, GPIO34 card detect.
- SD card: components/drivers/fs.cpp InitSD() initializes SDMMC slot 1 (4-bit, GPIO34 CD), mounts as LittleFS on /sd. CONFIG_LITTLEFS_SDMMC_SUPPORT must be enabled.

## Common Workflows
Document frequently used workflows and commands here.
