# Display Rendering Analysis

## Rendering Pipeline

```
Menu Pages (UIMenuPage subclasses)
  → DrawString(), DrawPixel(), InvertRect(), etc. → Display::fb[1024]
  → Display::Flush() → ssd1306_display_image() → i2c_master_transmit() → SSD1309
```

**Framebuffer:** 1024 bytes = 128 columns × 8 pages. Page-major, column-minor.
`fb[page*128 + x]` → each byte is a vertical 8-pixel column (SSD1306/SSD1309 GDDRAM page format).

**Dirty tracking:** `dirtyPages[8]` — one flag per 8-row page.

## Fonts

### FONT_5X7 (`font5x7.h`)
- **Storage:** Column-major, 5 bytes per glyph, each byte = one 7-bit column (MSB = top pixel).
- **Advance:** 6 px (5 glyph + 1 spacing).
- **Capacity:** ~9 rows × ~21 chars on 128×64.
- **DrawString reading:** `byte >> (7 - row)` reads bits 7→1 (top→bottom).

### FONT_8X8 (`font8x8_basic.h` — 90° transposed)
- **Storage:** Column-major, 8 bytes per glyph, each byte = one 8-bit column.
- **Advance:** 8 px.
- **Capacity:** 8 rows × 16 chars.
- **DrawString reading:** `byte >> row` reads bits 0→7 (bottom→top — opposite to FONT_5X7 convention).

## Known Issues

### 1. Flickering
**Cause:** `DrawString()`, `DrawVUMeter()`, and `DrawScrollbar()` each call `Display::Flush()` at the end. During redraw, every string drawn triggers an I2C transmission of dirty pages. Clear() also redundantly calls `ssd1306_clear_screen()` (direct I2C) in addition to `memset(fb)`.

**Fix:** Remove Flush() from DrawString/DrawVUMeter/DrawScrollbar. Remove direct I2C write from Clear(). Callers already flush at end of each redraw.

### 2. Wrong font in UIMenuPageParams SELECT
**Cause:** `redrawSelect()` uses FONT_8X8 with 16px row height instead of FONT_5X7.

**Fix:** Use FONT_5X7 with 9px row spacing.

### 3. Old Favorites UI task overwrites display
**Cause:** `SPManager::StartSoundProcessor()` creates `Favorites::ui_task` — a separate FreeRTOS task that:
- Polls GPIO5 (BBA) as button input
- GPIO5 is also used by the encoder (PCNT unit 0, GPIO5 pin B)
- Encoder rotation triggers false button events in the Favorites state machine
- State machine calls `LoadFavorite()`, `UserMode()` etc. via `ssd1306_display_text()` — bypassing the framebuffer
- This causes the old selection menu to appear

**Fix:** Don't create the Favorites UI task when UIMenu system is active. The GPIO conflict and dual-task display writes are fundamentally incompatible with the new menu system.

### 4. No display synchronization
Both `UIMenu::TaskFunction` and `Favorites::ui_task` write to the same OLED via I2C without any mutex.

### 5. GPIO5 conflict (BBA)
GPIO5 is used by both `Favorites.cpp` (PIN_PUSH_BTN) and `encoder.cpp` (PCNT unit 0 signal B).

## Display Config

| Param | Value |
|-------|-------|
| Display | SSD1309 128x64 |
| I2C addr | 0x3C (7-bit) |
| SCL/SDA | 22/21 |
| Clock | 1 MHz |
| Column remap | 0xA1 (SEGMENT_REMAP_1) |
| COM scan | 0xC8 |
| `_flip` | false (hardcoded) |
| Page mode | Page addressing |
