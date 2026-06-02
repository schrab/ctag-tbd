# UIMenu / Page Architecture (Norns-Style)

## Key Files

| File | Role |
|------|------|
| `main/UIMenu.hpp` / `.cpp` | Menu controller — owns navigation state and page registry |
| `main/UIMenuPage.hpp` | Abstract base class for all pages |
| `main/menupages/UIMenuPage*.hpp/.cpp` | Concrete page implementations (Home, Mix, Tape, Params, System, Midi, Mod) |
| `main/UserInput.hpp` / `.cpp` | Hardware input: encoder + 2 buttons → FreeRTOS event queue |
| `main/Display.hpp` / `.cpp` | OLED framebuffer (128x64, SSD1309) |
| `main/main.cpp` | Boot sequence: splash → `StartSoundProcessor()` → `UIMenu::Init()` → task |

## Navigation State Machine

```
ROOT (encoder switches panels)
  │  OK (BTN2_SHORT) → enter panel
  ▼
PANEL_IN (encoder scrolls page items)
  │  BACK (BTN1_SHORT) → `page.onBack()` → if false: return to ROOT
  │  OK   (BTN2_SHORT) → `page.onButton(2, false)` (select / enter)
  │  MOD  (BTN2_LONG)  → `page.onButton(2, true)` (MOD mapping)
  ▼
Sub-pages (page-owned, via onBack())
```

**Event mapping (two ADC buttons):**
| Physical button | ADC range | Event emitted | UIMenu dispatch | Role |
|---------------|-----------|---------------|-----------------|------|
| SW1 (short) | ~1850-1950 | `BTN2_SHORT` | `onButton(2, false)` | OK |
| SW1 (long) | ~1850-1950 | `BTN2_LONG` | `onButton(2, true)` | MOD |
| SW2 (short) | ~1100-1200 | `BTN1_SHORT` | `onBack()` | BACK |
| SW2 (long) | ~1100-1200 | `BTN1_LONG` | `onBack()` | BACK (long=short) |
| Both | ~800-900 | (no events) | — | No action |

Encoder A on GPIO5, B on GPIO23 via PCNT unit 0. **GPIO0 is I2S MCLK — NOT a button.**
GPIO36 is ADC1_CH0, shared by both buttons via voltage divider (see doc/ui-input.md).

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
enum Panel : uint8_t { PANEL_MIX=0, PANEL_TAPE=1, PANEL_HOME=2, PANEL_MOD=3, PANEL_PARAMS=4 };
// + PANEL_MIDI=5 when CONFIG_BT_ENABLED
// PANEL_COUNT = 5 (no BT) or 6 (BT enabled)
// Note: SYSTEM is NOT a panel — it's a SP_SYSTEM sub-page of HOME
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
  → polls encoder (PCNT, 1kHz) + ADC buttons (10ms, 4-sample average, 20ms debounce)
  → pushes events to FreeRTOS queue (64 slots)

UIMenu task (Core 0, idle+3, 4096 stack, 20ms loop)
  → UserInput::GetEvent(ev, 20)
  → if ROOT:
       ENC_DELTA → switch panel (accelerated: abs>1 → /2)
       BTN2_SHORT → enter PANEL_IN (OK)
  → if PANEL_IN:
       ENC_DELTA → pages[current]->onEncoder(delta)
       BTN1_SHORT/LONG → pages[current]->onBack(); if false → ROOT (BACK)
       BTN2_SHORT → pages[current]->onButton(2, false) (OK)
       BTN2_LONG  → pages[current]->onButton(2, true) (MOD)    
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
| `DrawScrollbar(x,y,h,total,cursor)` | Scrollbar (visible items = h/LINE_H) |
| `DrawVUMeter(x,y,w,h,level)` | VU bar (0.0-1.0) |
| `DrawPixel/HLine/VLine/Rect` | Primitives |

Screen: 128x64px. FONT_5X7 = 7 rows × ~21 chars, FONT_8X8 = 8 rows × 16 chars.

