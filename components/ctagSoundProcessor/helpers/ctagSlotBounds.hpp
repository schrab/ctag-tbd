/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2024 by Robert Manzke. All rights reserved.

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

// N_CVS / N_TRIGS are supplied by the top-level CMakeLists per platform
// (BBA: N_CVS=100, N_TRIGS=40). Host builds define them here so the
// modulation helpers can be unit-tested without ESP-IDF.
#ifndef N_CVS
#define N_CVS 100
#endif
#ifndef N_TRIGS
#define N_TRIGS 40
#endif

namespace CTAG {
    namespace SP {
        namespace HELPERS {
            // CV and trigger buffers are stack locals of audio_task
            // (SPManager.cpp). An out-of-range slot index writes over live
            // audio-task stack variables on every block, so every setter
            // funnels through here. -1 means "unassigned" and is preserved.
            inline int ClampCVSlot(int slot) {
                if (slot < 0) return -1;
                if (slot >= N_CVS) return -1;
                return slot;
            }

            inline int ClampTrigSlot(int slot) {
                if (slot < 0) return -1;
                if (slot >= N_TRIGS) return -1;
                return slot;
            }
        } // HELPERS
    } // SP
} // CTAG