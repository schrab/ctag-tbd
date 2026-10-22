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
***************/

#include "ctagSoundProcessorMbeads.hpp"

#include <algorithm>
#include <cstring>

#include "esp_log.h"

#ifndef TBD_SIM
#include "perf_monitor.hpp"
#endif

using namespace CTAG::SP;

// ============================================================================
// Mbeads debug hooks
//
// Allows the debug shell to override the normal grain cap and to read back how
// many grains were actually rendering. The override defaults to -1 (disabled),
// so normal firmware behaviour is unchanged unless a host explicitly calls the
// endpoints. No user-facing parameter is added.
// ============================================================================
namespace {
    std::atomic<int> gMbeadsDebugTotalOverride{-1};   // -1 = use production policy
    std::atomic<int> gMbeadsDebugHelperOverride{-1};  // -1 = use production policy
    std::atomic<int> gMbeadsDebugActiveGrains{-1};    // -1 = plugin not loaded
    std::atomic<int> gMbeadsDebugHelperActive{-1};
    std::atomic<int> gMbeadsDebugCoreCap{0};
    std::atomic<int> gMbeadsDebugHelperCap{0};
    std::atomic<int> gMbeadsDebugHelperCycles{0};
    std::atomic<int> gMbeadsDebugHelperOverload{0};
    std::atomic<int> gMbeadsDebugHelperStack{-1};
}

extern "C" int mbeads_debug_set_total_cap(int cap) {
    if (cap < -1) cap = -1;
    if (cap > 32) cap = 32;
    return gMbeadsDebugTotalOverride.exchange(cap);
}

extern "C" int mbeads_debug_get_total_cap() {
    return gMbeadsDebugTotalOverride.load();
}

extern "C" int mbeads_debug_set_helper_cap(int cap) {
    if (cap < -1) cap = -1;
    if (cap > 32) cap = 32;
    return gMbeadsDebugHelperOverride.exchange(cap);
}

extern "C" int mbeads_debug_get_helper_cap() {
    return gMbeadsDebugHelperOverride.load();
}

extern "C" int mbeads_debug_get_active_grains() {
    return gMbeadsDebugActiveGrains.load();
}

extern "C" int mbeads_debug_get_helper_active_grains() {
    return gMbeadsDebugHelperActive.load();
}

extern "C" int mbeads_debug_get_helper_cycles() {
    return static_cast<int>(gMbeadsDebugHelperCycles.load());
}

extern "C" int mbeads_debug_get_helper_overload() {
    return gMbeadsDebugHelperOverload.load();
}

extern "C" int mbeads_debug_get_helper_stack_high_water() {
    return gMbeadsDebugHelperStack.load();
}

extern "C" int mbeads_debug_get_core_cap() {
    return gMbeadsDebugCoreCap.load();
}

void ctagSoundProcessorMbeads::Init(std::size_t blockSize, void *blockPtr) {
    (void) blockSize;
    (void) blockPtr;

    knowYourself();
    model = std::make_unique<ctagSPDataModel>(id, isStereo);
    LoadPreset(0);

    // The glue and the DSP must agree on the block size; the audio buffer is
    // bufSz samples (32) per channel.
    assert(static_cast<std::size_t>(bufSz) == kBlockSize);

    delay_buffer_ = static_cast<uint8_t *>(
        heap_caps_malloc(kDelayBufferBytes, MALLOC_CAP_SPIRAM));
    reverb_buffer_ = static_cast<uint16_t *>(
        heap_caps_malloc(kReverbBufferBytes, MALLOC_CAP_SPIRAM));

    if (delay_buffer_ == nullptr || reverb_buffer_ == nullptr) {
        ESP_LOGE("Mbeads",
                 "Cannot allocate PSRAM buffers (delay %p, reverb %p), "
                 "plugin will output silence",
                 delay_buffer_, reverb_buffer_);
        if (delay_buffer_ != nullptr) {
            heap_caps_free(delay_buffer_);
            delay_buffer_ = nullptr;
        }
        if (reverb_buffer_ != nullptr) {
            heap_caps_free(reverb_buffer_);
            reverb_buffer_ = nullptr;
        }
        initialized_ = false;
        gMbeadsDebugActiveGrains.store(-1);
        return;
    }

    delay_banks_[0] = delay_buffer_;
    delay_banks_[1] = delay_buffer_ + kDelayBankBytes;
    delay_banks_[2] = delay_buffer_ + 2 * kDelayBankBytes;

    std::memset(reverb_buffer_, 0, kReverbBufferBytes);

    processor.Init(delay_banks_, reverb_buffer_, 44100.0f);
    last_media_ = 0;
    initialized_ = true;
    gMbeadsDebugActiveGrains.store(0);

#ifndef TBD_SIM
    if (!StartHelper()) {
        ESP_LOGE("Mbeads", "helper task creation failed; using single-core cap");
    }

    static bool perf_enabled = false;
    if (!perf_enabled) {
        CTAG::INSTRUMENTATION::PerfMonitor::EnableLogging(true, 5000, 1, 5);
        perf_enabled = true;
    }
#endif
}

