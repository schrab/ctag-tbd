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

#include "UIMenuPage.hpp"
#include "Display.hpp"
#include "UserInput.hpp"
#include <array>

namespace CTAG {
    namespace CTRL {
        class UIMenu final {
        public:
            static void Init();
            static void TaskFunction(void *);

        private:
            enum Panel : uint8_t { PANEL_MIX = 0, PANEL_TAPE = 1, PANEL_HOME = 2, PANEL_PARAMS = 3 };
            static constexpr const char *panelNames[4] = {"MIX", "TAPE", "HOME", "PARAMS"};

            static Panel currentPanel;
            static UIMenuPage *pages[4];
            static bool inMenu;
            static bool alt;
            static int panelBarTimer; // counts down, hides bar at 0

            static void drawPanelBar();
        };
    }
}
