# Taste (Continuously Learned by [CommandCode][cmd])

[cmd]: https://commandcode.ai/

# workflow
See [workflow/taste.md](workflow/taste.md)

# flash
- When flashing an ESP32 with `idf.py flash` or `esptool.py`, let the flash command run to completion without terminating it via timeouts, fuser -k, or premature cancellation — the flash process must finish on its own. Do NOT set any explicit timeout parameter whatsoever on shell_command for flash operations; just run the command and let it finish naturally. Confidence: 0.98
# sd-card
- For Ai-Thinker ESP32-A1S SD card support, mount SD as FAT/VFAT filesystem using `esp_vfs_fat_sdmmc_mount()`, not LittleFS — users expect standard FAT32-formatted cards. Confidence: 0.75
- Set `gpio_cd = GPIO_NUM_NC` (skip card detect) in the SDMMC slot config to avoid hangs; the card detect pin (GPIO34) polling can hang `esp_vfs_fat_sdmmc_mount()`. Confidence: 0.70
# build
- After writing new code that uses cross-namespace components (e.g., SDAudio), expect build errors from unqualified namespace access — proactively add the required `using namespace` directive and cast uint32_t to `(unsigned)` in snprintf format strings. Confidence: 0.70

# bluetooth
- For BT MIDI, prefer BLE MIDI via NimBLE over Classic BT SPP — no PIN entry needed from controller. Confidence: 0.50
- To fit BLE MIDI (NimBLE) on ESP32 with audio + WiFi, disable WiFi IRAM optimizations, ULP coprocessor, and use size compiler optimization. Confidence: 0.70
- ESP32-D0WD-V3 rev3.1 with PSRAM + dual-core requires the Interrupt Watchdog (CONFIG_ESP_INT_WDT) to be enabled — it is a hardware errata workaround and cannot be disabled. Confidence: 0.80
- When BLE MIDI (NimBLE) coexists with I2S init on ESP32 Rev3, the interrupt watchdog timeout must be increased to ~5000ms to accommodate BT coexistence calibration during audio codec init. Confidence: 0.70

# architecture
- Use lock-free SPIRAM ring buffers for cross-task audio data transfer between real-time audio task (Core 1, prio 23) and async worker tasks (Core 0, low prio). Confidence: 0.65

# build
- Edit board-specific sdkconfig defaults in the active defaults file (e.g., `sdkconfig.defaults.bba` for the BBA board variant) — not the generated `sdkconfig` file, which is auto-generated and gets overwritten. Check which defaults file the build actually reads (CMakeLists.txt or SDKCONFIG_DEFAULTS) before assuming the file name. Confidence: 0.70
- When adding `PRIV_REQUIRES` or `REQUIRES` to a component's CMakeLists.txt that includes a file compiled across multiple platform branches (e.g., `fs.cpp` in `drivers/CMakeLists.txt` used by MK2, BBA, and else branches), the requirement must be added to ALL branches — the build system checks the requirement against every branch that includes the file, not just the active one. Confidence: 0.75

# debugging
- Never use `sudo` in non-interactive commands when flashing or reading serial from the device — it will hang waiting for password input. Instead, instruct the user to run the command or fix permissions themselves. Confidence: 0.88
- When debugging an ESP32 crash (Guru Meditation / panic), decode the backtrace addresses via `xtensa-esp32-elf-addr2line` to confirm the root cause before implementing a fix — don't guess or apply workarounds blindly. Confidence: 0.65
- When capturing serial output from the ESP32 for debugging, capture the entire boot log in one shot (15-25s continuous read) instead of doing multiple small filtered captures — piecemeal captures waste time and miss context. Confidence: 0.70

# debugging
- Use `idf.py -p /dev/ttyUSB0 monitor` to read serial output from ESP32 — do not write custom Python serial scripts with DTR/RTS toggling for basic serial monitoring. Confidence: 0.70

# i2c
- On ESP-IDF v5.x with BT enabled, use the modern I2C master driver (`driver/i2c_master.h`) instead of the legacy I2C driver — the legacy driver's ISR conflicts with BT controller interrupts causing I2C FSM hangs. Confidence: 0.65
- When migrating from legacy `driver/i2c.h` to modern `driver/i2c_master.h`, the legacy I2C address byte includes the R/W bit shift (e.g., `0x20` for write = `0x10` << 1). The modern API expects the raw 7-bit address (e.g., `0x10`), not the shifted byte. Verify this for each migrated device. Confidence: 0.80
- When migrating from legacy I2C driver, the legacy `i2c_driver_install()` used `ESP_INTR_FLAG_SHARED | ESP_INTR_FLAG_LOWMED`. The modern `i2c_master.h` API's `i2c_master_bus_config_t.intr_priority` defaults to 0 (driver selects 1-3), which can be higher than LOWMED. Set `intr_priority = 1` to maintain low interrupt priority and avoid preempting spinlocks. Confidence: 0.75

