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
#include "render_ladder.hpp"
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
            // Arena-allocated like every other plugin: the Part engine
            // union (elements/dsp/voice.h) shrank the object from 115,856 B
            // to ~81.5 kB, so it fits the internal-SRAM arena — NO PSRAM
            // (PSRAM residency doubled the render cost and the user
            // rejected it). The previous heap overrides and the 963a2f45
            // boot reservation are gone.

            virtual void Process(const ProcessData &) override;
            virtual ~ctagSoundProcessorElements();
            virtual void Init(std::size_t blockSize, void *blockPtr) override;

        private:
            virtual void knowYourself() override;
            void PublishDebugStats();

            static constexpr std::size_t kBlockSize = 32;
#if defined(CONFIG_TBD_PLATFORM_BBA2)
            // S3 @ 32 kHz (native Elements rate): 24 modes keep the
            // high-geometry worst case (stiffness > 0 sustains all modes at
            // high Q above geometry ~900) inside the 240k block budget with
            // >40% headroom (handoff doc, Finding 2 — hardware-measured
            // 32 modes at 78% baseline before the high-geometry push).
            // The load-time chain-budget cap (chain_budget.hpp) can lower
            // this further; /debug/setElementsMode overrides for tests.
            static constexpr std::size_t kResolutionFull = 24;
            static constexpr std::size_t kResolutionFallback = 16;
#else
            // Modal resonator bands: 52 = dual-core target @ 32kHz
            // (2026-11-16 hardware-measured at pinned 52: steady 352-388 µs
            // avg across decay/noise/strikes/the full param matrix; worst
            // combo — modal model, damping 0 — 529 µs avg with isolated
            // 1520 µs blocks that the helper's one-block latency + DMA ring
            // absorb; the overload flag is per-block and the ladder needs 16
            // CONSECUTIVE over-budget blocks, so spikes never trigger it).
            // 24 = the proven-safe 2026-11-05 operating point, kept as the
            // ladder's first fallback step; 16 = floor before the V9 helper
            // retire.
            static constexpr std::size_t kResolutionFull = 52;
            static constexpr std::size_t kResolutionFallback = 16;
#endif

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
            static constexpr std::uint32_t kAudioBudgetCycles = 240000;
            // V9 deadline fallback: step the modal count down after a run of
            // consecutive misses/overloads (hysteresis, never per block).
            // The ladder is one-way per plugin instance: a transient core-0
            // burst can pin a lower count until the plugin is reloaded, so
            // Task 3 must not read the settled count as a hardware limit.
            static constexpr int kFallbackStreak = 16;
#if defined(CONFIG_TBD_PLATFORM_BBA2)
            static constexpr std::size_t kResolutionSteps[] = {
                kResolutionFull, 20, kResolutionFallback};
#else
            // A1S: 52 -> 24 (the proven-safe 2026-11-05 point) -> 16.
            static constexpr std::size_t kResolutionSteps[] = {
                kResolutionFull, 24, kResolutionFallback};
#endif
            static constexpr std::size_t kNumResolutionSteps =
                sizeof(kResolutionSteps) / sizeof(kResolutionSteps[0]);

            static void HelperTaskEntry(void *arg);
            void HelperTask();
            bool StartHelper();
            bool StopHelper();

            TaskHandle_t helper_task_ = nullptr;

            // Per-block handoff state shared between the audio task (core 1)
            // and the helper (core 0). Allocated in internal DRAM, separate
            // from the PSRAM plugin object: the two HelperBlock copies and the
            // seq_cst atomics run every block, and on classic ESP32 S32C1I
            // does not work on PSRAM, so PSRAM atomics take the slow software
            // workaround and the copies compete with the reverb/delay
            // streaming for the shared data cache. elements::Part itself
            // (~115 KB, larger than the whole 112 KB DRAM arena) stays in the
            // PSRAM object.
            struct HelperShared {
                HelperBlock blocks_[kHelperSlots];
                std::atomic<bool> stop_{false};
                std::atomic<bool> running_{false};
                std::atomic<int> pending_slot_{-1};
                std::atomic<std::uint32_t> done_seq_[kHelperSlots] = {};
                std::atomic<bool> slot_free_[kHelperSlots] = {};
                std::uint32_t block_counter_ = 0;
                std::atomic<std::uint32_t> cycles_{0};
                std::atomic<std::uint32_t> max_cycles_{0};
                std::atomic<bool> overload_{false};
                std::atomic<int> stack_high_water_{-1};
                std::atomic<std::uint32_t> misses_{0};
                // Core 1 asks, the helper applies — core 1 never touches `part`.
                std::atomic<std::size_t> requested_resolution_{kResolutionFull};
                // Written by the helper right after part.set_resolution(); read by
                // PublishDebugStats() so core 1 never reads part.resolution().
                std::atomic<std::size_t> applied_resolution_{kResolutionFull};
                std::size_t resolution_step_ = 0;
                int streak_ = 0;
                // Set once when V9 hands the render back to the audio core. The
                // warning is emitted from the destructor, never from Process().
                bool deadline_fallback_ = false;
            };
            HelperShared *helper_shared_ = nullptr;