void ctagSoundProcessorMbeads::Process(const ProcessData &data) {
    if (!initialized_) {
        for (std::size_t i = 0; i < 2 * kBlockSize; ++i) {
            data.buf[i] = 0.0f;
        }
        return;
    }

    int media = static_cast<int>(media_.load());
    if (media != 0 && media != 1) {
        media = 0;
    }
    if (media != last_media_) {
        processor.Configure(media == 1
                                ? beads::Processor::MEDIA_VINTAGE_DIGITAL
                                : beads::Processor::MEDIA_DIGITAL,
                            false);
        last_media_ = media;
        mute_blocks_ = 2;
    }
    if (mute_blocks_ > 0) {
        --mute_blocks_;
        for (std::size_t i = 0; i < 2 * kBlockSize; ++i) {
            data.buf[i] = 0.0f;
        }
        return;
    }

#ifndef TBD_SIM
    PERF_MEASURE("Mbeads");
#endif

    const int cv_time = cv_time_.load();
    const int cv_size = cv_size_.load();
    const int cv_shape = cv_shape_.load();
    const int cv_pitch = cv_pitch_.load();
    const int cv_density = cv_density_.load();
    const int cv_feedback = cv_feedback_.load();
    const int cv_dry_wet = cv_dry_wet_.load();
    const int cv_reverb = cv_reverb_.load();

    const float time = std::clamp(time_.load() / 4095.0f, 0.0f, 1.0f);
    const float time_mod = std::clamp(
        (time_mod_.load() - 2047.5f) / 2047.5f, -1.0f, 1.0f);
    const float size = std::clamp(size_.load() / 4095.0f, 0.0f, 1.0f);
    const float size_mod = std::clamp(
        (size_mod_.load() - 2047.5f) / 2047.5f, -1.0f, 1.0f);
    const float shape = std::clamp(shape_.load() / 4095.0f, 0.0f, 1.0f);
    const float shape_mod = std::clamp(
        (shape_mod_.load() - 2047.5f) / 2047.5f, -1.0f, 1.0f);
    const float pitch = std::clamp(static_cast<float>(pitch_.load()),
                                   -24.0f, 24.0f);
    const float pitch_mod = std::clamp(
        (pitch_mod_.load() - 2047.5f) / 2047.5f, -1.0f, 1.0f);

    beads::Parameters p;
    p.time.base = time;
    p.time.attenuverter = time_mod;
    p.time.cv = cv_time != -1 ? data.cv[cv_time] * 1.6f : 0.0f;
    p.time.has_cv = cv_time != -1;

    p.size.base = size;
    p.size.attenuverter = size_mod;
    p.size.cv = cv_size != -1 ? data.cv[cv_size] * 1.6f : 0.0f;
    p.size.has_cv = cv_size != -1;

    p.shape.base = shape;
    p.shape.attenuverter = shape_mod;
    p.shape.cv = cv_shape != -1 ? data.cv[cv_shape] * 1.6f : 0.0f;
    p.shape.has_cv = cv_shape != -1;

    p.pitch.base = pitch;
    p.pitch.attenuverter = pitch_mod;
    p.pitch.cv = cv_pitch != -1 ? data.cv[cv_pitch] * 60.0f : 0.0f;
    p.pitch.has_cv = cv_pitch != -1;

    p.monophonic_mode = false;

    beads::ModulatedParameter density;
    density.base = std::clamp(density_.load() / 4095.0f, 0.0f, 1.0f);
    density.cv = cv_density != -1 ? data.cv[cv_density] : 0.0f;

    float feedback = feedback_.load() / 4095.0f;
    if (cv_feedback != -1) feedback += data.cv[cv_feedback];
    feedback = std::clamp(feedback, 0.0f, 1.0f);

    float dry_wet = dry_wet_.load() / 4095.0f;
    if (cv_dry_wet != -1) dry_wet += data.cv[cv_dry_wet];
    dry_wet = std::clamp(dry_wet, 0.0f, 1.0f);

    float reverb = reverb_.load() / 4095.0f;
    if (cv_reverb != -1) reverb += data.cv[cv_reverb];
    reverb = std::clamp(reverb, 0.0f, 1.0f);

    bool freeze = freeze_.load() != 0;
    const int trig_freeze = trig_freeze_.load();
    if (trig_freeze != -1) freeze = data.trig[trig_freeze] == 1;
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        freeze_frames_[i] = freeze;
    }

    beads::SeedButtonState seed_state = beads::SEED_BUTTON_STATE_OFF;
    const stmlib::GateFlags *seed_input = nullptr;
    const int trig_seed = trig_seed_.load();
    if (trig_seed != -1) {
        const bool seed_on = data.trig[trig_seed] == 1;
        for (std::size_t i = 0; i < kBlockSize; ++i) {
            seed_flags_[i] = seed_on ? stmlib::GATE_FLAG_HIGH
                                     : stmlib::GATE_FLAG_LOW;
        }
        seed_input = seed_flags_;
    } else if (seed_.load() != 0) {
        seed_state = beads::SEED_BUTTON_STATE_ON;
    }

    for (std::size_t i = 0; i < kBlockSize; ++i) {
        in_[i].l = data.buf[2 * i];
        in_[i].r = data.buf[2 * i + 1];
    }

    beads::Processor::MixParameters mix;
    mix.feedback = feedback;
    mix.dry_wet = dry_wet;
    mix.reverb_amount = reverb;
    mix.output_gate = false;

    const bool reverb_active =
        reverb * reverb * 0.9f > beads::Processor::kReverbActiveThreshold;

