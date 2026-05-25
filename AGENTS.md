# Memory

## Project Overview
See @README.md for project overview and @package.json for available npm/pnpm commands for this project.

## Code Style Guidelines
- Use descriptive variable names
- Follow existing patterns in the codebase
- Extract complex conditions into meaningful boolean variables

## Architecture Notes
- Display framebuffer: main/Display.cpp has a 1024-byte SPIRAM framebuffer (`fb[1024]`) and per-page dirty tracking (`dirtyPages[8]`). Drawing primitives write to fb, Flush() sends dirty pages to SSD1306 over I2C. Two fonts: 8x8 (font8x8_basic.h, 16x8 chars) and 5x7 (font5x7.h, ~25x9 chars). All existing text-only functions (ShowFWVersion, etc.) are unchanged.
- Encoder driver: components/drivers/encoder.cpp uses PCNT unit 0 on GPIO23/GPIO5. Polled at 1kHz, sensitivity threshold 2, ReadDelta() returns accumulated count per poll.
- User input: main/UserInput.cpp runs Core 0 task at idle+3. GPIO36 (BTN1) and GPIO0 (BTN2) with 20ms debounce, 500ms long-press threshold. FreeRTOS queue (64 slots) dispatched via GetEvent().
- Parameter editing: main/menupages/UIMenuPageParams.cpp parses plugin parameter JSON via RapidJSON, renders 6-item scrollable list on OLED, dispatches value changes through SoundProcessorManager::SetChannelParamValue().
- Plugin browser: main/menupages/UIMenuPageHome.cpp parses available processors JSON, renders 6-item scrollable plugin list, stereo flag shown. BTN2 short backs to main menu, BTN2 long loads plugin on channel 0. SDMMC slot 1, 4-bit, GPIO34 card detect.
- SD card: components/drivers/fs.cpp InitSD() initializes SDMMC slot 1 (4-bit, GPIO34 CD), mounts as LittleFS on /sd. CONFIG_LITTLEFS_SDMMC_SUPPORT must be enabled.

## Common Workflows
Document frequently used workflows and commands here.