# display
See [display/taste.md](display/taste.md)
# navigation
- Use Norns-style hierarchy navigation (ROOT/PANEL_IN) with encoder switching panels at root level, OK to enter a panel, BACK to go up one level — instead of the old toggle-inMenu model. Confidence: 0.75
- Use BTN2_SHORT as the primary OK/enter action (not LONG), with LONG reserved for special/alternative actions. Confidence: 0.70
- The `UIMenu::TaskFunction` needs 8192 bytes of task stack minimum — the deep call chains from redraw → DrawString → DrawPixel → MarkDirty plus rapidjson Document parsing in parsePlugins() overflow smaller stacks. Confidence: 0.80
- In boot sequence, call `AUDIO::SoundProcessorManager::StartSoundProcessor()` BEFORE `UIMenu::Init()` and `xTaskCreatePinnedToCore(ui_menu)` — the UIMenu task accesses SPManager queues (parsePlugins via onButton), and creating the task before audio init completes causes `xQueueSemaphoreTake` assertions. Confidence: 0.80
- Add an `onBack()` virtual method to UIMenuPage base class returning true if the page handled back (went to previous sub-page) or false if at top level (UIMenu returns to ROOT). Each page owns its own sub-page stack. Confidence: 0.75

# plugin
- For mono plugins, show a channel selection submenu (Ch0/Ch1/Both) after pressing OK — stereo plugins load directly to channel 0. Confidence: 0.70

# debugging
- Before guessing or theorizing about display/behavior issues, first check any photo evidence the user provided (referenced as `@log/photo_...`) — examine the actual visual output before reasoning about what might be wrong. Do not theorize or make code changes without first looking at available photo evidence. This is a hard rule: if a photo is referenced in recent context, read it before making any statements or changes about display appearance. The user will yell if you skip this. Confidence: 0.90

# git
- When reverting component files to an older commit to test a regression, first verify the target commit's files are compatible with the current board hardware (e.g., GPIO pins, chip variant) — not all past commits target the same platform. Confidence: 0.70
- Make regular git commits when debugging a crash so you can bisect and trace when the crash started — without commits, there is no history to revert to or bisect from. Confidence: 0.82
- When iterating on display coordinate/rendering fixes (page and bit formulas for SSD1306), commit each attempted formula combination to git so the visual state at each commit is reproducible and recoverable — do not make multiple untracked edits flipping between formulas without committing. The user needs to see and compare visual output for each distinct formula combination, and without commits there is no way to revert to a previous visual state or track what produced it. Confidence: 0.88
# debugging
- After a successful `idf.py build flash`, subsequent boot tests only need a hardware reset (DTR/RTS toggle) — do not re-flash just to check the boot log, as flashing is slow and unnecessary. Confidence: 0.75
- After applying a fix and flashing, use `idf.py monitor` to capture the boot log and verify the fix works before reporting to the user — don't assume the fix succeeded or ask the user to check the log. Confidence: 0.65
- When a Kconfig option silently falls back to its default (e.g., INT_WDT_TIMEOUT_MS=15000 but max is 10000), check the generated `build/config/sdkconfig.h` to verify the actual value being used — don't assume the set value took effect. Confidence: 0.85
- When debugging a complex crash, document each failed attempt and what was learned before moving to the next approach — prevents repeating the same failed experiments. Confidence: 0.75
- To isolate a regression, test the known-good commit's code with current configs AND current code with the known-good commit's configs separately — this tells you whether the issue is in code changes or config changes. Confidence: 0.75

# gpio
- On ESP32 Rev3 with PSRAM, GPIO button ISRs (using `xQueueGenericSendFromISR`) must NOT be installed before I2S codec init — pin noise during the I2S MCLK spinlock (`clkout_mapping_alloc` → `i2s_check_set_mclk`) triggers the ISR, which tries to acquire the same spinlock, causing Interrupt WDT timeout. Fix: defer `gpio_install_isr_service()` + `gpio_isr_handler_add()` to after `Codec::InitCodec()` completes. Confidence: 0.85
- GPIO5 (BBA) is used by both the encoder PCNT unit 0 (signal B) AND `Favorites.cpp` as button input (`PIN_PUSH_BTN`). This conflict means encoder rotation triggers the old Favorites UI state machine. When running the new UIMenu system, `Favorites::StartUI()` must be commented out/disbled in `SPManager::StartSoundProcessor()` to prevent the old task from writing to the display and misreading the encoder signal. The Favorites data model (StoreFavorite, ActivateFavorite via REST/MIDI API) still works without the UI task. Confidence: 0.85

