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

            enum Panel : uint8_t {
                PANEL_MIX = 0, PANEL_TAPE = 1, PANEL_HOME = 2, PANEL_MOD = 3, PANEL_PARAMS = 4
#if CONFIG_BT_ENABLED
                , PANEL_MIDI = 5
#endif
            };

        private:
            enum NavState : uint8_t { ROOT, PANEL_IN };

#if CONFIG_BT_ENABLED
            static constexpr int PANEL_COUNT = 6;
            static UIMenuPage *pages[6];
#else
            static constexpr int PANEL_COUNT = 5;
            static UIMenuPage *pages[5];
#endif

            static Panel currentPanel;
            static NavState navState;
            static bool redrawNeeded;
            static int panelBarTimer;
            static int screensaverTimer;
            enum DisplayState { AWAKE, DIMMED, ASLEEP };
            static DisplayState displayState;
            static constexpr int SCREENSAVER_DIM_TIMEOUT = 1500;
            static constexpr int SCREENSAVER_SLEEP_TIMEOUT = 3000;

            static void drawPanelBar();
        };
    }
}