Only FONT_5X7 is used in menu UI. FONT_8X8 is legacy (only `font8x8_basic_tr` for `ssd1306_display_text()`).

**Framebuffer notes:** 1024 bytes = 128 columns × 8 pages. Page-major, column-minor. Physical display is SSD1309 (driver compatible with SSD1306/SSD1309, 0xC8 COM scan), requiring `page = 7 - (y >> 3)` and `bit = 7 - (y & 7)` in DrawPixel/InvertRect.

**Drawing functions (`DrawString`, `DrawVUMeter`, `DrawScrollbar`) do NOT call `Flush()` internally.** Each `redraw*()` method calls `Display::Flush()` once after all drawing is complete. `Clear()` does NOT do direct I2C — only `memset(fb, 0)` + dirty flags.

## Layout Constants (in `Display.hpp`)

All Y positions, visible-item counts, and scrollbar dimensions are derived from these:

```cpp
constexpr int FONT_H = 7;              // font glyph height
constexpr int LINE_H = FONT_H + 1;     // 8px — 7px font + 1px gap
constexpr int ITEM_Y0 = 5;             // first item Y (no-header pages)
constexpr int ITEM_Y0_HDR = 13;        // first item Y (header pages: 5 + 7 + 1)
constexpr int VISIBLE_ITEMS = 7;       // items per page (no-header)
constexpr int VISIBLE_ITEMS_HDR = 6;   // items per page (header pages)

#define ROW(n)     (ITEM_Y0 + (n) * LINE_H)        // e.g. ROW(2) = 21
#define ROW_HDR(n) (ITEM_Y0_HDR + (n) * LINE_H)    // e.g. ROW_HDR(0) = 13
```

Two inline helpers standardize scroll logic across all pages:

```cpp
inline void ClampScroll(cursor, &scrollOffset, total, visible);
inline int  ClampVisible(total, scrollOff, maxVis);
```

To change fonts (e.g. to 04B_03__ at 5px), edit `FONT_H` and recompile — every Y position, visible count, scrollbar size, and scroll offset recomputes automatically.

## Existing Pages

## DRAM Exhaustion → Silent Task Creation Failure

**Symptom:** UIMenu task is created but never runs. Display shows splash then freezes. Encoder and buttons have no effect. No "TaskFunction: starting main loop" log appears. `xTaskCreatePinnedToCore` returns `-1` (`errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY`).

**Root cause:** The UIMenu task needs ~8.3KB contiguous DRAM (8KB stack + TCB). On ESP32-D0WD-V3 rev3.1 with PSRAM, after audio init, only ~22KB DRAM remains with largest free block ~17KB. If `UserInput::Init()` is called **before** `xTaskCreatePinnedToCore`, it consumes 2KB for the input task stack, fragments the heap, and the UIMenu task's 8KB allocation fails.

**Trigger:** Commit `79879b0` moved `UserInput::Init()` from `TaskFunction` (post-creation) into `UIMenu::Init()` (pre-creation). The 2KB input task allocation reduced the largest free block below 8KB before the UIMenu task could claim it.

**Fix (applied):**
1. `UserInput::Init()` stays inside `TaskFunction` — called after the UIMenu task stack is allocated
2. UIMenu task stack reduced from 8192 → 4096 (polls queue + draws display, plenty of headroom)
3. `UserInput::EnableISR()` remains in `main.cpp` after `xTaskCreatePinnedToCore`

**Prevention:** Never call `UserInput::Init()` before the UIMenu task is created. Keep the allocation order: create UIMenu task first (large stack), then let TaskFunction create the input task (smaller stack). Monitor `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` if binary size grows.

