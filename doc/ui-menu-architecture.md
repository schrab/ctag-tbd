# UIMenu / Page Architecture (Norns-Style)

## Key Files

| File | Role |
|------|------|
| `main/UIMenu.hpp` / `.cpp` | Menu controller — owns navigation state and page registry |
| `main/UIMenuPage.hpp` | Abstract base class for all pages |
| `main/menupages/UIMenuPage*.hpp/.cpp` | Concrete page implementations (Home, Mix, Tape, Params, BtMidi) |
| `main/UserInput.hpp` / `.cpp` | Hardware input: encoder + 2 buttons → FreeRTOS event queue |
| `main/Display.hpp` / `.cpp` | OLED framebuffer (128x64, SSD1306) |
| `main/main.cpp` | Boot sequence: splash → `StartSoundProcessor()` → `UIMenu::Init()` → task |

## Navigation State Machine

```
ROOT (encoder switches panels)
  │  OK (BTN2_SHORT) → enter panel
  │  BACK (BTN1_SHORT) → no-op
  ▼
PANEL_IN (encoder scrolls page items)
  │  OK → `page.onButton(2, false)` (select/enter)
  │  BACK → `page.onBack()` → if false: return to ROOT
  │  LONG OK → `page.onButton(2, true)`
  │  LONG BACK → `page.onButton(1, true)`
  ▼
Sub-pages (page-owned, via onBack())
```

**Event mapping:**
- `BTN1` = GPIO36 = BACK
- `BTN2` = GPIO0 (BOOT button) = OK
- Encoder A/B on GPIO23/GPIO5 via PCNT unit 0

**States:** `enum NavState : uint8_t { ROOT, PANEL_IN }` in `UIMenu.hpp`

## Abstract Page Interface (`UIMenuPage.hpp`)

```cpp
class UIMenuPage {
public:
    virtual ~UIMenuPage() = default;
    virtual void init() = 0;
    virtual void deinit() = 0;
    virtual void doRedraw() = 0;
    virtual void onEncoder(int delta) {}   // encoder rotation while in this page
    virtual void onButton(int btnId, bool longPress) {}
    virtual bool onBack() { return false; } // true=handled sub-page back, false=return to ROOT
};
```

## Page Registration

Pages are hardcoded in a fixed array by `UIMenu::Init()`:

```cpp
enum Panel : uint8_t { PANEL_MIX=0, PANEL_TAPE=1, PANEL_HOME=2, PANEL_PARAMS=3 };
// + PANEL_BT when CONFIG_BT_ENABLED
static UIMenuPage *pages[PANEL_COUNT];
static Panel currentPanel;     // starts at PANEL_HOME
static NavState navState;      // starts at ROOT
static bool redrawNeeded;      // set true on state transitions
static int panelBarTimer;      // counts down from 50 (~1s) in ROOT
```

## Norns-Style Indicator Bar (`drawPanelBar()`)

- 3px-tall rectangles at y=0, one per panel, centered in their zone
  - **active panel**: filled 3×3 rectangle
  - **inactive panels**: 3×3 rectangle with 1px border (unfilled)
- Timer: decrements every 20ms tick from 50 to 0, then bar fades out
- No text labels in the bar

## Event Flow

```
UserInput task (Core 0, idle+3)
  → polls encoder (PCNT, 1kHz) + buttons (20ms debounce)
  → pushes events to FreeRTOS queue (64 slots)

UIMenu task (Core 0, idle+3, 8192 stack, 20ms loop)
  → UserInput::GetEvent(ev, 20)
  → if ROOT:
       ENC_DELTA → switch panel (accelerated: abs>1 → /2)
       BTN2_SHORT → enter PANEL_IN
       BTN1_SHORT/LONG → no-op
  → if PANEL_IN:
       ENC_DELTA → pages[current]->onEncoder(delta)
       BTN2_SHORT → pages[current]->onButton(2, false)
       BTN2_LONG  → pages[current]->onButton(2, true)
       BTN1_SHORT → pages[current]->onBack(); if false → ROOT
       BTN1_LONG  → pages[current]->onButton(1, true)
  → redrawNeeded? if ROOT: page->doRedraw() + drawPanelBar()
                  if PANEL_IN: page->doRedraw()
  → panelBarTimer-- each tick in ROOT
```

## Display API

| Method | Purpose |
|--------|---------|
| `Clear()` | Clear framebuffer (memset + mark all pages dirty) |
| `Flush()` | Push dirty pages to OLED via I2C |
| `DrawString(x,y,str,font)` | Text (FONT_5X7 or FONT_8X8) |
| `DrawStringRight(x,y,str,font)` | Right-aligned text |
| `InvertRect(x,y,w,h)` | Invert region (cursor highlight) |
| `DrawScrollbar(x,y,h,total,cursor)` | Scrollbar |
| `DrawVUMeter(x,y,w,h,level)` | VU bar (0.0-1.0) |
| `DrawPixel/HLine/VLine/Rect` | Primitives |

Screen: 128x64px. FONT_5X7 = ~9 rows × ~21 chars, FONT_8X8 = 8 rows × 16 chars.

Only FONT_5X7 is used in menu UI. FONT_8X8 is legacy (only `font8x8_basic_tr` for `ssd1306_display_text()`).

**Framebuffer notes:** 1024 bytes = 128 columns × 8 pages. Page-major, column-minor. SSD1306 with 0xC8 COM scan requires `page = 7 - (y >> 3)` and `bit = 7 - (y & 7)` in DrawPixel/InvertRect.

