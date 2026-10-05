/*
 * Host tests for ctagTempo.
 *
 * Covers the tempo engine added in the modulation workstream. The FreeRTOS
 * tick is faked via tests/host/shim so tap-tempo behaviour is deterministic
 * and does not depend on wall-clock time.
 */
#include "test_framework.hpp"
#include "ctagTempo.hpp"
#include "freertos/FreeRTOS.h"

using CTAG::SP::HELPERS::ctagTempo;

namespace {

// TBD default: 44100 Hz, 32-sample blocks (see codec.cpp).
constexpr float kFs = 44100.0f;
constexpr uint32_t kBlock = 32;
constexpr float kBlocksPerSec = kFs / kBlock;

} // namespace

// --- Internal clock -------------------------------------------------------

TEST(tempo_internal_phase_advances_at_bpm) {
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);
    t.SetBPM(120.0f);

    // 120 BPM = 2 beats/s; blocks/s = 44100/32.
    float expected = 2.0f / kBlocksPerSec;
    t.Tick();
    CHECK_NEAR(t.GetPhase(), expected, 1e-6f);
}

TEST(tempo_phase_wraps_at_one_beat) {
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);
    t.SetBPM(120.0f);

    // One beat is 44100/2 = 22050 samples = 689.0625 blocks, so 689 ticks get
    // just short of a full beat and the 690th wraps.
    for (int i = 0; i < 689; i++) t.Tick();
    float beforeWrap = t.GetPhase();
    CHECK_NEAR(beforeWrap, 0.999913f, 1e-4f);

    t.Tick();
    CHECK_MSG(t.GetPhase() < 0.01f, "phase must wrap at one beat, not accumulate");

    // Phase must never exceed 1.0 over a long run.
    for (int i = 0; i < 100000; i++) {
        t.Tick();
        CHECK(t.GetPhase() < 1.0f);
    }
}

TEST(tempo_bpm_is_clamped_to_valid_range) {
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);

    t.SetBPM(5.0f);
    CHECK_NEAR(t.GetBPM(), 20.0f, 1e-4f);

    t.SetBPM(5000.0f);
    CHECK_NEAR(t.GetBPM(), 300.0f, 1e-4f);
}

TEST(tempo_stop_and_reset_control_internal_clock) {
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);
    t.SetBPM(120.0f);

    // Reset() halts and rewinds the internal clock, and is always available.
    for (int i = 0; i < 100; i++) t.Tick();
    CHECK(t.GetPhase() > 0.0f);

    t.Reset();
    CHECK(t.IsRunning());
    CHECK_NEAR(t.GetPhase(), 0.0f, 1e-9f);

    // And it advances again afterwards.
    for (int i = 0; i < 1000; i++) t.Tick();
    CHECK(t.GetPhase() > 0.0f);
}

TEST(tempo_stop_halts_midi_clock_source) {
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);
    t.SetSource(ctagTempo::Source::MIDI_CLOCK);

    t.Stop();
    CHECK(!t.IsRunning());

    // A stopped MIDI clock ignores further clock messages.
    float frozen = t.GetPhase();
    for (int i = 0; i < 48; i++) {
        TestHostSetTick(i * 10);
        t.OnMidiClock();
    }
    CHECK_NEAR(t.GetPhase(), frozen, 1e-9f);

    t.Continue();
    CHECK(t.IsRunning());
}

TEST(tempo_continue_resumes_midi_clock_source) {
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);
    t.SetSource(ctagTempo::Source::MIDI_CLOCK);

    t.Stop();
    t.Continue();
    CHECK(t.IsRunning());

    float before = t.GetPhase();
    TestHostSetTick(0);
    t.OnMidiClock();
    CHECK(t.GetPhase() != before);
}

// --- Sample rate (issue #19) ---------------------------------------------

TEST(tempo_set_bpm_respects_configured_sample_rate) {
    // Regression for the hard-coded 44100/32 literals in SetBPM/SetSource.
    // At 48 kHz/32 the internal rate must follow the configured sample rate,
    // not the literals.
    ctagTempo t;
    t.SetSampleRate(48000.0f, kBlock);
    t.SetBPM(60.0f);

    float blocksPerSec = 48000.0f / kBlock;
    float expected = 1.0f / blocksPerSec; // 60 BPM = 1 beat/s
    t.Tick();
    CHECK_NEAR(t.GetPhase(), expected, 1e-6f);
}

TEST(tempo_set_source_respects_configured_sample_rate) {
    ctagTempo t;
    t.SetSampleRate(48000.0f, kBlock);
    t.SetBPM(60.0f);
    t.SetSource(ctagTempo::Source::INTERNAL);

    float expected = 1.0f / (48000.0f / kBlock);
    t.Tick();
    CHECK_NEAR(t.GetPhase(), expected, 1e-6f);
}

// --- MIDI clock (issue #4, the headline bug) ------------------------------

