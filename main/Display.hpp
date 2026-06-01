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

#pragma once

extern "C" {
    #include "ssd1306.h"
}

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

// Layout constants — change these when swapping fonts
static constexpr int FONT_H = 7;
static constexpr int LINE_H = FONT_H + 1;
static constexpr int ITEM_Y0 = 5;
static constexpr int ITEM_Y0_HDR = ITEM_Y0 + FONT_H + 1;
static constexpr int VISIBLE_ITEMS = 7;
static constexpr int VISIBLE_ITEMS_HDR = 6;
static constexpr int SCROLLBAR_X = 126;

#define ROW(n)     (ITEM_Y0 + (n) * LINE_H)
#define ROW_HDR(n) (ITEM_Y0_HDR + (n) * LINE_H)

inline void ClampScroll(int cursor, int &scrollOffset, int total, int visible) {
    if (cursor - scrollOffset < 0) scrollOffset = cursor;
    if (cursor - scrollOffset >= visible) scrollOffset = cursor - (visible - 1);
    if (scrollOffset > total - visible) scrollOffset = total - visible;
    if (scrollOffset < 0) scrollOffset = 0;
}

inline int ClampVisible(int total, int scrollOff, int maxVis) {
    int v = total - scrollOff;
    return v > maxVis ? maxVis : v;
}

namespace CTAG {
    namespace DRIVERS {
        class Display final {
            static const int I2CDisplayAddress;
            static const int I2CDisplayWidth;
            static const int I2CDisplayHeight;
            static const int I2CResetPin;
            static SSD1306_t I2CDisplay;
            static std::vector<std::string> userString_v;
            static int currentUserStringRow;

            // framebuffer: page-major, column-minor
            static uint8_t fb[1024];
            static bool dirtyPages[8];

            static void MarkDirty(int page);
            static int userContrast;

        public:
            static void Flush();

            // Font selection
            enum Font { FONT_8X8, FONT_5X7 };

            static void Sleep();
            static void Wake();
            static void Contrast(int val);

            Display() = delete;
            static void Init();
            static void Clear();
            static void ShowFavorite(int const &id, std::string const &name);
            static void ShowFWVersion();
            static void ShowUserString(std::string const &s);
            static void ShowUserString(std::vector<std::string> const &sv);
            static void PrepareDisplayFavoriteUString(int const &id, std::string const &name, std::string const &us);
            static void UpdateFavoriteUStringScroll();
            static void LoadFavorite(int const &id, std::string const &name);
            static void Confirm(const int &id);
            static void UserMode();

            // Framebuffer drawing primitives
            static void DrawPixel(int x, int y, bool on = true);
            static void DrawHLine(int x, int y, int w, bool on = true);
            static void DrawVLine(int x, int y, int h, bool on = true);
            static void DrawRect(int x, int y, int w, int h, bool fill = false, bool on = true);
            static void InvertRect(int x, int y, int w, int h);
            static void DrawString(int x, int y, const char *str, Font font = FONT_8X8);
            static void DrawStringRight(int x, int y, const char *str, Font font = FONT_8X8);
            static void DrawVUMeter(int x, int y, int w, int h, float level);
            static void DrawScrollbar(int x, int y, int h, int totalItems, int cursorPos);
        };
    }
}
