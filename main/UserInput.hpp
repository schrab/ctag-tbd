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
#include <cstdint>

namespace CTAG {
    namespace CTRL {
        struct InputEvent {
            enum Type : uint8_t {
                ENC_DELTA = 0,
                BTN1_SHORT = 1,
                BTN1_LONG = 2,
                BTN2_SHORT = 3,
                BTN2_LONG = 4,
                BTN1_DOUBLE = 5,
            };
            Type type;
            int16_t delta; // only meaningful for ENC_DELTA
        };

        class UserInput {
        public:
            static void Init();
            static void EnableISR();
            static bool GetEvent(InputEvent& ev, uint32_t timeoutMs = 10);
        };
    }
}
