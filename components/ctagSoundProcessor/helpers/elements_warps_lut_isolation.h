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

// LUT-macro isolation for the Elements TUs (GCC >= 15 / -Werror).
//
// The Elements Warps stage makes elements/dsp/part.h pull
// warps/dsp/modulator.h -> warps/resources.h into the same TU that already
// has elements/resources.h. Both resources.h files reuse the same LUT_*
// macro names with DIFFERENT values:
//     LUT_MIDI_TO_F_HIGH   4 (warps)  vs 13 (elements)
//     LUT_MIDI_TO_F_LOW    5 (warps)  vs 15 (elements)
// GCC <= 14 only warned about the redefinition; GCC >= 15 errors under
// -Werror. Expanding warps/resources.h FIRST (its include guard then makes
// part.h's later include a no-op) and dropping the two colliding names here
// lets elements/resources.h redefine them cleanly afterwards, so every
// module keeps its OWN macro values — the same technique as the generated
// plugin aggregator (components/ctagSoundProcessor/CMakeLists.txt).
//
// Force-included for the elements TUs via components/mutable/CMakeLists.txt
// and included directly by ctagSoundProcessorElements.hpp (the wrapper TU
// does not go through that CMake target).

#include "warps/resources.h"

#undef LUT_MIDI_TO_F_HIGH
#undef LUT_MIDI_TO_F_LOW
