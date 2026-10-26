/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2020 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

The CTAG TBD hardware design is released under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0).
Details here: https://creativecommons.org/licenses/by-nc-sa/4.0/

CTAG TBD is provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.
***************/

// C3 panel display: framebuffer primitives are the A1S Display.cpp code
// (page-major framebuffer, dirty-page Flush, per-glyph font advances); the
// backend is a direct SSD1309 SPI driver replacing the A1S I²C ssd1306
// component.

#include <string>
#include <cstring>
#include <cstdio>

#include "Display.hpp"
#include "font5x7.h"
#include "font8x8_basic.h"
#include "fonts/digi_slim_3x6.h"
#include "fonts/digi_one_5x6.h"
#include "fonts/norns_6x7.h"
#include "fonts/norns_ext_6x7.h"
#include "version.hpp"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using namespace CTAG::DRIVERS;

// Panel wiring (SDD §2.1). RST is tied to 3V3 via RC on the carrier.
#define OLED_SCK_GPIO 6
#define OLED_MOSI_GPIO 7
#define OLED_CS_GPIO 2
#define OLED_DC_GPIO 3

// 10 MHz start (SDD §2.1: 10–20 MHz); raise to 20 after the wiring is proven.
#define OLED_SPI_CLOCK_HZ (10 * 1000 * 1000)

// Some 128x64 SSD1309 modules sit on a 132-column controller with a 2-column
// offset. If the image is shifted 2px left/right at bring-up, set this to 2.
#define OLED_COLUMN_OFFSET 0

// COM scan + segment remap match the A1S panel orientation (0xC8 scan).
static const char *TAG = "Display";

static spi_device_handle_t spiHandle = nullptr;

uint8_t Display::fb[1024];
bool Display::dirtyPages[8] {false};
int Display::userContrast = 0xFF;

bool Display::sendCommand(uint8_t cmd) {
    gpio_set_level((gpio_num_t)OLED_DC_GPIO, 0);
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &cmd;
    return spi_device_polling_transmit(spiHandle, &t) == ESP_OK;
}

bool Display::sendData(const uint8_t *data, size_t len) {
    gpio_set_level((gpio_num_t)OLED_DC_GPIO, 1);
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    esp_err_t err = spi_device_polling_transmit(spiHandle, &t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi data tx: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void Display::Init() {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = OLED_MOSI_GPIO;
    bus.sclk_io_num = OLED_SCK_GPIO;
    bus.miso_io_num = -1;
    bus.quadwp_io_num = -1;
    bus.max_transfer_sz = 128;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = OLED_SPI_CLOCK_HZ;
    dev.mode = 0;
    dev.spics_io_num = OLED_CS_GPIO;
    dev.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &spiHandle));

    gpio_config_t dc = {};
    dc.pin_bit_mask = 1ULL << OLED_DC_GPIO;
    dc.mode = GPIO_MODE_OUTPUT;
    gpio_config(&dc);

    // SSD1309 init, page addressing mode (matches the dirty-page Flush).
    sendCommand(0xAE); // display off
    sendCommand(0xD5); sendCommand(0x80); // clock divide
    sendCommand(0xA8); sendCommand(0x3F); // multiplex 1/64
    sendCommand(0xD3); sendCommand(0x00); // display offset
    sendCommand(0x40);                    // start line 0
    sendCommand(0x8D); sendCommand(0x14); // charge pump on
    sendCommand(0x20); sendCommand(0x02); // page addressing mode
    sendCommand(0xA1);                    // segment remap
    sendCommand(0xC8);                    // COM scan direction
    sendCommand(0xDA); sendCommand(0x12); // COM pins
    sendCommand(0x81); sendCommand((uint8_t)userContrast);
    sendCommand(0xD9); sendCommand(0xF1); // precharge
    sendCommand(0xDB); sendCommand(0x40); // VCOMH
    sendCommand(0xA4);                    // display from RAM
    sendCommand(0xA6);                    // normal (not inverted)
    sendCommand(0xAF);                    // display on

    std::memset(fb, 0, sizeof(fb));
    for (int i = 0; i < 8; i++) dirtyPages[i] = true;
    Flush();
}

void Display::Sleep() {
    sendCommand(0xAE);
}

void Display::Wake() {
    sendCommand(0xAF);
    sendCommand(0x81);
    sendCommand((uint8_t)userContrast);
}

