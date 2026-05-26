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

using namespace CTAG::DRIVERS;

namespace CTAG {
    namespace CTRL {
        void UIMenuPageMix::init() {}

        void UIMenuPageMix::deinit() {}

        void UIMenuPageMix::doRedraw() {
            Display::Clear();
            Display::DrawString(0, 0, "MIX", Display::FONT_5X7);
            Display::DrawVUMeter(0, 16, 60, 8, 0.0f);
            Display::DrawVUMeter(0, 28, 60, 8, 0.0f);
            Display::DrawString(0, 40, "VU meters", Display::FONT_5X7);
            Display::Flush();
        }
    }
}