### HOME (`UIMenuPageHome`)
| SubPage | What it shows | Encoder | OK (BTN2_SHORT) | Back |
|---------|--------------|---------|-----------------|------|
| `SP_MAIN` | Menu items: SELECT, SYSTEM, FAVORITES, SD CARD, SLEEP | Scroll cursor (0-5) | Enter sub-page at cursor (SYSTEM → SP_SYSTEM, lazy-allocates UIMenuPageSystem) | Return to ROOT |
| `SP_SELECT` | Plugin list (parsed from JSON, max 64) | Scroll list (auto-scroll, 7 visible) | Load plugin: stereo → ch0 directly, mono → channel picker | Back to SP_MAIN |
| `SP_SELECT_CH` | Channel options: Ch0 / Ch1 / Both | Scroll 3 options | Load to selected channel(s) | Back to SP_SELECT |
| `SP_SYSTEM` | System config page (delegates to UIMenuPageSystem) | Forwarded to System page | Forwarded to System page | System page onBack() → SP_MAIN |

### PARAMS (`UIMenuPageParams`)
| Mode | What it shows | Encoder | OK (BTN2_SHORT) | Back |
|------|--------------|---------|-----------------|------|
| `MODE_SELECT` | EDIT / MAP / PSET (or dual Ch0:name / Ch1:name if two different mono plugins loaded) | Scroll cursor (0-2, or 0-3 with dual) | Enter selected mode (selects `chan` 0 or 1 for EDIT) | Return to ROOT |
| `MODE_GROUP` | Group list with param count, e.g. `Analogue BD (12)`, `Diffusion (5)` | Scroll list (auto-scroll, 7 visible) | Enter MODE_EDIT for that group | Back to MODE_SELECT |
| `MODE_EDIT` | Param list for selected group (or all params if no groups) | Scroll list (auto-scroll, 7 visible) | Enter `MODE_VALUEEDIT` for selected param | Back to MODE_GROUP (or MODE_SELECT if no groups) |
| `MODE_VALUEEDIT` | Same as MODE_EDIT layout | Adjust param value (`pi.current += delta`) | Confirm value → return to MODE_EDIT | Back to MODE_EDIT (no save) |
| `MODE_MAP` | CV slot editor for selected param (-1=None, 0-99=slot index) | Scroll CV slot (-1 through 99, names from CVSlotNames.hpp) | Confirm slot via `SetChannelParamValue(chan, id, "cv", slot)` → MODE_EDIT | Exit without saving → MODE_EDIT |
| `MODE_PSET` | "Not implemented" placeholder | No-op | No-op | Back to MODE_SELECT |

Long-press on a param in MODE_EDIT enters MODE_MAP. `ParamInfo[256]` and `GroupInfo[32]` in SPIRAM. Standalone leaf params get implicit "General" group. If only one group exists, it auto-opens into MODE_EDIT directly (skipping MODE_GROUP). MODE_MAP shows "MAPPING" title + "OK=save BACK=exit" instruction.

### TAPE (`UIMenuPageTape`)
| SubPage | What it shows | Encoder | OK (BTN2_SHORT) | Back |
|---------|--------------|---------|-----------------|------|
| `SP_MAIN` | PLAY FILE / RECORD / STOP (or PLAYING/RECORDING status) | Scroll cursor (0-2) | PL→file list, REC→record screen, STOP→stop | Return to ROOT |
| `SP_FILELIST` | .wav files from SD card | Scroll list (auto-scroll, 7 visible) | Load selected file for playback | Back to SP_MAIN |
| `SP_RECORD` | START REC / CANCEL | Scroll cursor (0-1) | START→record, CANCEL→stop | Back to SP_MAIN |

### SYSTEM (`UIMenuPageSystem`) — HOME sub-page
System config is entered as `SP_SYSTEM` from HOME's `SP_MAIN` (cursor=1, "SYSTEM"). It is NOT a panel tab. Instances are lazy-allocated in `UIMenuPageHome::onButton()` to keep boot allocation small.

| Mode | What it shows | Encoder | OK (BTN2_SHORT) | Back |
|------|--------------|---------|-----------------|------|
| Browse | Config item list (parsed from `GetCStrJSONConfiguration()`, scrollable, 7 visible) | Scroll list | Toggle `editMode` on current item | If editing → exit edit; else → SP_MAIN |
| Edit | Same list layout, right-side value highlighted (24px) | Adjust value (`valInt += delta`, clamped to min/max) | Toggle `editMode` off | Exit edit |

