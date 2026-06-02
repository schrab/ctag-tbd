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

#include <string>
#include <cstring>

#include "Display.hpp"
#include "font5x7.h"
#include "font8x8_basic.h"
#include "version.hpp"
#include "esp_log.h"

#if CONFIG_TBD_PLATFORM_MK2
    #define SCL_GPIO 22
    #define SDA_GPIO 21
#elif CONFIG_TBD_PLATFORM_BBA
    #define SCL_GPIO 22
    #define SDA_GPIO 21
#else
    #define SCL_GPIO 22
    #define SDA_GPIO 21
#endif

using namespace CTAG::DRIVERS;

SSD1306_t Display::I2CDisplay;
std::vector<std::string> Display::userString_v;
int Display::currentUserStringRow {0};
int Display::userContrast = 0xFF;
uint8_t Display::fb[1024];
bool Display::dirtyPages[8] {false};

void Display::Init() {
    i2c_master_init(&I2CDisplay, SDA_GPIO, SCL_GPIO, -1);
    ssd1306_init(&I2CDisplay, 128, 64);
    ssd1306_clear_screen(&I2CDisplay, false);
    ssd1306_contrast(&I2CDisplay, 0xff);
}

void Display::Sleep() {
    ssd1306_display_off(&I2CDisplay);
}

void Display::Wake() {
    ssd1306_display_on(&I2CDisplay);
    ssd1306_contrast(&I2CDisplay, userContrast);
}

void Display::Contrast(int val) {
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    userContrast = val;
    ssd1306_contrast(&I2CDisplay, val);
}

void Display::Clear() {
    std::memset(fb, 0, sizeof(fb));
    for (int i = 0; i < 8; i++) dirtyPages[i] = true;
}

void Display::ShowFavorite(const int &id, const std::string &name) {
    std::string s {"Active Favorite:"};
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 0, s.c_str(), s.length(), false);
    s = std::string("");
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 1, s.c_str(), s.length(), false);
    s = std::string(std::to_string(id) + ": " + name);
    s = s.substr(0, 16);
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 2, s.c_str(), s.length(), false);
    s = std::string("");
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 3, s.c_str(), s.length(), false);
}

void Display::ShowFWVersion() {
    Clear();
    DrawString(0, 10, "TBD fw:", FONT_5X7);
    DrawString(0, 20, TBD_FW_VERSION.c_str(), FONT_5X7);
    DrawString(0, 34, "TBD hw:", FONT_5X7);
    DrawString(0, 44, TBD_HW_VERSION.c_str(), FONT_5X7);
    Flush();
}

void Display::ShowUserString(std::string const &s) {
    ssd1306_clear_screen(&I2CDisplay, false);
    ssd1306_display_text(&I2CDisplay, 0, s.c_str(), s.length() > 16 ? 16 : s.length(), false);
}

void Display::ShowUserString(std::vector<std::string> const &sv) {
    ssd1306_clear_screen(&I2CDisplay, false);
    for(int i=0;i<sv.size();i++){
        ssd1306_display_text(&I2CDisplay, i, sv[i].c_str(), sv[i].length() > 16 ? 16 : sv[i].length(), false);
    }
}

void Display::PrepareDisplayFavoriteUString(int const &id, std::string const &name, std::string const &us) {
    // create slices
    std::string s {us}, title{"#" + std::to_string(id) + ": " + name};
    title.append(16-title.length(), ' ');
    userString_v.clear();
    userString_v.push_back(title);
    userString_v.push_back("                ");
    while(s.length() > 0){
        if(s.length() < 16) s.append(16-s.length(), ' ');
        userString_v.push_back(s.substr(0, 16));
        s = s.substr(16, s.length() - 16);
    }
    userString_v.shrink_to_fit();
    if(userString_v.size() > 4){
        userString_v.push_back("                ");
    }else{
        while(userString_v.size() < 4){
            userString_v.push_back("                ");
        }
    }
    userString_v.shrink_to_fit();
    currentUserStringRow = 0;
    ssd1306_clear_screen(&I2CDisplay, false);
    ssd1306_software_scroll(&I2CDisplay, I2CDisplay._pages - 1, 0);
    UpdateFavoriteUStringScroll();
    UpdateFavoriteUStringScroll();
    UpdateFavoriteUStringScroll();
    UpdateFavoriteUStringScroll();
}

void Display::Confirm(const int &id) {
    std::string s {"Load Fav: " + std::to_string(id)};
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 0, s.c_str(), s.length(), false);
    s = std::string("Confirm?");
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 1, s.c_str(), s.length(), false);
    s = std::string("y -> Long Press");
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 2, s.c_str(), s.length(), false);
    s = std::string("n -> Short Press");
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 3, s.c_str(), s.length(), false);
}

void Display::UserMode() {
    std::string s {"User Mode"};
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 0, s.c_str(), s.length(), false);
    s = std::string("");
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 1, s.c_str(), s.length(), false);
    s = std::string("No Fav Active!");
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 2, s.c_str(), s.length(), false);
    s = std::string("");
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 3, s.c_str(), s.length(), false);
}

void Display::LoadFavorite(int const &id, const std::string &name) {
    std::string s {"Load Favorite:"};
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 0, s.c_str(), s.length(), false);
    s = std::string(std::to_string(id) + ": " + name);
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 1, s.c_str(), s.length(), false);
    s = std::string("");
    s = s.substr(0, 16);
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 2, s.c_str(), s.length(), false);
    s = std::string("-> Long Press");
    s.append(16-s.length(), ' ');
    ssd1306_display_text(&I2CDisplay, 3, s.c_str(), s.length(), false);
}

void Display::UpdateFavoriteUStringScroll() {
    if(userString_v.size() == 0) return;
    if(userString_v.size() <= 4 && currentUserStringRow == userString_v.size()) return;
    if(currentUserStringRow >= userString_v.size()) currentUserStringRow = 0;
    ssd1306_scroll_text(&I2CDisplay, userString_v[currentUserStringRow].c_str(), 16, false);
    currentUserStringRow++;
}

// ---- framebuffer drawing primitives ----

void Display::MarkDirty(int page) {
    if (page >= 0 && page < 8) dirtyPages[page] = true;
}

void Display::Flush() {
    for (int p = 0; p < 8; p++) {
        if (!dirtyPages[p]) continue;
        ssd1306_display_image(&I2CDisplay, p, 0, &fb[p * 128], 128);
        dirtyPages[p] = false;
    }
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

void Display::DrawString(int x, int y, const char *str, Font font) {
    if (!str) return;
    if (font == FONT_8X8) {
        while (*str) {
            if (*str < 32 || *str > 127) { str++; continue; }
            int c = *str - 32;
            const uint8_t *glyph = font8x8_basic_tr[c];
            for (int col = 0; col < 8; col++) {
                uint8_t byte = glyph[col];
                for (int row = 0; row < 8; row++) {
                    DrawPixel(x + col, y + row, (byte >> row) & 1);
                }
            }
            str++;
        }
    } else {
        while (*str) {
            if ((unsigned char)*str > 127) { str++; continue; }
            const uint8_t *glyph = font5x7[(uint8_t)*str];
            for (int col = 0; col < 5; col++) {
                uint8_t byte = glyph[col];
                for (int row = 0; row < 7; row++) {
                    DrawPixel(x + col, y + row, (byte >> row) & 1);
                }
            }
            str++;
        }
    }
}

void Display::DrawStringRight(int x, int y, const char *str, Font font) {
    if (!str) return;
    int len = strlen(str);
    int strWidth = (font == FONT_8X8) ? len * 8 : len * 6;
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
