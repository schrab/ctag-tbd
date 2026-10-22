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

#include "ctagSoundProcessorElements.hpp"
#include <iostream>
#include <cmath>
#include "stmlib/stmlib.h"

#ifndef TBD_SIM
#include "perf_monitor.hpp"
#endif

using namespace CTAG::SP;

// ============================================================================
// Elements debug hooks
//
// Read-only counters for the second-core helper, consumed by
// /debug/getAudioStats. Distinct keys from the Mbeads fields so both plugins
// can be inspected with the same endpoint.
// ============================================================================
namespace {
    std::atomic<int> gElementsDebugHelperCycles{0};
    std::atomic<int> gElementsDebugHelperMaxCycles{0};
    std::atomic<int> gElementsDebugHelperOverload{0};
    std::atomic<int> gElementsDebugHelperStack{-1};   // -1 = plugin not loaded
    std::atomic<int> gElementsDebugHelperResolution{0};
    std::atomic<int> gElementsDebugHelperMisses{0};
}

extern "C" int elements_debug_get_helper_cycles() {
    return gElementsDebugHelperCycles.load();
}

extern "C" int elements_debug_get_helper_max_cycles() {
    return gElementsDebugHelperMaxCycles.load();
}

extern "C" int elements_debug_get_helper_overload() {
    return gElementsDebugHelperOverload.load();
}

extern "C" int elements_debug_get_helper_stack_high_water() {
    return gElementsDebugHelperStack.load();
}

extern "C" int elements_debug_get_helper_resolution() {
    return gElementsDebugHelperResolution.load();
}

extern "C" int elements_debug_get_helper_misses() {
    return gElementsDebugHelperMisses.load();
}