**Config items displayed:**

| # | id | name | type | options | deferred | Notes |
|---|-----|------|------|---------|----------|-------|
| 0 | `ng_config` | Noise Gate | enum | on,off | no | |
| 1 | `ch01_daisy` | Ch 0+1 Daisy | enum | on,off | no | |
| 2 | `ch0_toStereo` | Ch0→Stereo | enum | on,off | no | |
| 3 | `ch1_toStereo` | Ch1→Stereo | enum | on,off | no | |
| 4 | `ch0_outputSoftClip` | Ch0 Soft Clip | enum | on,off | no | |
| 5 | `ch1_outputSoftClip` | Ch1 Soft Clip | enum | on,off | no | |
| 6 | `ch0_codecLvlOut` | Ch0 Out Level | int | — | no | 0–33 |
| 7 | `ch1_codecLvlOut` | Ch1 Out Level | int | — | no | 0–33 |
| 8 | `input_source` | Input Source | enum | mic,line | yes | mic=LIN1 (mono), line=LIN2/RIN2 (stereo) |
| 9 | `input_gain` | Input Gain | int | — | yes | 0–8 (+0dB to +24dB) |
| 10 | `output_source` | Output Route | enum | hp,amp,all | yes | hp=LOUT1/ROUT1, amp=LOUT2/ROUT2 |
| 11 | `mixer_mode` | Mixer Mode | enum | dac,bypass,mix | yes | dac=DSP only, bypass=analog in, mix=both |
| 12 | `oled_brightness` | OLED Brightness | int | — | yes | 0–255 |

Deferred items are only committed on OK or BACK, not on encoder scroll.

**Persistence:**
- `applyCurrent()` loads the full config JSON, overlays only managed fields in-place, then calls `SetConfigurationFromJSON()`. This preserves `cv_ch0..cv_ch3`, `wifi`, and other keys not displayed in the UI.
- On `deinit()`, if `itemCount > 0`, calls `applyCurrent()` to persist any unsaved changes.
- Non-deferred items are applied immediately on encoder change (`applyCurrent(false)`).
- Deferred items are applied only on OK or BACK (`applyCurrent(true, true)` — skips non-deferred to avoid double-write).
No sub-pages. Shows "MIX" + placeholder VU meters. No encoder/button handlers. `onBack()` returns false (ROOT directly).

