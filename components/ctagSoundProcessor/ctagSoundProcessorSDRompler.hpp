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

#include <atomic>
#include <map>
#include <functional>
#include "ctagSoundProcessor.hpp"

using namespace std;

namespace CTAG {
    namespace SP {
        class ctagSoundProcessorSDRompler : public ctagSoundProcessor {
        public:
            virtual void Process(const ProcessData &) override;
            virtual ~ctagSoundProcessorSDRompler();
            virtual void Init(std::size_t blockSize, void *blockPtr) override;

        private:
            virtual void knowYourself() override;

            atomic<int32_t> gain, cv_gain;
            atomic<int32_t> pitch, cv_pitch;
            atomic<int32_t> startOffset, cv_startOffset;
            atomic<int32_t> loopMode;

            uint32_t playPos = 0;
            float frac = 0.0f;
        };
    }
}
