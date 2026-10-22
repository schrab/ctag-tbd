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

#include "ctagSoundProcessorMbeads.hpp"

#include <algorithm>
#include <cstring>

#include "esp_log.h"

#ifndef TBD_SIM
#include "perf_monitor.hpp"
#endif

using namespace CTAG::SP;

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
        // No assert here on purpose: a missing PSRAM allocation must not abort
        // the firmware (asserts are enabled in this build). Process() checks
        // initialized_ and outputs silence instead.
        if (delay_buffer_ != nullptr) {
            heap_caps_free(delay_buffer_);
            delay_buffer_ = nullptr;
        }
        if (reverb_buffer_ != nullptr) {
            heap_caps_free(reverb_buffer_);
            reverb_buffer_ = nullptr;
        }
        initialized_ = false;
        return;
    }

    delay_banks_[0] = delay_buffer_;
    delay_banks_[1] = delay_buffer_ + kDelayBankBytes;
    delay_banks_[2] = delay_buffer_ + 2 * kDelayBankBytes;

    // The reverb network is skipped while its mix is zero (the default). Clear
    // the PSRAM buffer once so enabling it starts from silence, not garbage.
    std::memset(reverb_buffer_, 0, kReverbBufferBytes);

    processor.Init(delay_banks_, reverb_buffer_, 44100.0f);
    // beads::Processor::Init already calls Configure(MEDIA_DIGITAL, false), so
    // the 768 KB delay buffer is cleared exactly once here. Configure() only
    // runs again when the media parameter changes (see Process()).
    last_media_ = 0;
    initialized_ = true;

#ifndef TBD_SIM
    // Performance logging is process-wide and must only be started once, no
    // matter how often the plugin is loaded. Core 1 / priority 5 keeps the
    // logging task well below the audio task (core 1, priority 23).
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

    // Media switching. Configure() clears 768 KB of PSRAM (only the delay line
    // of the active channel layout) and must never run on every block; accept a
    // two-block mute when the mode changes.
    // 0 = Digital, 1 = Vintage; anything else falls back to Digital.
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
    // Measures the whole per-block plugin cost from here on, including the
    // parameter conversion, the Beads engine and the copy back to data.buf.
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

    // Fields are assigned explicitly; the aggregate order of
    // beads::RandomizedParameter is base, attenuverter, cv, has_cv.
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

    // TBD trigger convention: 1 means active.
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

    // CPU protection. The granular engine's cost grows with the number of
    // simultaneously rendering grains; at long Size settings the natural
    // overlap exceeds the audio-task budget and produces dropouts. The reverb
    // network is also expensive, so reserve headroom for it when it is
    // audible. These caps keep the worst case inside the 174,150-cycle block
    // budget while leaving the default patch (0-1 grains) untouched.
    const bool reverb_active =
        reverb * reverb * 0.9f > beads::Processor::kReverbActiveThreshold;
    processor.SetMaxActiveGrains(reverb_active ? 2 : 4);

#ifndef TBD_SIM
    {
        // Nested section: the granular engine alone, without the glue's
        // parameter conversion and buffer copies.
        PERF_MEASURE("MbeadsGranular");
#endif
        processor.Process(seed_state, seed_input, freeze_frames_, density, p,
                          mix, in_, out_, kBlockSize);
#ifndef TBD_SIM
    }
#endif

    for (std::size_t i = 0; i < kBlockSize; ++i) {
        data.buf[2 * i] = out_[i].l;
        data.buf[2 * i + 1] = out_[i].r;
    }
}

ctagSoundProcessorMbeads::~ctagSoundProcessorMbeads() {
    if (delay_buffer_ != nullptr) {
        heap_caps_free(delay_buffer_);
        delay_buffer_ = nullptr;
    }
    if (reverb_buffer_ != nullptr) {
        heap_caps_free(reverb_buffer_);
        reverb_buffer_ = nullptr;
    }
}

void ctagSoundProcessorMbeads::knowYourself() {
    // Granular parameters with bipolar attenuverters. Without CV the
    // attenuverter is Beads' internal randomization depth.
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