### MOD (`UIMenuPageMod`)
| SubPage | What it shows | Encoder | OK (BTN2_SHORT) | Back |
|---------|--------------|---------|-----------------|------|
| `SP_MAIN` | Overview: LFO1 shape/rate, LFO2 shape/rate, CC count | Scroll cursor (0-2) | Enter sub-page at cursor | Return to ROOT |
| `SP_LFO1` / `SP_LFO2` | Shape / Rate (Hz) / Amp / Output CV slot | Scroll cursor (not editing); adjust value (editing) | Toggle editing mode on/off | If editing: exit edit mode; else back to SP_MAIN |
| `SP_CC_SLOTS` | Scrolling list of 8 CC slots (CC# / chan / CV slot) | Scroll list (VISIBLE_ITEMS_HDR=6 visible, scrollbar) | Enter SP_CC_EDIT at cursor | Back to SP_MAIN |
| `SP_CC_EDIT` | CC number / MIDI channel / Learn toggle | Scroll cursor (not editing); adjust value (editing, cursors 0-1). Cursor 2 (Learn): OK starts/stops learn | Toggle editing mode on cursors 0-1; toggle Learn on cursor 2 | If editing: exit edit mode; else back to SP_CC_SLOTS |

Editing mode visual: value portion (right side) inverted instead of full row. Saves to SPIFFS (`/spiffs/data/mod-config.jsn`) on every value change.

### MIDI (`UIMenuPageMidi`) — only when `CONFIG_BT_ENABLED`
| SubPage | What it shows | Encoder | OK (BTN2_SHORT) | Back |
|---------|--------------|---------|-----------------|------|
| `SP_MAIN` | SCAN, Disconnect, Status, UART ON/OFF, BLE ON/OFF | Scroll cursor (0-4) | Toggle UART/BLE on cursor 3/4; LONG OK preserved for SCAN | Return to ROOT |
| `SP_SCAN` | Scanning status + device list | Scroll list | OK on device → connect | Stop scan + back to SP_MAIN |

MIDI source toggling: `Midi::SetUartEnabled()` / `SetBleEnabled()` flags guard reads in `Midi::Update()`. Cursor 3 toggles UART RX, cursor 4 toggles BLE RX.

## Boot Sequence (`main.cpp`)

```
1. Display::Init()                     — I2C init, OLED config
2. Display::ShowFWVersion()            — framebuffer-based, FONT_5X7, 2s delay
3. FileSystem::InitSD()                — SD card (silent fail)
4. SoundProcessorManager::StartSoundProcessor()  — audio init (creates audio task + Favorites data model)
5. UIMenu::Init()                      — creates pages, sets navState=ROOT, redrawNeeded=true
6. xTaskCreate(UIMenu::TaskFunction, 4096, idle+3, core 0)
7. UserInput::EnableISR()              — GPIO ISRs (after audio init, avoid MCLK spinlock)
```

The UIMenu task starts AFTER `StartSoundProcessor()` completes. This ensures the audio pipeline is ready when pages query sound processor state.

## Adding a New Page

1. Create `main/menupages/UIMenuPageFoo.hpp` and `.cpp` subclassing `UIMenuPage`
2. Add enum value to `UIMenu::Panel` in `UIMenu.hpp`
3. `new` it in `UIMenu::Init()`
4. Implement `onBack()` for sub-page navigation
5. Use `btnId=2, longPress=false` (OK/short) as the primary select/enter action — this is triggered by physical SW1 short press in the actual mapping

## Adding a Sub-Page (like SYSTEM)

For pages accessed from a parent page's menu (not a panel tab):

1. Create `main/menupages/UIMenuPageFoo.hpp` and `.cpp` subclassing `UIMenuPage`
2. Forward events from the parent page's `onEncoder`/`onButton`/`onBack`/`doRedraw` when the parent is in the matching sub-page state
3. **Do NOT add to `UIMenu::Panel`** — it's not a panel tab
4. **Lazy-allocate**: `new` on entry (user action), not during boot `init()`
5. `deinit()` on exit from the sub-page, `delete` in parent's `deinit()`

## Common Pitfalls

- **Two buttons on one ADC pin**: SW1 = OK (10k to GND), SW2 = BACK (4.7k to GND), shared 10k pull-up to 3.3V. ADC reads distinct voltage levels for each. **Must not use GPIO0** (I2S MCLK). The ADC button approach eliminates single-button timing races (stale events on mode transitions, double-click delay). See `doc/ui-input.md` for circuit and thresholds.
- **Encoder pin swap**: On this build, ENC_A = GPIO5, ENC_B = GPIO23 (swapped relative to upstream). The PCNT glitch filter (val=200, ~2.5µs) is enabled for mechanical bounce rejection.
- **I2C contention**: All display writes from a single task (UIMenu). No mutex needed.
- **GPIO5 conflict (encoder)**: GPIO5 is encoder signal A (PCNT). The old `Favorites::ui_task` used GPIO5 as a button — DISABLED in `SPManager.cpp`. No other GPIO5 usage.
- **Button ISR timing**: `UserInput::EnableISR()` must be called after `Codec::InitCodec()` to avoid ISR firing inside I2S MCLK spinlock on ESP32 Rev3. GPIO0 (BOOT button) is NOT usable — it's the I2S MCLK output. With ADC polling, ISRs are not needed — `EnableISR()` is a no-op.
- **SPModel assert on boot**: Same as upstream — storage partition must have `spm-config.jsn`.
- **Boot-time `new` allocation freeze**: Same as upstream.
- **Config persistence overwrite**: Same as upstream.