void Display::Contrast(int val) {
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    userContrast = val;
    sendCommand(0x81);
    sendCommand((uint8_t)userContrast);
}

void Display::Clear() {
    std::memset(fb, 0, sizeof(fb));
    for (int i = 0; i < 8; i++) dirtyPages[i] = true;
}

void Display::ShowFWVersion() {
    Clear();
    DrawString(0, 10, "TBD fw:", FONT_5X7);
    DrawString(0, 20, TBD_FW_VERSION.c_str(), FONT_5X7);
    DrawString(0, 34, "TBD hw:", FONT_5X7);
    DrawString(0, 44, TBD_HW_VERSION.c_str(), FONT_5X7);
    Flush();
}

void Display::MarkDirty(int page) {
    if (page >= 0 && page < 8) dirtyPages[page] = true;
}

void Display::Flush() {
    for (int p = 0; p < 8; p++) {
        if (!dirtyPages[p]) continue;
        sendCommand(0xB0 | p);                                        // page address
        sendCommand(0x00 | ((OLED_COLUMN_OFFSET) & 0x0F));            // column low
        sendCommand(0x10 | (((OLED_COLUMN_OFFSET) >> 4) & 0x0F));     // column high
        sendData(&fb[p * 128], 128);
        dirtyPages[p] = false;
    }
}

void Display::CopyFramebuffer(uint8_t *dst) {
    if (dst == nullptr) return;
    memcpy(dst, fb, FRAMEBUFFER_SIZE);
}

void Display::DrawPixel(int x, int y, bool on) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    int page = y >> 3;
    int bit = y & 7;
    int idx = page * 128 + x;
    if (on)
        fb[idx] |= (1 << bit);
    else
        fb[idx] &= ~(1 << bit);
    MarkDirty(page);
}

void Display::DrawHLine(int x, int y, int w, bool on) {
    for (int i = 0; i < w; i++) DrawPixel(x + i, y, on);
}

void Display::DrawVLine(int x, int y, int h, bool on) {
    for (int i = 0; i < h; i++) DrawPixel(x, y + i, on);
}

void Display::DrawRect(int x, int y, int w, int h, bool fill, bool on) {
    if (fill) {
        for (int row = 0; row < h; row++)
            DrawHLine(x, y + row, w, on);
    } else {
        DrawHLine(x, y, w, on);
        DrawHLine(x, y + h - 1, w, on);
        DrawVLine(x, y, h, on);
        DrawVLine(x + w - 1, y, h, on);
    }
}

void Display::InvertRect(int x, int y, int w, int h) {
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int px = x + col, py = y + row;
            if (px < 0 || px >= 128 || py < 0 || py >= 64) continue;
            int page = py >> 3;
            int bit = py & 7;
            int idx = page * 128 + px;
            fb[idx] ^= (1 << bit);
            MarkDirty(page);
        }
    }
}

static void drawGlyph(int &x, int y, const uint8_t *glyph, int gw, int gh, int advance,
                void (*pixel)(int, int, bool)) {
    for (int col = 0; col < gw; col++) {
        uint8_t byte = glyph[col];
        for (int row = 0; row < gh; row++) {
            pixel(x + col, y + row, (byte >> row) & 1);
        }
    }
    x += advance;
}

