# Norns-Style GUI + Modulation Engine on CTAG TBD (ESP32-A1S)

## Target Hardware

### ESP32-A1S (Ai-Thinker Audio Kit v2.2)
- ESP32-D0WD, 240MHz dual-core, 8MB PSRAM, 4MB+ flash
- ES8388 codec (I2S: GPIO25/26/27/35, I2C: GPIO32/33)

### GPIO Allocation — Final

| GPIO | Function | Notes |
|------|----------|-------|
| 0 | BOOT button | **BTN4** (aux), strapping — OK post-boot |
| 1 | UART0 RX | USB-UART console |
| 3 | UART0 TX | USB-UART console |
| 2,4,12,13,14,15 | SDMMC 4-bit | microSD card |
| 5 | ENC_B | encoder phase B (repurposed from KEY6) |
| 16 | PSRAM CS | fixed |
| 17 | PSRAM CLK | fixed |
| 18 | MIDI UART1 TX | TRS MIDI OUT |
| 19 | MIDI UART1 RX | TRS MIDI IN |
| 21 | I2C SDA → OLED | |
| 22 | I2C SCL → OLED | |
| 23 | ENC_A | encoder phase A (only genuinely free GPIO) |
| 25 | I2S WS | ES8388 audio |
| 26 | I2S DOUT | ES8388 audio |
| 27 | I2S BCLK | ES8388 audio |
| 32 | I2C SCL → codec | ES8388 config |
| 33 | I2C SDA → codec | ES8388 config |
| 34 | SD card detect | input-only, polled |
| 35 | I2S DIN | ES8388 audio |
| 36 | ENC_BTN | encoder button, input-only, **needs ext 10k pull-up to 3.3V** |

### Control Layout (1 Encoder + 2 Buttons)
| Control | Physical | Norns Equivalent | Function |
|---------|----------|-----------------|----------|
| ENC | GPIO23(A) + GPIO5(B) | E1+E2+E3 combined | Panel switch (fast) / scroll / value adjust |
| BTN1 | encoder btn, GPIO36 | K1 | Short: MENU↔PLAY, Hold: ALT modifier |
| BTN2 | BOOT, GPIO0 | K2/K3 | Context: back *or* select (long press = enter) |

### Single-Encoder Navigation Design
Norns uses 3 encoders + 3 buttons. With 1 encoder + 2 buttons:

- **Normal turn**: scroll cursor / adjust value
- **Fast spin** (rapid turn): switch panel (MIX→TAPE→HOME→PARAMS)
- **BTN1 short**: toggle MENU ↔ PLAY mode
- **BTN1 long**: ALT modifier — changes encoder behavior to fast-scroll/fine-tune
- **BTN2 short**: back / exit
- **BTN2 long**: select / enter / confirm

### Hardware Notes
- Rotary encoder: GPIO23(A), GPIO5(B), common to GND — both have internal pull-ups
- Encoder button: GPIO36 → button → GND. GPIO36 is input-only, needs **external 10kΩ pull-up to 3.3V**
- BOOT(GPIO0): strapping pin — ok as input post-boot, already pulled up on board

---

## Phase 1: Encoder + Button Input System

### New Files
- `components/drivers/encoder.hpp` / `encoder.cpp` — PCNT rotary encoder driver (single unit)
- `main/UserInput.hpp` / `main/UserInput.cpp` — Unified event queue

### PCNT Config
- PCNT unit 0: channel 0=GPIO23(pulse), channel 1=GPIO5(control)
- Polled at 1kHz from FreeRTOS task (no per-edge interrupts)
- Sensitivity threshold: 2 ticks per delta unit
- Optional acceleration curve (delta^1.6) for fast-turn panel switching
- GPIO interrupt on GPIO36/GPIO0 falling edge → debounce 20ms → short (<500ms) / long (≥500ms)

### Event Queue (FreeRTOS, 64 slots)
```
ENC_DELTA (int), BTN_EVENT (id: 1/2, type: SHORT/LONG)
```

---

## Phase 2: SD Card Audio — Sample Streaming & Recording

### Modified Files
- `sdkconfig` — enable `CONFIG_LITTLEFS_SDMMC_SUPPORT=y`
- `components/drivers/fs.cpp` — SD card init + mount as second LittleFS volume on `/sd`
- New: `main/SDAudio.hpp` / `main/SDAudio.cpp`

### Sample Playback
- Read `.wav` from SD via LittleFS (16-bit, mono/stereo, 44.1kHz)
- Ring buffer in SPIRAM: 32KB = ~370ms mono audio
- New DSP plugin: `ctagSDRompler` — single/multi-sample player, MIDI note → key zone mapping, loop points, start offset, pitch

### Master Recording
- Tap post-master output (after soft clipper in `audio_task`)
- Write stereo 16-bit PCM to `.wav` on SD via LittleFS
- Double-buffered: 2 × 4KB buffers, one fills while other writes
- Start/stop via menu or MIDI CC

