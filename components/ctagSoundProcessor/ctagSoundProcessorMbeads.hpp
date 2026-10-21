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

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "esp_heap_caps.h"

#include "ctagSoundProcessor.hpp"
#include "beads/dsp/frame.h"
#include "beads/dsp/processor.h"
#include "stmlib/utils/gate_flags.h"

namespace CTAG::SP {
    class ctagSoundProcessorMbeads : public ctagSoundProcessor {
    public:
        // The processor object is ~30 KB and the DSP buffers are allocated from
        // PSRAM; it must not live in the 112 KB sound processor arena.
        static void *operator new(std::size_t sz) {
            void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
            if (!p) p = heap_caps_malloc(sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            assert(p);
            return p;
        }

        static void operator delete(void *p) {
            heap_caps_free(p);
        }

        void Process(const ProcessData &) override;
        void Init(std::size_t blockSize, void *blockPtr) override;
        ~ctagSoundProcessorMbeads() override;

    private:
        void knowYourself() override;

        static constexpr std::size_t kBlockSize = 32;
        // Beads' scratch buffers and block clamps are sized for 32 samples.
        static_assert(kBlockSize <= beads::Processor::kMaxBlockSize,
                      "Mbeads block size exceeds the Beads processor limit");
        // 3 x 256 KiB banks, shared by all four media delay lines.
        static constexpr std::size_t kDelayBufferBytes = 786432;
        static constexpr std::size_t kDelayBankBytes = 262144;
        // 32768 x uint16_t Dattorro reverb buffer.
        static constexpr std::size_t kReverbBufferBytes = 65536;

        beads::Processor processor;
        uint8_t *delay_buffer_ = nullptr;
        uint16_t *reverb_buffer_ = nullptr;
        uint8_t *delay_banks_[3] = {};
        // Beads cannot process in place: the dry signal is captured from in_
        // while the wet engine writes out_.
        beads::FloatFrame in_[kBlockSize] = {};
        beads::FloatFrame out_[kBlockSize] = {};
        bool freeze_frames_[kBlockSize] = {};
        stmlib::GateFlags seed_flags_[kBlockSize] = {};
        int last_media_ = -1;
        int mute_blocks_ = 0;
        bool initialized_ = false;

        // Parameters, initialized to the default patch so that a missing or
        // broken preset cannot leave them indeterminate.
        std::atomic<int32_t> time_{2048};
        std::atomic<int32_t> time_mod_{2047};
        std::atomic<int32_t> size_{2048};
        std::atomic<int32_t> size_mod_{2047};
        std::atomic<int32_t> shape_{2048};
        std::atomic<int32_t> shape_mod_{2047};
        std::atomic<int32_t> pitch_{0};
        std::atomic<int32_t> pitch_mod_{2047};
        std::atomic<int32_t> density_{3072};
        std::atomic<int32_t> feedback_{0};
        std::atomic<int32_t> dry_wet_{2048};
        std::atomic<int32_t> reverb_{0};
        std::atomic<int32_t> media_{0};
        std::atomic<int32_t> freeze_{0};
        std::atomic<int32_t> seed_{1};

        std::atomic<int32_t> cv_time_{-1};
        std::atomic<int32_t> cv_size_{-1};
        std::atomic<int32_t> cv_shape_{-1};
        std::atomic<int32_t> cv_pitch_{-1};
        std::atomic<int32_t> cv_density_{-1};
        std::atomic<int32_t> cv_feedback_{-1};
        std::atomic<int32_t> cv_dry_wet_{-1};
        std::atomic<int32_t> cv_reverb_{-1};

        std::atomic<int32_t> trig_freeze_{-1};
        std::atomic<int32_t> trig_seed_{-1};
    };
}