// Decode a single UTF-8 codepoint from str, advance pointer past it
static uint16_t utf8_decode(const char *&s) {
    uint8_t b0 = (uint8_t)*s;
    if (b0 < 0x80) {
        s++;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0) {
        // 2-byte sequence
        if ((s[1] & 0xC0) != 0x80) { s++; return b0; }
        uint16_t cp = ((uint16_t)(b0 & 0x1F) << 6) | (s[1] & 0x3F);
        s += 2;
        return cp;
    }
    if ((b0 & 0xF0) == 0xE0) {
        // 3-byte sequence
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) { s++; return b0; }
        uint16_t cp = ((uint16_t)(b0 & 0x0F) << 12) | ((uint16_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        s += 3;
        return cp;
    }
    // 4-byte or invalid lead byte — skip 1 byte
    s++;
    return b0;
}

void Display::DrawString(int x, int y, const char *str, Font font) {
    if (!str) return;
    while (*str) {
        const char *next = str;
        uint16_t c = utf8_decode(next);
        str = next;
        switch (font) {
        case FONT_8X8:
            if (c < 32 || c > 127) continue;
            drawGlyph(x, y, font8x8_basic_tr[c - 32], 8, 8, 8, DrawPixel);
            break;
        case FONT_5X7:
            if (c > 127) continue;
            drawGlyph(x, y, font5x7[c], 5, 7, 6, DrawPixel);
            break;
        case FONT_DIGI_SLIM_3X6:
            if (c >= FONT_DIGI_SLIM_N_CHARS) continue;
            drawGlyph(x, y, font_digi_slim[c], FONT_DIGI_SLIM_W, FONT_DIGI_SLIM_H,
                      font_digi_slim_advances[c], DrawPixel);
            break;
        case FONT_DIGI_ONE_5X6:
            if (c >= FONT_DIGI_ONE_N_CHARS) continue;
            drawGlyph(x, y, font_digi_one[c], FONT_DIGI_ONE_W, FONT_DIGI_ONE_H,
                      font_digi_one_advances[c], DrawPixel);
            break;
        case FONT_NORNS_6X7:
            if (c < FONT_NORNS_N_CHARS) {
                drawGlyph(x, y, font_norns[c], FONT_NORNS_W, FONT_NORNS_H,
                          font_norns_advances[c], DrawPixel);
            } else {
                int idx = FontNornsExtLookup(c);
                if (idx < FONT_NORNS_EXT_N_CHARS) {
                    drawGlyph(x, y, font_norns_ext_data[idx], FONT_NORNS_EXT_W,
                              FONT_NORNS_EXT_H, font_norns_ext_advances[idx], DrawPixel);
                }
            }
            break;
        }
    }
}

int Display::FontAdvance(Font f) {
    switch (f) {
    case FONT_8X8: return 8;
    case FONT_5X7: return 6;
    case FONT_DIGI_SLIM_3X6: return FONT_DIGI_SLIM_ADVANCE;
    case FONT_DIGI_ONE_5X6: return FONT_DIGI_ONE_ADVANCE;
    case FONT_NORNS_6X7: return FONT_NORNS_ADVANCE;
    }
    return 6;
}

int Display::FontAdvance(Font f, unsigned char c) {
    switch (f) {
    case FONT_DIGI_SLIM_3X6:
        if (c < FONT_DIGI_SLIM_N_CHARS) return font_digi_slim_advances[c];
        return FONT_DIGI_SLIM_ADVANCE;
    case FONT_DIGI_ONE_5X6:
        if (c < FONT_DIGI_ONE_N_CHARS) return font_digi_one_advances[c];
        return FONT_DIGI_ONE_ADVANCE;
    case FONT_NORNS_6X7:
        if (c < FONT_NORNS_N_CHARS) return font_norns_advances[c];
        if (c >= 0x80) {
            int idx = FontNornsExtLookup(c);
            if (idx < FONT_NORNS_EXT_N_CHARS)
                return font_norns_ext_advances[idx];
        }
        return FONT_NORNS_ADVANCE;
    default:
        return FontAdvance(f);
    }
}

void Display::DrawStringRight(int x, int y, const char *str, Font font) {
    if (!str) return;
    int len = strlen(str);
    int strWidth = len * FontAdvance(font);
    DrawString(x - strWidth, y, str, font);
}

void Display::DrawVUMeter(int x, int y, int w, int h, float level) {
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    int fillW = (int)(level * w);
    if (fillW > w) fillW = w;
    DrawRect(x, y, w, h, false, true);
    if (fillW > 0) {
        DrawRect(x + 1, y + 1, fillW, h - 2, true, true);
    }
}

void Display::DrawVUMeterV(int x, int y, int w, int h, float level) {
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    DrawRect(x, y, w, h, false, true);
    int fillH = (int)(level * (h - 2));
    if (fillH > h - 2) fillH = h - 2;
    if (fillH > 0) {
        // fill upwards from the bottom edge
        DrawRect(x + 1, y + h - 1 - fillH, w - 2, fillH, true, true);
    }
}

void Display::DrawScrollbar(int x, int y, int h, int totalItems, int cursorPos) {
    if (totalItems <= 1) return;
    int visibleItems = h / LINE_H;
    if (visibleItems >= totalItems) return;
    int thumbH = h * visibleItems / totalItems;
    if (thumbH < 4) thumbH = 4;
    int thumbY = y + (h - thumbH) * cursorPos / (totalItems - visibleItems);
    DrawVLine(x, y, h);                           // 1px track
    DrawRect(x - 1, thumbY, 3, thumbH, true, true); // 3px thumb overlaps right edge
}