### Performance
- 4-bit SDMMC at 20MHz = ~10 MB/s read, ~5 MB/s write
- Audio needs: 44.1kHz × 2ch × 2B = 176 KB/s — well within budget

---

## Phase 3: Display Rendering Enhancements

### Modified Files
- `components/drivers/ssd1306.h` / `ssd1306.c`
- `main/Display.hpp` / `main/Display.cpp`

### New API
```
DrawPixel, DrawLine, DrawHLine, DrawRect, DrawString(x,y,font,...),
DrawStringRight, DrawVUMeter, DrawScrollbar, InvertRect (highlight)
```

### Fonts: 8×8 default + 5×7 compact. 1KB SPIRAM framebuffer, dirty-region tracking, ~30 FPS.

---

## Phase 4: Menu Engine (Norns-Style)

### New Files
- `main/UIMenu.hpp` / `main/UIMenu.cpp` — State machine + page dispatch
- `main/UIMenuPage.hpp` — Abstract interface
- `main/menupages/UIMenuPageHome.cpp`, `UIMenuPageParams.cpp`, `UIMenuPageTape.cpp`, `UIMenuPageMix.cpp`
- `main/menupages/UIModalConfirm.cpp`

### State Machine
```
mode: PLAY ↔ MENU (BTN1 toggles)
alt:  false ↔ true (BTN1 hold ≥ 250ms)
panel: MIX / TAPE / HOME / PARAMS (encoder fast-turn cycles)
page:  current page (BTN2 long enters, BTN2 short backs)
```

### Encoder Contexts
| Context | Normal Turn | Fast Turn | ALT + Turn |
|---------|------------|-----------|------------|
| Menu list | Scroll cursor | Switch panel | Page up/down |
| Param edit | Adjust value | Switch panel | Fine-tune |

### Navigation Tree (Norns-aligned, 3 levels max)
```
HOME
├── SELECT → plugin browser
├── SYSTEM → DEVICES / WIFI / SETTINGS / RESTART
├── FAVORITES → presets
├── SD CARD → file browser, samples, recording
└── SLEEP → confirm
```

---

## Phase 5: Parameter Editing + Modulation Engine

### Parameter Types
FLOAT, INT, ENUM, BOOL, TRIGGER, GROUP — read from `ctagSPDataModel`
Panel sub-modes: EDIT / MAP / MAPEDIT / PSET (matches Norns `params.lua`)

### Modulation Engine (Core 0, 100Hz esp_timer)
- **2 LFOs**: sine/tri/saw/sq/S&H, 0.01-100Hz, depth ±1, smoothing
- **2 Trigger Sequencers**: 16-step / euclidean, BPM 30-300 or MIDI clock sync
- **ModMatrix**: source → param_id + depth, up to 50 destinations
- Also accepts MIDI CC (UART or Bluetooth) as modulation source
- Zero impact on Core 1 audio — control-rate design, ~50 CPU cycles per source

---

## Phase 6: Bluetooth MIDI (Optional)
- Enable BT Classic SPP, advertise MIDI service
- Bytes fed into existing `Midi::Update()` pipeline
- BT CCs usable as modulation sources

---

## Phase 7: Integration

### Task Architecture (Final)
```
Core 1 (audio only, priority 23):
  └── audio_task → I2S read → DSP → I2S write

Core 0:
  ├── encoder_task           (idle+3, 1kHz)      NEW
  ├── ui_menu_task           (idle+3, 60Hz)      NEW
  ├── favorites_ui_task      (idle+2)            MODIFIED
  ├── mod_engine_timer       (esp_timer 100Hz)   NEW
  ├── sd_audio_task          (idle+2)            NEW
  ├── bluetooth_task         (idle+2)            optional
  ├── wifi/webserver         (various)
  └── serial_api_task        (idle+4)
```

### Memory Budget
| Component | RAM | Location |
|-----------|-----|----------|
| Display framebuffer | 1 KB | SPIRAM |
| SD sample ring buffer | 32 KB | SPIRAM |
| Menu/param cache | ~5 KB | SPIRAM |
| Mod matrix | ~2 KB | SPIRAM |
| Event queue | 512 B | Internal DRAM |
| **Total** | **~41 KB SPIRAM + 512 B DRAM** | |

DSP arena (112 KB internal DRAM) untouched. PSRAM: < 0.5% of 8 MB.

---

## Verification

| Phase | Test |
|-------|------|
| 1 | Encoder → console deltas. BTN1/BTN2 → short/long events detected |
| 2 | SD mounts at `/sd`, `.wav` plays, master reording writes valid WAV |
| 3 | Draw test pattern on OLED, no I2C errors with audio running |
| 4 | Navigate HOME→SYSTEM→SETTINGS→back, panel switch via fast spin |
| 5 | LFO modulates parameter from menu, no zipper noise |
| 6 | BT controller pairs, MIDI notes → audio, CC → modulation |
| Full | Heavy DSP + menu nav + SD playback + LFO mod → zero I2S underruns, no memory leaks over 30 min |