**Drawing functions (`DrawString`, `DrawVUMeter`, `DrawScrollbar`) do NOT call `Flush()` internally.** Each `redraw*()` method calls `Display::Flush()` once after all drawing is complete. `Clear()` does NOT do direct I2C — only `memset(fb, 0)` + dirty flags.

## Existing Pages

### HOME (`UIMenuPageHome`)
| SubPage | What it shows | Encoder | OK (BTN2_SHORT) | Back |
|---------|--------------|---------|-----------------|------|
| `SP_MAIN` | Menu items: SELECT, SYSTEM, FAVORITES, SD CARD, SLEEP | Scroll cursor (0-5) | Enter sub-page at cursor | Return to ROOT |
| `SP_SELECT` | Plugin list (parsed from JSON, max 64) | Scroll list (auto-scroll, 6 visible) | Load plugin: stereo → ch0 directly, mono → channel picker | Back to SP_MAIN |
| `SP_SELECT_CH` | Channel options: Ch0 / Ch1 / Both | Scroll 3 options | Load to selected channel(s) | Back to SP_SELECT |

### PARAMS (`UIMenuPageParams`)
| Mode | What it shows | Encoder | OK (BTN2_SHORT) | Back |
|------|--------------|---------|-----------------|------|
| `MODE_SELECT` | EDIT / MAP / PSET | Scroll cursor (0-2) | Enter selected mode | Return to ROOT |
| `MODE_EDIT` | Param list (parsed from JSON, max 64) | Scroll list (auto-scroll, 6 visible) | Enter `MODE_VALUEEDIT` for selected param | Back to MODE_SELECT |
| `MODE_VALUEEDIT` | Same as MODE_EDIT layout | Adjust param value (`pi.current += delta`) | Return to MODE_EDIT | Back to MODE_EDIT |
| `MODE_MAP` / `MODE_PSET` | "Not implemented" placeholder | No-op | No-op | Back to MODE_SELECT |

### TAPE (`UIMenuPageTape`)
| SubPage | What it shows | Encoder | OK (BTN2_SHORT) | Back |
|---------|--------------|---------|-----------------|------|
| `SP_MAIN` | PLAY FILE / RECORD / STOP (or PLAYING/RECORDING status) | Scroll cursor (0-2) | PL→file list, REC→record screen, STOP→stop | Return to ROOT |
| `SP_FILELIST` | .wav files from SD card | Scroll list (auto-scroll) | Load selected file for playback | Back to SP_MAIN |
| `SP_RECORD` | START REC / CANCEL | Scroll cursor (0-1) | START→record, CANCEL→stop | Back to SP_MAIN |

### MIX (`UIMenuPageMix`)
No sub-pages. Shows "MIX" + placeholder VU meters. No encoder/button handlers. `onBack()` returns false (ROOT directly).

### BT MIDI (`UIMenuPageBtMidi`) — only when `CONFIG_BT_ENABLED`
| SubPage | What it shows | Encoder | OK (BTN2_SHORT) | Back |
|---------|--------------|---------|-----------------|------|
| `SP_MAIN` | SCAN, Disconnect, Status | Scroll cursor (0-2) | LONG OK only (preserved) | Return to ROOT |
| `SP_SCAN` | Scanning status + device list | Scroll list | OK on device → connect | Stop scan + back to SP_MAIN |

## Boot Sequence (`main.cpp`)

```
1. Display::Init()                     — I2C init, OLED config
2. Display::ShowFWVersion()            — framebuffer-based, FONT_5X7, 2s delay
3. FileSystem::InitSD()                — SD card (silent fail)
4. SoundProcessorManager::StartSoundProcessor()  — audio init (creates audio task + Favorites data model)
5. UIMenu::Init()                      — creates pages, sets navState=ROOT, redrawNeeded=true
6. xTaskCreate(UIMenu::TaskFunction, 8192, idle+3, core 0)
7. UserInput::EnableISR()              — GPIO ISRs (after audio init, avoid MCLK spinlock)
```

The UIMenu task starts AFTER `StartSoundProcessor()` completes. This ensures the audio pipeline is ready when pages query sound processor state.

## Adding a New Page

1. Create `main/menupages/UIMenuPageFoo.hpp` and `.cpp` subclassing `UIMenuPage`
2. Add enum value to `UIMenu::Panel` in `UIMenu.hpp`
3. `new` it in `UIMenu::Init()`
4. Implement `onBack()` for sub-page navigation
5. Use BTN2_SHORT as the primary select/enter action (not LONG)

## Common Pitfalls

- **Stack overflow**: UIMenu task stack is 8192 bytes. JSON parsing in `parsePlugins()`/`parseParams()` uses stack-heavy RapidJSON `Document`. If overflowing, increase stack in `main.cpp`.
- **I2C contention**: All display writes from a single task (UIMenu). No mutex needed.
- **GPIO5 conflict**: PIN_PUSH_BTN for old Favorites system is GPIO5, which is also encoder signal B. The old `Favorites::ui_task` is DISABLED in `SPManager.cpp`.
- **Button ISR timing**: `UserInput::EnableISR()` must be called after `Codec::InitCodec()` to avoid ISR firing inside I2S MCLK spinlock on ESP32 Rev3.
- **SPModel assert on boot**: If storage partition doesn't have `spm-config.jsn` (e.g. after format), the firmware asserts. Use `idf.py flash` to write the storage partition from `spiffs_image/`.
