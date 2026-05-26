# Taste (Continuously Learned by [CommandCode][cmd])

[cmd]: https://commandcode.ai/

# workflow
See [workflow/taste.md](workflow/taste.md)
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

# i2c
- On ESP-IDF v5.x with BT enabled, use the modern I2C master driver (`driver/i2c_master.h`) instead of the legacy I2C driver — the legacy driver's ISR conflicts with BT controller interrupts causing I2C FSM hangs. Confidence: 0.65

# git
- When reverting component files to an older commit to test a regression, first verify the target commit's files are compatible with the current board hardware (e.g., GPIO pins, chip variant) — not all past commits target the same platform. Confidence: 0.70
- Make regular git commits when debugging a crash so you can bisect and trace when the crash started — without commits, there is no history to revert to or bisect from. Confidence: 0.80
# debugging
- When a Kconfig option silently falls back to its default (e.g., INT_WDT_TIMEOUT_MS=15000 but max is 10000), check the generated `build/config/sdkconfig.h` to verify the actual value being used — don't assume the set value took effect. Confidence: 0.85
- When debugging a complex crash, document each failed attempt and what was learned before moving to the next approach — prevents repeating the same failed experiments. Confidence: 0.75
- To isolate a regression, test the known-good commit's code with current configs AND current code with the known-good commit's configs separately — this tells you whether the issue is in code changes or config changes. Confidence: 0.75