#ifndef TBD_SIM
    bool helper_available = (helper_task_ != nullptr) &&
                            helper_running_.load(std::memory_order_acquire);
#else
    bool helper_available = false;
#endif

    const int total_override = gMbeadsDebugTotalOverride.load();
    const int helper_override = gMbeadsDebugHelperOverride.load();

    int default_total;
    int default_helper;
    if (media == 0) { // Digital
        if (helper_available) {
            default_total = 16;
            default_helper = 8;
        } else {
            default_total = reverb_active ? 2 : 4;
            default_helper = 0;
        }
    } else { // Vintage
        default_total = reverb_active ? 8 : 16;
        default_helper = 0;
    }

    int total = total_override >= 0 ? total_override : default_total;
    int helper = helper_override >= 0 ? helper_override : default_helper;
    total = std::max(0, std::min(32, total));
    helper = std::max(0, std::min(total, helper));
    const int core = total - helper;

    processor.SetMaxActiveGrains(core);
    processor.SetHelperMaxActiveGrains(helper);

#ifndef TBD_SIM
    const int write_slot = static_cast<int>(helper_block_counter_ & 1u);
    const int read_slot = write_slot ^ 1;
    const bool use_helper = helper_available && media == 0 && helper > 0;
    const bool helper_ready =
        use_helper && helper_block_counter_ > 0 &&
        helper_done_seq_[read_slot].load(std::memory_order_acquire) ==
            helper_block_counter_ - 1;
    const beads::FloatFrame *extra_wet =
        helper_ready ? helper_out_[read_slot] : nullptr;

    const bool queue_helper =
        use_helper &&
        helper_slot_free_[write_slot].exchange(false, std::memory_order_acquire);
    beads::Processor::GrainHelperBlock *helper_block =
        queue_helper ? &helper_blocks_[write_slot] : nullptr;
