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
#include <cstddef>
#include <cstdint>
#include <cassert>
#include "esp_heap_caps.h"
#include "ctagSoundProcessor.hpp"
#include "elements/dsp/part.h"

#ifndef TBD_SIM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_cpu.h>
#endif

namespace CTAG {
    namespace SP {
        class ctagSoundProcessorElements : public ctagSoundProcessor {
        public:
            // Allocate from PSRAM — class is too large for the 112KB DRAM arena
            static void* operator new(std::size_t sz) {
                void* p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
                if (!p) p = heap_caps_malloc(sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                assert(p);
                return p;
            }
            static void operator delete(void* p) {
                heap_caps_free(p);
            }

            virtual void Process(const ProcessData &) override;
            virtual ~ctagSoundProcessorElements();
            virtual void Init(std::size_t blockSize, void *blockPtr) override;

        private:
            virtual void knowYourself() override;

            static constexpr std::size_t kBlockSize = 32;
            // Modal resonator bands: 52 = original, 12 = single-core baseline.
            static constexpr std::size_t kResolutionFull = 52;
            static constexpr std::size_t kResolutionFallback = 12;

            // One block handed from the audio task (core 1) to the helper
            // (core 0). The patch snapshot travels with the block so core 1
            // never touches elements::Part while the helper renders.
            struct HelperBlock {
                elements::PerformanceState perf_state;
                elements::Patch patch;
                float blow_in[kBlockSize];
                float strike_in[kBlockSize];
                float main[kBlockSize];
                float aux[kBlockSize];
                int resonator_model;
                bool easter_egg;
                std::uint32_t seq;
            };

#ifndef TBD_SIM
            static constexpr std::size_t kHelperTaskStack = 4096;
            static constexpr UBaseType_t kHelperTaskPriority = configMAX_PRIORITIES - 2;
            static constexpr BaseType_t kHelperTaskCore = 0;
            static constexpr std::size_t kHelperSlots = 2;
            static constexpr std::uint32_t kAudioBudgetCycles = 174150;

            static void HelperTaskEntry(void *arg);
            void HelperTask();
            bool StartHelper();
            void StopHelper();

            TaskHandle_t helper_task_ = nullptr;
            std::atomic<bool> helper_stop_{false};
            std::atomic<bool> helper_running_{false};
            std::atomic<int> helper_pending_slot_{-1};
            std::atomic<std::uint32_t> helper_done_seq_[kHelperSlots] = {};
            std::atomic<bool> helper_slot_free_[kHelperSlots] = {};
            HelperBlock helper_blocks_[kHelperSlots] = {};
            std::uint32_t helper_block_counter_ = 0;
            std::atomic<std::uint32_t> helper_cycles_{0};
            std::atomic<std::uint32_t> helper_max_cycles_{0};
            std::atomic<bool> helper_overload_{false};
            std::atomic<int> helper_stack_high_water_{-1};
            std::atomic<std::uint32_t> helper_misses_{0};
#endif

            uint16_t *reverb_buffer;
            elements::Part part;
            elements::Patch patch;
            elements::PerformanceState perfState;
            bool prevGate = false;

            atomic<int32_t> frequency, cv_frequency;
            atomic<int32_t> resonator_model = 1, cv_resonator_model;
            atomic<int32_t> exciter_envelope_shape, cv_exciter_envelope_shape;
            atomic<int32_t> exciter_bow_level, cv_exciter_bow_level;
            atomic<int32_t> exciter_bow_timbre, cv_exciter_bow_timbre;
            atomic<int32_t> exciter_blow_level, cv_exciter_blow_level;
            atomic<int32_t> exciter_blow_meta, cv_exciter_blow_meta;
            atomic<int32_t> exciter_blow_timbre, cv_exciter_blow_timbre;
            atomic<int32_t> exciter_strike_level, cv_exciter_strike_level;
            atomic<int32_t> exciter_strike_meta, cv_exciter_strike_meta;
            atomic<int32_t> exciter_strike_timbre, cv_exciter_strike_timbre;
            atomic<int32_t> exciter_signature, cv_exciter_signature;
            atomic<int32_t> resonator_geometry, cv_resonator_geometry;
            atomic<int32_t> resonator_brightness, cv_resonator_brightness;
            atomic<int32_t> resonator_damping, cv_resonator_damping;
            atomic<int32_t> resonator_position, cv_resonator_position;
            atomic<int32_t> resonator_mod_freq, cv_resonator_mod_freq;
            atomic<int32_t> resonator_mod_offset, cv_resonator_mod_offset;
            atomic<int32_t> reverb_diffusion, cv_reverb_diffusion;
            atomic<int32_t> reverb_lp, cv_reverb_lp;
            atomic<int32_t> space, cv_space;
            atomic<int32_t> modulation_frequency, cv_modulation_frequency;
            atomic<int32_t> gate, trig_gate;
            atomic<int32_t> easter_egg, trig_easter_egg;
        };
    }
}
