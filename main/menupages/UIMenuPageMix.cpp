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

#include "UIMenuPageMix.hpp"
#include "Display.hpp"
#include <cstdio>

using namespace CTAG::DRIVERS;

namespace {
    constexpr int numFonts = 4;
    constexpr Display::Font fonts[numFonts] = {
        Display::FONT_DIGI_SLIM_3X6,
        Display::FONT_DIGI_ONE_5X6,
        Display::FONT_ANALOG_ONE_3X5,
        Display::FONT_NORNS_6X7,
    };
    constexpr const char* fontNames[numFonts] = {
        "digi-slim 3x6",
        "digi-one 5x6",
        "analog-one 3x5",
        "norns 6x7",
    };
}

namespace CTAG {
    namespace CTRL {
        void UIMenuPageMix::init() {
            fontIndex = 0;
            pageOffset = 0;
        }

        void UIMenuPageMix::deinit() {}

        void UIMenuPageMix::doRedraw() {
            Display::Clear();

            auto f = fonts[fontIndex];
            bool isNorns = f == Display::FONT_NORNS_6X7;

            int maxCode = isNorns ? 0xFF : 126;
            constexpr int pageSize = 64;
            int nPages = ((maxCode - 0x20 + 1) + pageSize - 1) / pageSize;
            if (pageOffset >= nPages) pageOffset = nPages - 1;

            int minCode = 0x20 + pageOffset * pageSize;
            maxCode = minCode + pageSize - 1;
            if (maxCode > (isNorns ? 0xFF : 126)) maxCode = (isNorns ? 0xFF : 126);
            if (minCode > maxCode) minCode = maxCode;

            int fontH = f == Display::FONT_DIGI_SLIM_3X6 ? 6
                      : f == Display::FONT_DIGI_ONE_5X6 ? 6
                      : f == Display::FONT_ANALOG_ONE_3X5 ? 5
                      : 7;
            int gap = isNorns ? 1 : 2;
            int rowH = fontH + gap;

            int maxAdv = Display::FontAdvance(f);
            int x = 0, y = 0;

            // Header: font name + page info
            char hdr[24];
            if (nPages > 1)
                snprintf(hdr, sizeof(hdr), "%s [%d/%d]", fontNames[fontIndex], pageOffset + 1, nPages);
            else
                snprintf(hdr, sizeof(hdr), "%s", fontNames[fontIndex]);
            Display::DrawString(0, 0, hdr, Display::FONT_5X7);
            y = 10;

            for (int code = minCode; code <= maxCode; code++) {
                char buf[2] = {(char)code, 0};
                Display::DrawString(x, y, buf, f);
                x += Display::FontAdvance(f, (unsigned char)code);
                if (x + maxAdv > 128) {
                    x = 0;
                    y += rowH;
                }
            }

            Display::Flush();
        }

        void UIMenuPageMix::onButton(int btnId, bool longPress) {
            if (btnId == 2 && !longPress) {
                fontIndex = (fontIndex + 1) % numFonts;
                pageOffset = 0;
                doRedraw();
            }
        }

        void UIMenuPageMix::onEncoder(int delta) {
            auto f = fonts[fontIndex];
            bool isNorns = f == Display::FONT_NORNS_6X7;
            int maxCode = isNorns ? 0xFF : 126;
            constexpr int pageSize = 64;
            int nPages = ((maxCode - 0x20 + 1) + pageSize - 1) / pageSize;
            int oldOffset = pageOffset;
            pageOffset += delta;
            if (pageOffset < 0) pageOffset = 0;
            if (pageOffset >= nPages) pageOffset = nPages - 1;
            if (pageOffset != oldOffset) doRedraw();
        }
    }
}