#else
    const beads::FloatFrame *extra_wet = nullptr;
    beads::Processor::GrainHelperBlock *helper_block = nullptr;
#endif

#ifndef TBD_SIM
    {
        PERF_MEASURE("MbeadsGranular");
#endif
        processor.Process(seed_state, seed_input, freeze_frames_, density, p,
                          mix, in_, out_, kBlockSize, helper_block, extra_wet);
#ifndef TBD_SIM
    }
#endif

#ifndef TBD_SIM
    if (queue_helper) {
        helper_pending_slot_.store(write_slot, std::memory_order_relaxed);
        helper_pending_seq_.store(helper_block_counter_, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        xTaskNotifyGive(helper_task_);
    }
    ++helper_block_counter_;
#endif

    gMbeadsDebugActiveGrains.store(processor.ActiveGrains());
    gMbeadsDebugHelperActive.store(
        helper_available ? processor.HelperActiveGrains() : -1);
    gMbeadsDebugCoreCap.store(core);
    gMbeadsDebugHelperCap.store(helper);
#ifndef TBD_SIM
    gMbeadsDebugHelperCycles.store(
        static_cast<int>(helper_cycles_.load(std::memory_order_relaxed)));
    gMbeadsDebugHelperOverload.store(
        helper_overload_.load(std::memory_order_relaxed) ? 1 : 0);
    gMbeadsDebugHelperStack.store(
        helper_stack_high_water_.load(std::memory_order_relaxed));
#endif

    for (std::size_t i = 0; i < kBlockSize; ++i) {
        data.buf[2 * i] = out_[i].l;
        data.buf[2 * i + 1] = out_[i].r;
    }
}

ctagSoundProcessorMbeads::~ctagSoundProcessorMbeads() {
#ifndef TBD_SIM
    StopHelper();
#endif
    gMbeadsDebugActiveGrains.store(-1);
    gMbeadsDebugHelperActive.store(-1);
    if (delay_buffer_ != nullptr) {
        heap_caps_free(delay_buffer_);
        delay_buffer_ = nullptr;
    }
    if (reverb_buffer_ != nullptr) {
        heap_caps_free(reverb_buffer_);
        reverb_buffer_ = nullptr;
    }
}