#endif

            uint16_t *reverb_buffer = nullptr;
            // Warps cross-modulator, heap-allocated OUTSIDE the arena (the
            // arena is exactly full). The ~7.4 kB core (xmod algorithms) is
            // internal-first — per-sample PSRAM state measured unusable on
            // the A1S (~640 us/block of SPI-stall for ~42 us of math, plus
            // 39 ms pathological blocks). The 65.8 kB vocoder is a separate
            // allocation, PSRAM-acceptable (only runs in the vocoder zone,
            // the CPU-heaviest algorithm). Both freed after the helper stops.
            warps::Modulator *warps_mod_ = nullptr;
            void *warps_vocoder_ = nullptr;
            elements::Part part;
            elements::Patch patch;
            elements::PerformanceState perfState;
            bool prevGate = false;
            // True once this core has taken `part` back (no helper).
            bool direct_fallback_prepared_ = false;
            // BBA2 only: the cap-adjusted default resolution set at Init; 0
            // on other targets. The direct render path re-applies it whenever
            // the /debug/setElementsMode override is cleared back to -1.
            std::size_t cap_resolution_ = 0;
            // BBA2 only: auto step-down ladder driven by the measured
            // part.Process cost (render_ladder.hpp; host-tested). Excited-
            // state peaks step 24 -> 20 -> 16, one-way per instance.
            RenderLadder render_ladder_;

            atomic<int32_t> frequency{800}, cv_frequency{-1};
            atomic<int32_t> resonator_model{0}, cv_resonator_model{-1};
            atomic<int32_t> exciter_envelope_shape{2047}, cv_exciter_envelope_shape{-1};
            atomic<int32_t> exciter_bow_level{0}, cv_exciter_bow_level{-1};
            atomic<int32_t> exciter_bow_timbre{2047}, cv_exciter_bow_timbre{-1};
            atomic<int32_t> exciter_blow_level{0}, cv_exciter_blow_level{-1};
            atomic<int32_t> exciter_blow_meta{2047}, cv_exciter_blow_meta{-1};
            atomic<int32_t> exciter_blow_timbre{2047}, cv_exciter_blow_timbre{-1};
            atomic<int32_t> exciter_strike_level{3276}, cv_exciter_strike_level{-1};
            atomic<int32_t> exciter_strike_meta{2047}, cv_exciter_strike_meta{-1};
            atomic<int32_t> exciter_strike_timbre{2047}, cv_exciter_strike_timbre{-1};
            atomic<int32_t> exciter_signature{0}, cv_exciter_signature{-1};
            atomic<int32_t> resonator_geometry{819}, cv_resonator_geometry{-1};
            atomic<int32_t> resonator_brightness{2047}, cv_resonator_brightness{-1};
            atomic<int32_t> resonator_damping{1024}, cv_resonator_damping{-1};
            atomic<int32_t> resonator_position{1229}, cv_resonator_position{-1};
            atomic<int32_t> resonator_mod_freq{0}, cv_resonator_mod_freq{-1};
            atomic<int32_t> resonator_mod_offset{410}, cv_resonator_mod_offset{-1};
            atomic<int32_t> reverb_diffusion{2560}, cv_reverb_diffusion{-1};
            atomic<int32_t> reverb_lp{2867}, cv_reverb_lp{-1};
            atomic<int32_t> space{2047}, cv_space{-1};
            atomic<int32_t> modulation_frequency{0}, cv_modulation_frequency{-1};
            // Warps stage (0..4095 UI scale). algorithm 0 = bypass (zero
            // cost); the two levels map onto the engine's channel_drive
            // gain pots (0.5 = near-unity, saturator provides overdrive).
            // Non-zero level defaults so raising the algorithm on an old
            // preset (which carries no warps members) is immediately audible.
            atomic<int32_t> warps_algorithm{0}, cv_warps_algorithm{-1};
            atomic<int32_t> warps_timbre{2047}, cv_warps_timbre{-1};
            atomic<int32_t> warps_mod_level{2048}, cv_warps_mod_level{-1};
            atomic<int32_t> warps_carrier_level{2048}, cv_warps_carrier_level{-1};
            atomic<int32_t> warps_mix{2047}, cv_warps_mix{-1};
            atomic<int32_t> strength{2048}, cv_strength{-1};
            atomic<int32_t> play{0}, trig_play{-1};
            std::atomic<bool> play_pulse{false};
            int pulse_blocks_{0};
            static constexpr int kPulseBlockCount = 20; // ~14.5 ms @ 44.1 kHz / 32 samples
            atomic<int32_t> gate{0}, trig_gate{-1};
            atomic<int32_t> easter_egg{0};
        };
    }
}
