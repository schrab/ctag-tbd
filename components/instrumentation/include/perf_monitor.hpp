/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

Lock-free atomic performance monitoring component for real-time systems.

(c) 2025 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

CTAG TBD is provided "as is" without any express or implied warranties.
***************/

#pragma once

#include <atomic>
#include <cstdint>
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace CTAG {
namespace INSTRUMENTATION {

class PerfMonitor {
public:
    struct Stats {
        std::atomic<uint32_t> total_cycles{0};
        std::atomic<uint32_t> min_cycles{UINT32_MAX};
        std::atomic<uint32_t> max_cycles{0};
        std::atomic<uint32_t> call_count{0};
        std::atomic<bool> enabled{true};

        uint32_t start_cycles{0};

        void Reset() {
            total_cycles.store(0, std::memory_order_relaxed);
            min_cycles.store(UINT32_MAX, std::memory_order_relaxed);
            max_cycles.store(0, std::memory_order_relaxed);
            call_count.store(0, std::memory_order_relaxed);
        }
    };

    struct Section {
        char name[32];
        Stats stats;

        Section() : name{0} {}
        explicit Section(const char* section_name);
    };

    class ScopedMeasure {
    public:
        explicit ScopedMeasure(Section& section)
            : section_(section)
        {
            if (section_.stats.enabled.load(std::memory_order_relaxed)) {
                section_.stats.start_cycles = esp_cpu_get_cycle_count();
            }
        }

        ~ScopedMeasure() {
            if (section_.stats.enabled.load(std::memory_order_relaxed)) {
                uint32_t end = esp_cpu_get_cycle_count();
                uint32_t cycles = end - section_.stats.start_cycles;
                RecordCycles(section_.stats, cycles);
            }
        }

    private:
        Section& section_;

        ScopedMeasure(const ScopedMeasure&) = delete;
        ScopedMeasure& operator=(const ScopedMeasure&) = delete;
    };

    static Section& GetSection(const char* name);

    static inline void Begin(Section& section) {
        if (section.stats.enabled.load(std::memory_order_relaxed)) {
            section.stats.start_cycles = esp_cpu_get_cycle_count();
        }
    }

    static inline void End(Section& section) {
        if (section.stats.enabled.load(std::memory_order_relaxed)) {
            uint32_t end = esp_cpu_get_cycle_count();
            uint32_t cycles = end - section.stats.start_cycles;
            RecordCycles(section.stats, cycles);
        }
    }

    static void EnableSection(Section& section, bool enable) {
        section.stats.enabled.store(enable, std::memory_order_relaxed);
    }

    static void EnableAll(bool enable);
    static void ResetSection(Section& section) {
        section.stats.Reset();
    }
    static void ResetAll();
    static void EnableLogging(bool enable,
                            uint32_t interval_ms = 5000,
                            BaseType_t core_id = 0,
                            UBaseType_t priority = 1);

    static void GetStats(const Section& section,
                        uint32_t& avg_cycles,
                        uint32_t& min_cycles,
                        uint32_t& max_cycles,
                        uint32_t& count);

    static float CyclesToMicroseconds(uint32_t cycles);
    static uint32_t GetCpuFrequency();

private:
    static constexpr size_t MAX_SECTIONS = 32;

    struct SectionRegistry {
        Section sections[MAX_SECTIONS];
        std::atomic<uint32_t> count{0};
        std::atomic<bool> logging_enabled{false};
        TaskHandle_t logging_task{nullptr};
        uint32_t log_interval_ms{5000};
    };

    static SectionRegistry registry_;

    static void RecordCycles(Stats& stats, uint32_t cycles) {
        stats.total_cycles.fetch_add(cycles, std::memory_order_relaxed);
        stats.call_count.fetch_add(1, std::memory_order_relaxed);

        uint32_t current_min = stats.min_cycles.load(std::memory_order_relaxed);
        while (cycles < current_min &&
               !stats.min_cycles.compare_exchange_weak(current_min, cycles,
                   std::memory_order_relaxed));

        uint32_t current_max = stats.max_cycles.load(std::memory_order_relaxed);
        while (cycles > current_max &&
               !stats.max_cycles.compare_exchange_weak(current_max, cycles,
                   std::memory_order_relaxed));
    }

    static void LoggingTask(void* arg);
};

#define PERF_MEASURE(name) \
    CTAG::INSTRUMENTATION::PerfMonitor::ScopedMeasure \
    __perf_measure_##__LINE__(CTAG::INSTRUMENTATION::PerfMonitor::GetSection(name))

#define PERF_MEASURE_FUNCTION() PERF_MEASURE(__FUNCTION__)

} // namespace INSTRUMENTATION
} // namespace CTAG