#ifndef TBD_SIM
bool ctagSoundProcessorMbeads::StartHelper() {
    if (helper_task_ != nullptr) return true;
    helper_stop_.store(false, std::memory_order_release);
    helper_running_.store(false, std::memory_order_release);
    helper_done_seq_[0].store(UINT32_MAX, std::memory_order_relaxed);
    helper_done_seq_[1].store(UINT32_MAX, std::memory_order_relaxed);
    helper_slot_free_[0].store(true, std::memory_order_relaxed);
    helper_slot_free_[1].store(true, std::memory_order_relaxed);
    helper_pending_slot_.store(0, std::memory_order_relaxed);
    helper_pending_seq_.store(0, std::memory_order_relaxed);
    helper_block_counter_ = 0;

    BaseType_t ok = xTaskCreatePinnedToCore(
        HelperTaskEntry, "MbeadsGrain", kHelperTaskStack, this,
        kHelperTaskPriority, &helper_task_, kHelperTaskCore);
    if (ok != pdPASS) {
        helper_task_ = nullptr;
        ESP_LOGE("Mbeads", "helper task creation failed");
        return false;
    }
    // Wait up to 20 ms for the task to start.
    for (int i = 0; i < 20; ++i) {
        if (helper_running_.load(std::memory_order_acquire)) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (!helper_running_.load(std::memory_order_acquire)) {
        vTaskDelete(helper_task_);
        helper_task_ = nullptr;
        ESP_LOGE("Mbeads", "helper task did not start");
        return false;
    }
    return true;
}

void ctagSoundProcessorMbeads::StopHelper() {
    if (helper_task_ == nullptr) return;
    helper_stop_.store(true, std::memory_order_release);
    xTaskNotifyGive(helper_task_);
    for (int i = 0; i < 100; ++i) {
        if (!helper_running_.load(std::memory_order_acquire)) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (helper_running_.load(std::memory_order_acquire)) {
        vTaskDelete(helper_task_);
    }
    helper_task_ = nullptr;
}

void ctagSoundProcessorMbeads::HelperTaskEntry(void *arg) {
    static_cast<ctagSoundProcessorMbeads *>(arg)->HelperTask();
}

void ctagSoundProcessorMbeads::HelperTask() {
    helper_running_.store(true, std::memory_order_release);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (helper_stop_.load(std::memory_order_acquire)) break;

        const int slot = helper_pending_slot_.load(std::memory_order_acquire);
        const std::uint32_t seq =
            helper_pending_seq_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_acquire);

        const std::uint32_t start = esp_cpu_get_cycle_count();
        processor.RenderHelperGrains(helper_blocks_[slot], helper_out_[slot]);
        const std::uint32_t cycles = esp_cpu_get_cycle_count() - start;

        helper_cycles_.store(cycles, std::memory_order_relaxed);
        helper_overload_.store(cycles > kAudioBudgetCycles,
                               std::memory_order_relaxed);
        helper_stack_high_water_.store(
            static_cast<int>(uxTaskGetStackHighWaterMark(nullptr) *
                             sizeof(StackType_t)),
            std::memory_order_relaxed);

        helper_done_seq_[slot].store(seq, std::memory_order_release);
        helper_slot_free_[slot].store(true, std::memory_order_release);
    }
    helper_running_.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}
#endif

void ctagSoundProcessorMbeads::knowYourself() {
    pMapPar.emplace("time", [&](const int val) { time_ = val; });
    pMapCv.emplace("time", [&](const int val) { cv_time_ = val; });
    pMapPar.emplace("time_mod", [&](const int val) { time_mod_ = val; });

    pMapPar.emplace("size", [&](const int val) { size_ = val; });
    pMapCv.emplace("size", [&](const int val) { cv_size_ = val; });
    pMapPar.emplace("size_mod", [&](const int val) { size_mod_ = val; });

    pMapPar.emplace("shape", [&](const int val) { shape_ = val; });
    pMapCv.emplace("shape", [&](const int val) { cv_shape_ = val; });
    pMapPar.emplace("shape_mod", [&](const int val) { shape_mod_ = val; });

    pMapPar.emplace("pitch", [&](const int val) { pitch_ = val; });
    pMapCv.emplace("pitch", [&](const int val) { cv_pitch_ = val; });
    pMapPar.emplace("pitch_mod", [&](const int val) { pitch_mod_ = val; });

    pMapPar.emplace("density", [&](const int val) { density_ = val; });
    pMapCv.emplace("density", [&](const int val) { cv_density_ = val; });

    pMapPar.emplace("feedback", [&](const int val) { feedback_ = val; });
    pMapCv.emplace("feedback", [&](const int val) { cv_feedback_ = val; });

    pMapPar.emplace("dry_wet", [&](const int val) { dry_wet_ = val; });
    pMapCv.emplace("dry_wet", [&](const int val) { cv_dry_wet_ = val; });

    pMapPar.emplace("reverb", [&](const int val) { reverb_ = val; });
    pMapCv.emplace("reverb", [&](const int val) { cv_reverb_ = val; });

    pMapPar.emplace("media", [&](const int val) { media_ = val; });

    pMapPar.emplace("freeze", [&](const int val) { freeze_ = val; });
    pMapTrig.emplace("freeze", [&](const int val) { trig_freeze_ = val; });

    pMapPar.emplace("seed", [&](const int val) { seed_ = val; });
    pMapTrig.emplace("seed", [&](const int val) { trig_seed_ = val; });

    isStereo = true;
    id = "Mbeads";
}