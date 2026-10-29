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
#include "ctagSoundProcessor.hpp"
#include "plaits/dsp/voice.h"

namespace CTAG {
    namespace SP {
        class ctagSoundProcessorTBDaits : public ctagSoundProcessor {
        public:
            virtual void Process(const ProcessData &) override;

            virtual ~ctagSoundProcessorTBDaits();

           virtual void Init(std::size_t blockSize, void *blockPtr) override;


        private:
            virtual void knowYourself() override;

            // private attributes could go here
            plaits::Voice voice;
            plaits::Patch patch = {};
            char *shared_buffer;
            // sectionHpp
            atomic<int32_t> smodel{0}, cv_smodel{-1};
            atomic<int32_t> trigger{0}, trig_trigger{-1};
            atomic<int32_t> frequency{2048}, cv_frequency{-1};
            atomic<int32_t> level{4095}, cv_level{-1};
            atomic<int32_t> harmonics{2048}, cv_harmonics{-1};
            atomic<int32_t> timbre{2048}, cv_timbre{-1};
            atomic<int32_t> morph{2048}, cv_morph{-1};
            atomic<int32_t> lpg_color{2048}, cv_lpg_color{-1};
            atomic<int32_t> lpg_decay{2048}, cv_lpg_decay{-1};
            atomic<int32_t> mod_freq{0}, cv_mod_freq{-1};
            atomic<int32_t> mod_timbre{0}, cv_mod_timbre{-1};
            atomic<int32_t> mod_morph{0}, cv_mod_morph{-1};
            // sectionHpp
        };
    }
}