void ctagSoundProcessorElements::Init(std::size_t blockSize, void *blockPtr) {
    knowYourself();
    model = std::make_unique<ctagSPDataModel>(id, isStereo);
    LoadPreset(0);

    // The helper handoff carries exactly one 32-sample block.
    assert(static_cast<std::size_t>(bufSz) == kBlockSize);

    reverb_buffer = (uint16_t *) heap_caps_malloc(32768 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    assert(reverb_buffer != nullptr);

    part.Init(reverb_buffer);

#ifndef TBD_SIM
    if (StartHelper()) {
        part.set_resolution(kResolutionFull);
        helper_applied_resolution_.store(kResolutionFull, std::memory_order_relaxed);
    } else {
        // Fallback: run on the audio core at the proven 12-mode resolution.
        part.set_resolution(kResolutionFallback);
        helper_applied_resolution_.store(kResolutionFallback, std::memory_order_relaxed);
        direct_fallback_prepared_ = true;
        ESP_LOGE("Elements", "helper task unavailable; single-core fallback (12 modes)");
    }

    static bool perf_enabled = false;
    if (!perf_enabled) {
        CTAG::INSTRUMENTATION::PerfMonitor::EnableLogging(true, 5000, 1, 5);
        perf_enabled = true;
    }
#else
    part.set_resolution(kResolutionFallback);
    direct_fallback_prepared_ = true;
#endif
    // Reset the debug stats for this plugin load.
    PublishDebugStats();
}

void ctagSoundProcessorElements::PublishDebugStats() {
#ifndef TBD_SIM
    gElementsDebugHelperCycles.store(
        static_cast<int>(helper_cycles_.load(std::memory_order_relaxed)));
    gElementsDebugHelperMaxCycles.store(
        static_cast<int>(helper_max_cycles_.load(std::memory_order_relaxed)));
    gElementsDebugHelperOverload.store(
        helper_overload_.load(std::memory_order_relaxed) ? 1 : 0);
    gElementsDebugHelperStack.store(
        helper_stack_high_water_.load(std::memory_order_relaxed));
    gElementsDebugHelperResolution.store(
        static_cast<int>(helper_applied_resolution_.load(std::memory_order_relaxed)));
    gElementsDebugHelperMisses.store(
        static_cast<int>(helper_misses_.load(std::memory_order_relaxed)));
#else
    // Simulator: always the single-core 12-mode path, no helper task.
    gElementsDebugHelperCycles.store(0);
    gElementsDebugHelperMaxCycles.store(0);
    gElementsDebugHelperOverload.store(0);
    gElementsDebugHelperStack.store(-1);
    gElementsDebugHelperResolution.store(static_cast<int>(kResolutionFallback));
    gElementsDebugHelperMisses.store(0);
#endif
}

void ctagSoundProcessorElements::Process(const ProcessData &data) {
    if (static_cast<std::size_t>(bufSz) != kBlockSize) {
        // The helper handoff is sized for one fixed 32-sample block.
        for (int i = 0; i < 2 * bufSz; i++) {
            data.buf[i] = 0.f;
        }
        return;
    }

    // Fill the core-1 owned patch snapshot. The helper (or the single-core
    // fallback) copies it into elements::Part; core 1 never writes to Part in
    // helper mode.
    elements::Patch* p = &patch;
    p->exciter_envelope_shape = exciter_envelope_shape / 4095.f;
    if (cv_exciter_envelope_shape != -1)
        p->exciter_envelope_shape = fabsf(data.cv[cv_exciter_envelope_shape]);
    CONSTRAIN(p->exciter_envelope_shape, 0.f, 1.f);

    p->exciter_bow_level = exciter_bow_level / 4095.f;
    if (cv_exciter_bow_level != -1)
        p->exciter_bow_level = fabsf(data.cv[cv_exciter_bow_level]);
    CONSTRAIN(p->exciter_bow_level, 0.f, 1.f);

    p->exciter_bow_timbre = exciter_bow_timbre / 4095.f;
    if (cv_exciter_bow_timbre != -1)
        p->exciter_bow_timbre = fabsf(data.cv[cv_exciter_bow_timbre]);
    CONSTRAIN(p->exciter_bow_timbre, 0.f, 1.f);

    p->exciter_blow_level = exciter_blow_level / 4095.f;
    if (cv_exciter_blow_level != -1)
        p->exciter_blow_level = fabsf(data.cv[cv_exciter_blow_level]);
    CONSTRAIN(p->exciter_blow_level, 0.f, 1.f);

    p->exciter_blow_meta = exciter_blow_meta / 4095.f;
    if (cv_exciter_blow_meta != -1)
        p->exciter_blow_meta = fabsf(data.cv[cv_exciter_blow_meta]);
    CONSTRAIN(p->exciter_blow_meta, 0.f, 1.f);

    p->exciter_blow_timbre = exciter_blow_timbre / 4095.f;
    if (cv_exciter_blow_timbre != -1)
        p->exciter_blow_timbre = fabsf(data.cv[cv_exciter_blow_timbre]);
    CONSTRAIN(p->exciter_blow_timbre, 0.f, 1.f);

    p->exciter_strike_level = exciter_strike_level / 4095.f;
    if (cv_exciter_strike_level != -1)
        p->exciter_strike_level = fabsf(data.cv[cv_exciter_strike_level]);
    CONSTRAIN(p->exciter_strike_level, 0.f, 1.f);

    p->exciter_strike_meta = exciter_strike_meta / 4095.f;
    if (cv_exciter_strike_meta != -1)
        p->exciter_strike_meta = fabsf(data.cv[cv_exciter_strike_meta]);
    CONSTRAIN(p->exciter_strike_meta, 0.f, 1.f);

    p->exciter_strike_timbre = exciter_strike_timbre / 4095.f;
    if (cv_exciter_strike_timbre != -1)
        p->exciter_strike_timbre = fabsf(data.cv[cv_exciter_strike_timbre]);
    CONSTRAIN(p->exciter_strike_timbre, 0.f, 1.f);

    p->exciter_signature = exciter_signature / 4095.f;
    if (cv_exciter_signature != -1)
        p->exciter_signature = fabsf(data.cv[cv_exciter_signature]);
    CONSTRAIN(p->exciter_signature, 0.f, 1.f);

    p->resonator_geometry = resonator_geometry / 4095.f;
    if (cv_resonator_geometry != -1)
        p->resonator_geometry = fabsf(data.cv[cv_resonator_geometry]);
    CONSTRAIN(p->resonator_geometry, 0.f, 1.f);

    p->resonator_brightness = resonator_brightness / 4095.f;
    if (cv_resonator_brightness != -1)
        p->resonator_brightness = fabsf(data.cv[cv_resonator_brightness]);
    CONSTRAIN(p->resonator_brightness, 0.f, 1.f);

    p->resonator_damping = resonator_damping / 4095.f;
    if (cv_resonator_damping != -1)
        p->resonator_damping = fabsf(data.cv[cv_resonator_damping]);
    CONSTRAIN(p->resonator_damping, 0.f, 1.f);

    p->resonator_position = resonator_position / 4095.f;
    if (cv_resonator_position != -1)
        p->resonator_position = fabsf(data.cv[cv_resonator_position]);
    CONSTRAIN(p->resonator_position, 0.f, 1.f);

    p->resonator_modulation_frequency = (resonator_mod_freq / 4095.f * 0.5f + 0.4f) / 44100.f;
    if (cv_resonator_mod_freq != -1)
        p->resonator_modulation_frequency = (fabsf(data.cv[cv_resonator_mod_freq]) * 0.5f + 0.4f) / 44100.f;
    CONSTRAIN(p->resonator_modulation_frequency, 0.4f / 44100.f, 1.2f / 44100.f);

    p->resonator_modulation_offset = resonator_mod_offset / 4095.f * 0.15f + 0.05f;
    if (cv_resonator_mod_offset != -1)
        p->resonator_modulation_offset = fabsf(data.cv[cv_resonator_mod_offset]) * 0.15f + 0.05f;
    CONSTRAIN(p->resonator_modulation_offset, 0.05f, 0.2f);

    p->reverb_diffusion = reverb_diffusion / 4095.f * 0.3f + 0.55f;
    if (cv_reverb_diffusion != -1)
        p->reverb_diffusion = fabsf(data.cv[cv_reverb_diffusion]) * 0.3f + 0.55f;
    CONSTRAIN(p->reverb_diffusion, 0.55f, 0.85f);

    p->reverb_lp = reverb_lp / 4095.f * 0.3f + 0.5f;
    if (cv_reverb_lp != -1)
        p->reverb_lp = fabsf(data.cv[cv_reverb_lp]) * 0.3f + 0.5f;
    CONSTRAIN(p->reverb_lp, 0.5f, 0.8f);

    p->space = space / 4095.f * 1.75f;
    if (cv_space != -1)
        p->space = fabsf(data.cv[cv_space]) * 1.75f;
    CONSTRAIN(p->space, 0.f, 1.75f);

    p->modulation_frequency = modulation_frequency / 4095.f * 0.5f;
    if (cv_modulation_frequency != -1)
        p->modulation_frequency = fabsf(data.cv[cv_modulation_frequency]) * 0.5f;
    CONSTRAIN(p->modulation_frequency, 0.f, 0.5f);

    // PerformanceState
    bool gateVal = gate;
    if (trig_gate != -1)
        gateVal = data.trig[trig_gate] == 1;
    perfState.gate = gateVal;

    float freqCv = frequency / 4032.f * 96.f;
    if (cv_frequency != -1)
        freqCv += data.cv[cv_frequency] * 5.f * 12.f;
    CONSTRAIN(freqCv, 0.f, 127.f);
    perfState.note = freqCv;

    perfState.modulation = 0.f;
    perfState.strength = 0.5f;

    // Resonator model
    int rModel = resonator_model;
    if (cv_resonator_model != -1)
        rModel = (int)(floorf(data.cv[cv_resonator_model] * 3.f)) % 3;
    CONSTRAIN(rModel, 0, 2);

    bool ee = easter_egg;
    if (trig_easter_egg != -1)
        ee = data.trig[trig_easter_egg] == 1;

#ifndef TBD_SIM
    PERF_MEASURE("Elements");
#endif
    float blow_in[kBlockSize], strike_in[kBlockSize];
    for (std::size_t i = 0; i < kBlockSize; i++) {
        strike_in[i] = data.buf[i * 2];
        blow_in[i] = data.buf[i * 2 + 1];
    }

#ifndef TBD_SIM
    const bool helper_alive = (helper_task_ != nullptr) &&
                              helper_running_.load(std::memory_order_acquire);
    const bool helper_stopping = helper_alive &&
                                 helper_stop_.load(std::memory_order_acquire);
    if (helper_stopping) {
        // V9 last resort: the helper was asked to stop at the 12-mode floor.
        // `part` still belongs to it until it deletes itself, so stay silent.
        if (helper_block_counter_ > 0) {
            helper_misses_.fetch_add(1, std::memory_order_relaxed);
        }
        for (std::size_t i = 0; i < 2 * kBlockSize; i++) {
            data.buf[i] = 0.f;
        }
        PublishDebugStats();
        ++helper_block_counter_;
        return;
    }
    const bool helper_available = helper_alive &&
                                  !helper_stop_.load(std::memory_order_acquire);
    if (helper_available) {
        // Hand this block to the core-0 helper. Publishing is gated on an empty
        // pending slot so a block can never be overwritten in flight (that
        // would leave its slot marked busy forever).
        const int write_slot = static_cast<int>(helper_block_counter_ & 1u);
        if (helper_pending_slot_.load(std::memory_order_acquire) < 0 &&
            helper_slot_free_[write_slot].exchange(false, std::memory_order_acquire)) {
            HelperBlock& block = helper_blocks_[write_slot];
            block.perf_state = perfState;
            block.patch = patch;
            block.resonator_model = rModel;
            block.easter_egg = ee;
            block.seq = helper_block_counter_;
            for (std::size_t i = 0; i < kBlockSize; i++) {
                block.blow_in[i] = blow_in[i];
                block.strike_in[i] = strike_in[i];
            }
            helper_pending_slot_.store(write_slot, std::memory_order_release);
            xTaskNotifyGive(helper_task_);
        }

        // One block of latency: the previous block's render is read back here.
        const int read_slot = write_slot ^ 1;
        const bool helper_ready =
            helper_block_counter_ > 0 &&
            helper_done_seq_[read_slot].load(std::memory_order_acquire) ==
                helper_block_counter_ - 1;
        if (helper_ready) {
            const HelperBlock& block = helper_blocks_[read_slot];
            for (std::size_t i = 0; i < kBlockSize; i++) {
                data.buf[i * 2] = block.main[i] * 0.125f;
                data.buf[i * 2 + 1] = block.aux[i] * 0.125f;
            }
        } else {
            // Nothing renderable this block (first block, or the helper missed
            // its deadline). Silence, never the live input.
            if (helper_block_counter_ > 0) {
                helper_misses_.fetch_add(1, std::memory_order_relaxed);
            }
            for (std::size_t i = 0; i < 2 * kBlockSize; i++) {
                data.buf[i] = 0.f;
            }
        }

        // V9 deadline fallback: a run of consecutive misses or over-budget
        // renders steps the modal count down (52 -> 32 -> 24 -> 16 -> 12).
        // Core 1 only publishes the request; the helper owns `part`. At the
        // 12-mode floor another such run requests a stop and hands the render
        // back to this core (the helper deletes itself; we never touch `part`).
        if (helper_block_counter_ > 0) {
            const bool struggling =
                !helper_ready ||
                helper_overload_.load(std::memory_order_relaxed);
            if (struggling) {
                if (++helper_streak_ >= kFallbackStreak) {
                    helper_streak_ = 0;
                    if (resolution_step_ + 1 < kNumResolutionSteps) {
                        ++resolution_step_;
                        helper_requested_resolution_.store(
                            kResolutionSteps[resolution_step_],
                            std::memory_order_release);
                    } else {
                        helper_stop_.store(true, std::memory_order_release);
                        xTaskNotifyGive(helper_task_);
                        ESP_LOGW("Elements",
                                 "helper still missing deadlines at 12 modes; "
                                 "falling back to single-core");
                    }
                }
            } else {
                helper_streak_ = 0;
            }
        }
        PublishDebugStats();
        ++helper_block_counter_;
        return;
    }
#endif

    // Single-core fallback (also the simulator path): render on this core.
    if (!direct_fallback_prepared_) {
        // The helper never started, or died at runtime; this core owns `part`
        // again and must not run it at the 52-mode budget.
        part.set_resolution(kResolutionFallback);
#ifndef TBD_SIM
        helper_applied_resolution_.store(kResolutionFallback,
                                         std::memory_order_relaxed);
#endif
        direct_fallback_prepared_ = true;
    }
    *part.mutable_patch() = patch;
    part.set_resonator_model((elements::ResonatorModel) rModel);
    part.set_easter_egg(ee);

    float out[kBlockSize], aux[kBlockSize];
    part.Process(perfState, blow_in, strike_in, out, aux, kBlockSize);

    for (std::size_t i = 0; i < kBlockSize; i++) {
        data.buf[i * 2] = out[i] * 0.125f;
        data.buf[i * 2 + 1] = aux[i] * 0.125f;
    }
    PublishDebugStats();
}

ctagSoundProcessorElements::~ctagSoundProcessorElements() {
#ifndef TBD_SIM
    // Only tear down the renderer before the buffers it writes into.
    if (!StopHelper()) {
        // Force-deleted; StopHelper already waited for the core to drop it.
        ESP_LOGW("Elements", "helper task force-deleted; freeing reverb buffer");
    }
    // An unloaded plugin must not report stale helper stats.
    gElementsDebugHelperStack.store(-1);
    gElementsDebugHelperResolution.store(0);
#endif
    if (reverb_buffer != nullptr) {
        heap_caps_free(reverb_buffer);
        reverb_buffer = nullptr;
    }
}

#ifndef TBD_SIM
bool ctagSoundProcessorElements::StartHelper() {
    if (helper_task_ != nullptr) return true;
    helper_stop_.store(false, std::memory_order_release);
    helper_running_.store(false, std::memory_order_release);
    helper_pending_slot_.store(-1, std::memory_order_relaxed);
    helper_done_seq_[0].store(UINT32_MAX, std::memory_order_relaxed);
    helper_done_seq_[1].store(UINT32_MAX, std::memory_order_relaxed);
    helper_slot_free_[0].store(true, std::memory_order_relaxed);
    helper_slot_free_[1].store(true, std::memory_order_relaxed);
    helper_block_counter_ = 0;
    helper_cycles_.store(0, std::memory_order_relaxed);
    helper_max_cycles_.store(0, std::memory_order_relaxed);
    helper_overload_.store(false, std::memory_order_relaxed);
    helper_stack_high_water_.store(-1, std::memory_order_relaxed);
    helper_misses_.store(0, std::memory_order_relaxed);
    helper_requested_resolution_.store(kResolutionFull, std::memory_order_relaxed);
    helper_applied_resolution_.store(kResolutionFull, std::memory_order_relaxed);
    resolution_step_ = 0;
    helper_streak_ = 0;

    BaseType_t ok = xTaskCreatePinnedToCore(
        HelperTaskEntry, "Elements", kHelperTaskStack, this,
        kHelperTaskPriority, &helper_task_, kHelperTaskCore);
    if (ok != pdPASS) {
        helper_task_ = nullptr;
        ESP_LOGE("Elements", "helper task creation failed");
        return false;
    }
    // Wait up to ~200 ms for the task to start. Time-based (CONFIG_FREERTOS_HZ
    // is 100, so pdMS_TO_TICKS(1) is 0 ticks and would be a bare yield).
    for (int i = 0; i < 20; ++i) {
        if (helper_running_.load(std::memory_order_acquire)) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!helper_running_.load(std::memory_order_acquire)) {
        vTaskDelete(helper_task_);
        helper_task_ = nullptr;
        ESP_LOGE("Elements", "helper task did not start");
        return false;
    }
    return true;
}

bool ctagSoundProcessorElements::StopHelper() {
    if (helper_task_ == nullptr) return true;
    if (!helper_running_.load(std::memory_order_acquire)) {
        // The helper already deleted itself (for example after the V9 12-mode
        // handoff); the handle is stale, so neither notify nor delete it.
        helper_task_ = nullptr;
        return true;
    }
    helper_stop_.store(true, std::memory_order_release);
    xTaskNotifyGive(helper_task_);
    // Wait up to ~500 ms of real time for the helper to leave its render and
    // delete itself. pdMS_TO_TICKS(1) == 0 at CONFIG_FREERTOS_HZ=100, so a
    // 1 ms delay would not actually wait.
    for (int i = 0; i < 50; ++i) {
        if (!helper_running_.load(std::memory_order_acquire)) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    bool stopped = !helper_running_.load(std::memory_order_acquire);
    if (!stopped) {
        // Last resort: the task is wedged. Delete it, then let the other core
        // observe the deletion before the caller frees the reverb buffer.
        ESP_LOGE("Elements", "helper task did not stop; deleting");
        vTaskDelete(helper_task_);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    helper_task_ = nullptr;
    return stopped;
}

void ctagSoundProcessorElements::HelperTaskEntry(void *arg) {
    static_cast<ctagSoundProcessorElements *>(arg)->HelperTask();
}

void ctagSoundProcessorElements::HelperTask() {
    helper_running_.store(true, std::memory_order_release);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Drain one publish; if core 1 managed to publish again while this
        // render was running, pick it up immediately so no slot stays busy.
        for (;;) {
            if (helper_stop_.load(std::memory_order_acquire)) break;
            const int slot = helper_pending_slot_.exchange(-1, std::memory_order_acq_rel);
            if (slot < 0) break;

            HelperBlock& block = helper_blocks_[slot];
            const std::uint32_t seq = block.seq;

            // Measure the whole per-block cost: resolution change, patch
            // snapshot copy, model/EE apply, then Part::Process (V9 budget).
            const std::uint32_t start = esp_cpu_get_cycle_count();
            const std::size_t requested =
                helper_requested_resolution_.load(std::memory_order_acquire);
            if (requested != helper_applied_resolution_.load(std::memory_order_relaxed)) {
                part.set_resolution(requested);
                helper_applied_resolution_.store(requested, std::memory_order_release);
            }
            part.set_resonator_model((elements::ResonatorModel) block.resonator_model);
            part.set_easter_egg(block.easter_egg);
            *part.mutable_patch() = block.patch;
            part.Process(block.perf_state, block.blow_in, block.strike_in,
                         block.main, block.aux, kBlockSize);
            const std::uint32_t cycles = esp_cpu_get_cycle_count() - start;

            helper_cycles_.store(cycles, std::memory_order_relaxed);
            if (cycles > helper_max_cycles_.load(std::memory_order_relaxed)) {
                helper_max_cycles_.store(cycles, std::memory_order_relaxed);
            }
            helper_overload_.store(cycles > kAudioBudgetCycles,
                                   std::memory_order_relaxed);
            helper_stack_high_water_.store(
                static_cast<int>(uxTaskGetStackHighWaterMark(nullptr) *
                                 sizeof(StackType_t)),
                std::memory_order_relaxed);

            helper_done_seq_[slot].store(seq, std::memory_order_release);
            helper_slot_free_[slot].store(true, std::memory_order_release);
        }
        if (helper_stop_.load(std::memory_order_acquire)) break;
    }
    helper_running_.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}
#endif

void ctagSoundProcessorElements::knowYourself() {
    pMapPar.emplace("frequency", [&](const int val) { frequency = val; });
    pMapCv.emplace("frequency", [&](const int val) { cv_frequency = val; });
    pMapPar.emplace("resonator_model", [&](const int val) { resonator_model = val; });
    pMapCv.emplace("resonator_model", [&](const int val) { cv_resonator_model = val; });
    pMapPar.emplace("exciter_envelope_shape", [&](const int val) { exciter_envelope_shape = val; });
    pMapCv.emplace("exciter_envelope_shape", [&](const int val) { cv_exciter_envelope_shape = val; });
    pMapPar.emplace("exciter_bow_level", [&](const int val) { exciter_bow_level = val; });
    pMapCv.emplace("exciter_bow_level", [&](const int val) { cv_exciter_bow_level = val; });
    pMapPar.emplace("exciter_bow_timbre", [&](const int val) { exciter_bow_timbre = val; });
    pMapCv.emplace("exciter_bow_timbre", [&](const int val) { cv_exciter_bow_timbre = val; });
    pMapPar.emplace("exciter_blow_level", [&](const int val) { exciter_blow_level = val; });
    pMapCv.emplace("exciter_blow_level", [&](const int val) { cv_exciter_blow_level = val; });
    pMapPar.emplace("exciter_blow_meta", [&](const int val) { exciter_blow_meta = val; });
    pMapCv.emplace("exciter_blow_meta", [&](const int val) { cv_exciter_blow_meta = val; });
    pMapPar.emplace("exciter_blow_timbre", [&](const int val) { exciter_blow_timbre = val; });
    pMapCv.emplace("exciter_blow_timbre", [&](const int val) { cv_exciter_blow_timbre = val; });
    pMapPar.emplace("exciter_strike_level", [&](const int val) { exciter_strike_level = val; });
    pMapCv.emplace("exciter_strike_level", [&](const int val) { cv_exciter_strike_level = val; });
    pMapPar.emplace("exciter_strike_meta", [&](const int val) { exciter_strike_meta = val; });
    pMapCv.emplace("exciter_strike_meta", [&](const int val) { cv_exciter_strike_meta = val; });
    pMapPar.emplace("exciter_strike_timbre", [&](const int val) { exciter_strike_timbre = val; });
    pMapCv.emplace("exciter_strike_timbre", [&](const int val) { cv_exciter_strike_timbre = val; });
    pMapPar.emplace("exciter_signature", [&](const int val) { exciter_signature = val; });
    pMapCv.emplace("exciter_signature", [&](const int val) { cv_exciter_signature = val; });
    pMapPar.emplace("resonator_geometry", [&](const int val) { resonator_geometry = val; });
    pMapCv.emplace("resonator_geometry", [&](const int val) { cv_resonator_geometry = val; });
    pMapPar.emplace("resonator_brightness", [&](const int val) { resonator_brightness = val; });
    pMapCv.emplace("resonator_brightness", [&](const int val) { cv_resonator_brightness = val; });
    pMapPar.emplace("resonator_damping", [&](const int val) { resonator_damping = val; });
    pMapCv.emplace("resonator_damping", [&](const int val) { cv_resonator_damping = val; });
    pMapPar.emplace("resonator_position", [&](const int val) { resonator_position = val; });
    pMapCv.emplace("resonator_position", [&](const int val) { cv_resonator_position = val; });
    pMapPar.emplace("resonator_mod_freq", [&](const int val) { resonator_mod_freq = val; });
    pMapCv.emplace("resonator_mod_freq", [&](const int val) { cv_resonator_mod_freq = val; });
    pMapPar.emplace("resonator_mod_offset", [&](const int val) { resonator_mod_offset = val; });
    pMapCv.emplace("resonator_mod_offset", [&](const int val) { cv_resonator_mod_offset = val; });
    pMapPar.emplace("reverb_diffusion", [&](const int val) { reverb_diffusion = val; });
    pMapCv.emplace("reverb_diffusion", [&](const int val) { cv_reverb_diffusion = val; });
    pMapPar.emplace("reverb_lp", [&](const int val) { reverb_lp = val; });
    pMapCv.emplace("reverb_lp", [&](const int val) { cv_reverb_lp = val; });
    pMapPar.emplace("space", [&](const int val) { space = val; });
    pMapCv.emplace("space", [&](const int val) { cv_space = val; });
    pMapPar.emplace("modulation_frequency", [&](const int val) { modulation_frequency = val; });
    pMapCv.emplace("modulation_frequency", [&](const int val) { cv_modulation_frequency = val; });
    pMapPar.emplace("gate", [&](const int val) { gate = val; });
    pMapTrig.emplace("gate", [&](const int val) { trig_gate = val; });
    pMapPar.emplace("easter_egg", [&](const int val) { easter_egg = val; });
    pMapTrig.emplace("easter_egg", [&](const int val) { trig_easter_egg = val; });
    isStereo = true;
    id = "Elements";
}
