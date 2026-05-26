# UIMenu / Page Architecture

## Key Files
| File | Role |
|------|------|
| `main/UIMenu.hpp` / `.cpp` | Menu controller — owns pages and event loop task |
| `main/UIMenuPage.hpp` | Abstract base class for all pages |
| `main/menupages/UIMenuPage*.hpp/.cpp` | Concrete page implementations |
| `main/UserInput.hpp` / `.cpp` | Hardware input (encoder + 2 buttons) |
| `main/Display.hpp` / `.cpp` | OLED framebuffer (128x64, SSD1306) |

## Abstract Page Interface
```cpp
class UIMenuPage {
public:
    virtual void init() = 0;
    virtual void deinit() = 0;
    virtual void doRedraw() = 0;
    virtual void onEncoder(int delta) {}     // delta = ±1
    virtual void onButton(int btnId, bool longPress) {}  // btnId=2 only
};
```

## Page Registration
Pages are hardcoded in a fixed array:
```cpp
enum Panel : uint8_t { PANEL_MIX=0, PANEL_TAPE=1, PANEL_HOME=2, PANEL_PARAMS=3 };
static UIMenuPage *pages[4];
```
To add a page: add enum value, expand array, `new` it in `Init()`.

## Event Flow
```
UserInput task (Core 0)
  → polls encoder (PCNT, 1kHz) + buttons (20ms debounce, 500ms long-press)
  → pushes events to FreeRTOS queue (64 slots)

UIMenu task (Core 0, 20ms loop)
  → UserInput::GetEvent(ev, 20)
  → BTN1_SHORT → toggle inMenu
  → BTN1_LONG  → set alt modifier
  → BTN2_SHORT → pages[current]->onButton(2, false)
  → BTN2_LONG  → pages[current]->onButton(2, true)
  → ENC_DELTA > 1  → switch panel (navigate)
  → ENC_DELTA <= 1 → pages[current]->onEncoder(delta)
```

## Display API
| Method | Purpose |
|--------|---------|
| `Clear()` | Clear framebuffer |
| `Flush()` | Push to OLED (dirty-page optimized) |
| `DrawString(x,y,str,font)` | Text (FONT_8X8 or FONT_5X7) |
| `DrawStringRight(x,y,str,font)` | Right-aligned text |
| `InvertRect(x,y,w,h)` | Invert region (highlight) |
| `DrawScrollbar(x,y,h,total,cursor)` | Scrollbar |
| `DrawVUMeter(x,y,w,h,level)` | VU bar (0.0-1.0) |
| `DrawPixel/HLine/VLine/Rect` | Primitives |

Screen: 128x64px. FONT_8X8 = 8 rows × 16 chars. FONT_5X7 = ~9 rows.

## Existing Pages
| Page | File | Purpose |
|------|------|---------|
| HOME | `UIMenuPageHome.cpp` | Plugin browser, load/select |
| MIX | `UIMenuPageMix.cpp` | Volume/levels (stub) |
| TAPE | `UIMenuPageTape.cpp` | Sample recording (stub) |
| PARAMS | `UIMenuPageParams.cpp` | Parameter editing, CV mapping |
