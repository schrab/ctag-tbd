#pragma once

// BBA2 Elements render ladder (doc/bba2-elements-sram-handoff.md, Finding 2).
//
// The Elements render cost depends on the resonator's EXCITATION state: a
// struck, high-geometry patch costs ~220-230k cycles per 32-sample block
// while decayed passages cost ~140k, and the mode count barely matters.
// A static resolution either wastes budget or overruns at the peaks, so
// the ladder watches the measured part.Process cycles and steps the
// resolution DOWN one step after 8 consecutive over-threshold blocks.
//
// One-way per plugin instance (A1S V9 parity): a transient excitation can
// pin a lower count until the plugin is reloaded; /debug/setElementsMode
// or a plugin switch restores. Pure and host-tested
// (tests/host/test_render_ladder.cpp).

#include <cstddef>
#include <cstdint>

namespace CTAG {
    namespace SP {

        class RenderLadder {
        public:
            static constexpr std::size_t kNumSteps = 3; // {24, 20, 16}
            static constexpr uint32_t kOverStreak = 8;
            static constexpr std::size_t kSteps[kNumSteps] = {24, 20, 16};

            RenderLadder() = default;

            // The resolution the ladder currently prescribes (no Update
            // needed — use before the first block too).
            std::size_t current() const { return kSteps[step_]; }

            // Feed one measured render cost + the block budget for the
            // current sample rate. Returns the resolution to render with
            // THIS block (evaluate AFTER rendering; the returned value
            // applies from the next block).
            std::size_t Update(uint32_t renderCycles, uint32_t budgetCycles) {
                // 85% threshold: the whole-loop overhead (~8k: Marbles,
                // codec I/O, DC-cut, instrumentation) must fit alongside.
                const bool over =
                    renderCycles > (budgetCycles * 17 / 20);
                if (over) {
                    if (underStreak_ > 0) underStreak_ = 0;
                    if (overStreak_ < kOverStreak) ++overStreak_;
                    if (overStreak_ >= kOverStreak && step_ + 1 < kNumSteps) {
                        ++step_;
                        overStreak_ = 0;
                    }
                } else {
                    overStreak_ = 0;
                }
                return kSteps[step_];
            }

            std::size_t step() const { return step_; }

        private:
            std::size_t step_ = 0;
            uint32_t overStreak_ = 0;
            uint32_t underStreak_ = 0; // reserved for a future recovery rule
        };

    } // namespace SP
} // namespace CTAG
