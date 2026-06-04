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
#include "fonts/norns_6x7.h"
#include "fonts/norns_ext_6x7.h"
#include <cstdio>
#include <algorithm>

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
    constexpr int pageSize = 64;

    int nBasePages(bool isNorns) {
        int maxCode = isNorns ? 0x7F : 126;
        return ((maxCode - 0x20 + 1) + pageSize - 1) / pageSize;
    }

    int nExtPages() {
        return (FONT_NORNS_EXT_N_CHARS + pageSize - 1) / pageSize;
    }

    int totalPages(bool isNorns) {
        return nBasePages(isNorns) + (isNorns ? nExtPages() : 0);
    }

    // Encode a uint16_t codepoint as UTF-8 into buf, return byte length
    int utf8_encode(uint16_t cp, char *buf) {
        if (cp < 0x80) {
            buf[0] = cp;
            return 1;
        } else if (cp < 0x800) {
            buf[0] = 0xC0 | (cp >> 6);
            buf[1] = 0x80 | (cp & 0x3F);
            return 2;
        } else {
            buf[0] = 0xE0 | (cp >> 12);
            buf[1] = 0x80 | ((cp >> 6) & 0x3F);
            buf[2] = 0x80 | (cp & 0x3F);
            return 3;
        }
    }
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
            bool norns = f == Display::FONT_NORNS_6X7;
            int nTot = totalPages(norns);
            int nBase = nBasePages(norns);
            if (pageOffset >= nTot) pageOffset = nTot - 1;

            int fontH = f == Display::FONT_DIGI_SLIM_3X6 ? 6
                       : f == Display::FONT_DIGI_ONE_5X6 ? 6
                       : f == Display::FONT_ANALOG_ONE_3X5 ? 5
                       : 7;
            int gap = norns ? 0 : 2;
            int rowH = fontH + gap;
            int maxAdv = Display::FontAdvance(f);
            int x = 0, y = 0;

            // Header: font name + page info
            char hdr[28];
            if (nTot > 1)
                snprintf(hdr, sizeof(hdr), "%s [%d/%d]", fontNames[fontIndex], pageOffset + 1, nTot);
            else
                snprintf(hdr, sizeof(hdr), "%s", fontNames[fontIndex]);
            Display::DrawString(0, 0, hdr, Display::FONT_5X7);
            y = 10;

            if (pageOffset < nBase) {
                // Base page: 0x20-0x7F (or 0x20-126 for non-norns)
                int maxCode = norns ? 0x7F : 126;
                int minCode = 0x20 + pageOffset * pageSize;
                maxCode = std::min(minCode + pageSize - 1, maxCode);
                if (minCode > maxCode) minCode = maxCode;

                for (int code = minCode; code <= maxCode; code++) {
                    if (norns && code >= FONT_NORNS_N_CHARS &&
                        FontNornsExtLookup(code) >= FONT_NORNS_EXT_N_CHARS) {
                        continue;
                    }
                    char buf[2] = {(char)code, 0};
                    int adv = Display::FontAdvance(f, (unsigned char)code);
                    Display::DrawString(x, y, buf, f);
                    x += adv;
                    if (x + maxAdv > 128) {
                        x = 0;
                        y += rowH;
                    }
                }
            } else {
                // Extended norns page: iterate ext table entries
                int extPage = pageOffset - nBase;
                int startIdx = extPage * pageSize;
                int endIdx = std::min(startIdx + pageSize - 1, FONT_NORNS_EXT_N_CHARS - 1);

                for (int i = startIdx; i <= endIdx; i++) {
                    uint16_t cp = font_norns_ext_codepoints[i];
                    char buf[4];
                    utf8_encode(cp, buf);
                    int idx = FontNornsExtLookup(cp);
                    int adv = (idx < FONT_NORNS_EXT_N_CHARS)
                        ? font_norns_ext_advances[idx] : maxAdv;
                    Display::DrawString(x, y, buf, f);
                    x += adv;
                    if (x + maxAdv > 128) {
                        x = 0;
                        y += rowH;
                    }
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
            int nTot = totalPages(f == Display::FONT_NORNS_6X7);
            int oldOffset = pageOffset;
            pageOffset += delta;
            if (pageOffset < 0) pageOffset = 0;
            if (pageOffset >= nTot) pageOffset = nTot - 1;
            if (pageOffset != oldOffset) doRedraw();
        }
    }
}
