#pragma once

// BBA2 (ESP32-S3) load-time CPU budget (doc/bba2-esp32s3-sdd.md §6.3).
//
// The S3 audio task is single-core (no Elements helper task): every loaded
// plugin draws from one 174,150-cycle budget per 32-sample block at 44.1 kHz
// (doc/esp32s3-porting-guide.md). This helper is the ONLY component that
// decides plugin load de-rating; it is pure and host-tested
// (tests/host/test_chain_budget.cpp).
//
// Policy: only an Elements LOAD is ever capped (52 -> 32 -> 24 -> 16 modes,
// largest step whose block sum fits under 90% of budget). Any other
// over-cap load combination is allowed with a warning — operators choose,
// SPManager logs. Estimates below are bench-measured (guide §6.1) except
// Elements 24M/16M, which are linear interpolations of the 12/32/52/56M
// measurements (~51.7k base + ~1.27k/mode).

#include <atomic>
#include <cstdint>

namespace CTAG {
    namespace SP {

        // Consumed by ctagSoundProcessorElements::init() on BBA2; SPManager
        // writes it through ChainBudget::ElementsResolution before Create.
        // Legacy targets never read it. Declared before ChainBudget because
        // the struct's static members reference it.
        inline std::atomic<int> gElementsCapResolution{52};

        struct ChainBudget {
            static constexpr uint32_t kBudgetCycles = 174150; // 32/44100 * 240 MHz
            static constexpr uint32_t kCapPct = 90;
            static constexpr uint32_t kCapCycles = kBudgetCycles * kCapPct / 100; // 156735

            // Estimated cycles/block at 44.1 kHz. Unknown ids cost 0 (not
            // counted — logged by the caller, never refused).
            static uint32_t EstimateCycles(const char *id) {
                if (id == nullptr) return 0;
                const auto eq = [](const char *a, const char *b) {
                    while (*a && *b && *a == *b) { ++a; ++b; }
                    return *a == *b;
                };
                if (eq(id, "Void") || eq(id, "")) return 0;
                if (eq(id, "TBDaits")) return 45000;
                if (eq(id, "TBDings")) return 55000;
                if (eq(id, "Claude")) return 62000;
                if (eq(id, "Mbeads")) return 100000; // provisional, unmeasured (SDD §6.3)
                if (eq(id, "Elements")) return 120000; // 52 modes
                return 0;
            }

            // Resolution ladder for Elements loads. Costs parallel the steps.
            // Returns the chosen resolution and stores it into
            // gElementsCapResolution. Non-Elements loads return the current
            // cap untouched.
            static int ElementsResolution(const char *newId, const char *otherId) {
                if (newId == nullptr || !eq_id(newId, "Elements")) {
                    return gElementsCapResolution.load(std::memory_order_relaxed);
                }
                static constexpr int kSteps[] = {52, 32, 24, 16};
                static constexpr uint32_t kStepCosts[] = {120000, 93000, 82000, 72000};
                const uint32_t other = EstimateCycles(otherId);
                for (std::size_t i = 0; i < 4; ++i) {
                    if (other + kStepCosts[i] <= kCapCycles) {
                        gElementsCapResolution.store(kSteps[i], std::memory_order_relaxed);
                        return kSteps[i];
                    }
                }
                // Even 16 modes overflows next to 'other' — best effort floor.
                gElementsCapResolution.store(16, std::memory_order_relaxed);
                return 16;
            }

        private:
            static bool eq_id(const char *a, const char *b) {
                while (*a && *b && *a == *b) { ++a; ++b; }
                return *a == *b;
            }
        };

        // Per-block audio budget in cycles, published by the audio task
        // (240k @ 32 kHz / 174.15k @ 44.1 kHz) and consumed by the BBA2
        // Elements render ladder (render_ladder.hpp).
        inline std::atomic<uint32_t> gBlockBudgetCycles{240000};

    } // namespace SP
} // namespace CTAG