TEST(tempo_midi_clock_updates_bpm_from_tick_interval) {
    // 96 clocks at 10ms spacing = 1.6s for 4 beats => 150 BPM.
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);
    t.SetBPM(120.0f);
    t.SetSource(ctagTempo::Source::MIDI_CLOCK);

    const uint32_t kIntervalMs = 10;
    for (int i = 0; i < 96; i++) {
        TestHostSetTick(i * kIntervalMs);
        t.OnMidiClock();
    }

    // 24 PPQN * 4 beats = 96 clocks in 4 * (60000/BPM) ms
    // => BPM = 4 * 60000 / (96 * 10) = 250... check against measured interval:
    float expected = 60000.0f / (24.0f * kIntervalMs);
    CHECK_MSG(t.GetBPM() > 100.0f,
              "MIDI clock must derive BPM from the tick interval; GetBPM() "
              "stayed at the last dialled-in value");
    CHECK_NEAR(t.GetBPM(), expected, 1.0f);
}

TEST(tempo_midi_clock_phase_tracks_24ppqn) {
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);
    t.SetSource(ctagTempo::Source::MIDI_CLOCK);

    for (int i = 0; i < 24; i++) {
        TestHostSetTick(i * 10);
        t.OnMidiClock();
    }
    // 24 pulses == 1 beat == phase back to 0 (wraps).
    CHECK(t.GetPhase() < 1.0f);
    CHECK_NEAR(t.GetPhase(), 0.0f, 1e-5f);
}

TEST(tempo_internal_clock_ignores_midi_clock_messages) {
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);
    t.SetBPM(120.0f);
    // source stays INTERNAL

    for (int i = 0; i < 24; i++) {
        TestHostSetTick(i * 10);
        t.OnMidiClock();
    }
    // Internal phase must be untouched by MIDI traffic.
    CHECK_NEAR(t.GetPhase(), 0.0f, 1e-9f);
}

// --- Transport (issue #8) -------------------------------------------------

TEST(tempo_midi_stop_does_not_halt_internal_clock) {
    // A stray 0xFC from an external device must not be able to freeze the
    // internal clock when the source is INTERNAL.
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);
    t.SetBPM(120.0f);
    CHECK(t.GetSource() == ctagTempo::Source::INTERNAL);

    t.Stop();
    t.Tick();
    CHECK_MSG(t.IsRunning(),
              "Stop() must not halt the clock while source is INTERNAL; "
              "a stray MIDI 0xFC permanently freezes the internal tempo");

    for (int i = 0; i < 100; i++) t.Tick();
    CHECK(t.GetPhase() > 0.0f);
}

TEST(tempo_midi_stop_halts_clock_when_source_is_midi) {
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);
    t.SetSource(ctagTempo::Source::MIDI_CLOCK);

    t.Stop();
    CHECK(!t.IsRunning());
}

// --- Tap tempo (issue #7) ------------------------------------------------

TEST(tempo_tap_averages_recent_intervals) {
    // doc/modulation-system.md: "averages last 4 taps".
    // Intervals 400/400/500/400 ms => mean 425 ms => ~141 BPM.
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);
    t.SetSource(ctagTempo::Source::INTERNAL);

    const uint32_t intervals[4] = {400, 400, 500, 400};
    uint32_t now = 10000;
    TestHostSetTick(now);
    t.OnTapTempo(); // first tap starts the measurement

    for (int i = 0; i < 4; i++) {
        now += intervals[i];
        TestHostSetTick(now);
        t.OnTapTempo();
    }

    float expected = 60000.0f / 425.0f;
    CHECK_MSG(std::fabs(t.GetBPM() - expected) < 5.0f,
              "tap tempo must average the recent intervals, not just use the "
              "last one");
}

TEST(tempo_tap_ignores_intervals_outside_window) {
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);
    t.SetBPM(120.0f);

    uint32_t now = 10000;
    TestHostSetTick(now);
    t.OnTapTempo();

    // 50ms is below the 200ms floor -> must be discarded, not set to 1200 BPM.
    now += 50;
    TestHostSetTick(now);
    t.OnTapTempo();
    CHECK_NEAR(t.GetBPM(), 120.0f, 1.0f);

    // 5000ms is above the 3000ms ceiling -> must be discarded.
    now += 5000;
    TestHostSetTick(now);
    t.OnTapTempo();
    CHECK_NEAR(t.GetBPM(), 120.0f, 1.0f);
}

TEST(tempo_tap_resets_after_long_pause) {
    ctagTempo t;
    t.SetSampleRate(kFs, kBlock);
    t.SetBPM(120.0f);

    uint32_t now = 10000;
    TestHostSetTick(now);
    t.OnTapTempo();
    now += 500;
    TestHostSetTick(now);
    t.OnTapTempo();
    CHECK_NEAR(t.GetBPM(), 120.0f, 5.0f);

    // Long pause -> next tap starts a fresh measurement.
    now += 10000;
    TestHostSetTick(now);
    t.OnTapTempo();
    CHECK_NEAR(t.GetBPM(), 120.0f, 5.0f);
}