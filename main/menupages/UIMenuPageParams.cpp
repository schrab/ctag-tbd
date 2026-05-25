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

#include "UIMenuPageParams.hpp"
#include "Display.hpp"

using namespace CTAG::DRIVERS;

namespace CTAG {
    namespace CTRL {
        void UIMenuPageParams::init() {}

        void UIMenuPageParams::deinit() {}

        void UIMenuPageParams::doRedraw() {
            Display::Clear();
            Display::DrawString(0, 0, "PARAMS", Display::FONT_8X8);
            Display::DrawString(0, 16, "EDIT", Display::FONT_8X8);
            Display::DrawString(0, 24, "MAP", Display::FONT_8X8);
            Display::DrawString(0, 32, "PSET", Display::FONT_8X8);
            Display::InvertRect(0, 16, 128, 8);
        }
    }
}
