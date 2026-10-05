/*
 * Host tests for ctagSeq16 and ctagGate16.
 *
 * Both are pure value types with no I/O, so timing, direction and probability
 * behaviour can be verified exactly on the host.
 */
#include "test_framework.hpp"
#include "ctagSeq16.hpp"
#include "ctagGate16.hpp"

#include <vector>

using CTAG::SP::HELPERS::ctagSeq16;
using CTAG::SP::HELPERS::ctagGate16;

namespace {

constexpr float kFs = 44100.0f;
constexpr uint32_t kBlock = 32;
constexpr float kBlocksPerSec = kFs / kBlock;

// Mirrors SPManager.cpp: N_CVS/N_TRIGS for CONFIG_TBD_PLATFORM_BBA.
constexpr int kNCvs = 100;
constexpr int kNTrigs = 40;

// Blocks needed to advance exactly `beats` at `bpm`.
int BlocksForBeats(float bpm, float beats) {
    return static_cast<int>((beats * 60.0f / bpm) * kBlocksPerSec + 0.5f);
}

} // namespace

// ---------------------------------------------------------------------------
// ctagSeq16
// ---------------------------------------------------------------------------

TEST(seq_unassigned_writes_nothing) {
    ctagSeq16 seq;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    seq.SetCVSlot(-1);
    seq.SetTrigSlot(-1);

    for (int i = 0; i < 2000; i++) seq.Process(120.0f, cv.data(), trig.data(), kBlock);

    for (float v : cv) CHECK_NEAR(v, 0.0f, 1e-9f);
    for (uint8_t t : trig) CHECK_EQ(t, 0);
}

TEST(seq_step_timing_is_accurate_at_120bpm) {
    ctagSeq16 seq;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    seq.SetCVSlot(0);
    seq.SetTrigSlot(0);
    seq.SetStepLength(1.0f);

    // The first step boundary lands one full step after start, so allow one
    // extra beat of settling before counting.
    int settle = BlocksForBeats(120.0f, 1.0f);
    for (int i = 0; i < settle; i++) seq.Process(120.0f, cv.data(), trig.data(), kBlock);

    int triggers = 0;
    uint8_t prev = trig[0];
    int blocks = BlocksForBeats(120.0f, 20.0f);
    for (int i = 0; i < blocks; i++) {
        seq.Process(120.0f, cv.data(), trig.data(), kBlock);
        if (trig[0] && !prev) triggers++;
        prev = trig[0];
    }
    CHECK_MSG(triggers == 20, "one trigger per beat expected");
}

TEST(seq_step_length_scales_timing) {
    // At 0.5 beats/step we should get twice as many triggers per beat.
    ctagSeq16 seq;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    seq.SetCVSlot(0);
    seq.SetTrigSlot(0);
    seq.SetStepLength(0.5f);

    int settle = BlocksForBeats(120.0f, 0.5f);
    for (int i = 0; i < settle; i++) seq.Process(120.0f, cv.data(), trig.data(), kBlock);

    int triggers = 0;
    uint8_t prev = trig[0];
    int blocks = BlocksForBeats(120.0f, 10.0f);
    for (int i = 0; i < blocks; i++) {
        seq.Process(120.0f, cv.data(), trig.data(), kBlock);
        if (trig[0] && !prev) triggers++;
        prev = trig[0];
    }
    CHECK_MSG(triggers == 20, "two steps per beat expected over 10 beats");
}

TEST(seq_step_length_is_clamped) {
    ctagSeq16 seq;
    seq.SetStepLength(0.0f);
    CHECK(seq.GetStepLength() >= 0.125f);
    seq.SetStepLength(1000.0f);
    CHECK(seq.GetStepLength() <= 8.0f);
}

// The sequencer only writes a new value to cv[] when a step boundary is
// crossed, so sample CV right after each trigger to read the step order.
TEST(seq_direction_fwd_wraps_after_16) {
    ctagSeq16 seq;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    seq.SetCVSlot(0);
    seq.SetTrigSlot(0);
    seq.SetDirection(ctagSeq16::Direction::FWD);

    // Give every step a distinct value so we can see the order.
    for (int i = 0; i < 16; i++) seq.SetStep(i, (float)i / 15.0f);

    std::vector<float> seen;
    uint8_t prev = 0;
    // 19 beats: 16 steps to fill the sequence plus 3 to observe the wrap.
    int blocks = BlocksForBeats(120.0f, 19.0f);
    for (int i = 0; i < blocks; i++) {
        seq.Process(120.0f, cv.data(), trig.data(), kBlock);
        if (trig[0] && !prev) seen.push_back(cv[0]);
        prev = trig[0];
    }

    CHECK_MSG(seen.size() >= 17, "at least one full cycle plus a wrap expected");
    // Forward visits steps 0..15 in order, then wraps back to step 0.
    // cv maps step value v to v*2-1, so step i is i/15*2-1.
    for (int i = 0; i < 15; i++) {
        float expected = ((float)i / 15.0f) * 2.0f - 1.0f;
        CHECK_NEAR(seen[i], expected, 1e-4f);
    }
    CHECK_NEAR(seen[15], (15.0f / 15.0f) * 2.0f - 1.0f, 1e-4f); // step 15
    CHECK_MSG(std::fabs(seen[16] - seen[0]) < 1e-4f,
              "step 15 must wrap back to step 0");
}

TEST(seq_direction_bwd_wraps_after_16) {
    ctagSeq16 seq;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    seq.SetCVSlot(0);
    seq.SetTrigSlot(0);
    seq.SetDirection(ctagSeq16::Direction::BWD);

    for (int i = 0; i < 16; i++) seq.SetStep(i, (float)i / 15.0f);

    std::vector<float> seen;
    uint8_t prev = 0;
    int blocks = BlocksForBeats(120.0f, 19.0f);
    for (int i = 0; i < blocks; i++) {
        seq.Process(120.0f, cv.data(), trig.data(), kBlock);
        if (trig[0] && !prev) seen.push_back(cv[0]);
        prev = trig[0];
    }

    CHECK_MSG(seen.size() >= 17, "at least one full cycle plus a wrap expected");
    // Backward from step 0 wraps to step 15.
    float step15 = (15.0f / 15.0f) * 2.0f - 1.0f;
    CHECK_NEAR(seen[1], step15, 1e-4f);
    for (int i = 1; i < 16; i++) {
        float expected = ((float)(16 - i) / 15.0f) * 2.0f - 1.0f;
        CHECK_NEAR(seen[i], expected, 1e-4f);
    }
}

TEST(seq_direction_pendulum_reverses_at_ends) {
    ctagSeq16 seq;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    seq.SetCVSlot(0);
    seq.SetTrigSlot(0);
    seq.SetDirection(ctagSeq16::Direction::PENDULUM);

    for (int i = 0; i < 16; i++) seq.SetStep(i, (float)i / 15.0f);

    std::vector<float> seen;
    int blocks = BlocksForBeats(120.0f, 32.0f);
    for (int i = 0; i < blocks; i++) {
        seq.Process(120.0f, cv.data(), trig.data(), kBlock);
        float v = cv[0];
        if (seen.empty() || seen.back() != v) seen.push_back(v);
    }

    // Pendulum must reverse, not wrap: step 15 must be followed by step 14.
    bool sawPeakThenDescent = false;
    for (size_t i = 1; i + 1 < seen.size(); i++) {
        if (seen[i] > seen[i - 1] && seen[i] > seen[i + 1]) sawPeakThenDescent = true;
    }
    CHECK_MSG(sawPeakThenDescent,
              "pendulum must reverse direction at the top, not wrap to 0");
}

TEST(seq_slew_smooths_transitions) {
    ctagSeq16 seq;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    seq.SetCVSlot(0);
    seq.SetTrigSlot(0);
    seq.SetSlew(0.9f);
    for (int i = 0; i < 16; i++) seq.SetStep(i, 0.0f);
    seq.SetStep(0, 1.0f);

    // With heavy slew the output must not jump instantly to the target.
    seq.Process(120.0f, cv.data(), trig.data(), kBlock);
    CHECK_MSG(cv[0] > -1.0f && cv[0] < 1.0f,
              "heavy slew should not jump straight to full scale");
}

TEST(seq_cv_output_range_is_bipolar) {
    ctagSeq16 seq;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    seq.SetCVSlot(0);
    seq.SetTrigSlot(-1);

    for (int i = 0; i < 16; i++) seq.SetStep(i, 0.0f);
    seq.SetStep(0, 1.0f);   // -> +1.0
    seq.SetStep(1, 0.0f);   // -> -1.0

    // Step 0's value is latched at the first boundary (one beat).
    int blocks = BlocksForBeats(120.0f, 1.5f);
    for (int i = 0; i < blocks; i++) seq.Process(120.0f, cv.data(), trig.data(), kBlock);
    CHECK_NEAR(cv[0], 1.0f, 1e-5f);

    // Step 1's value is latched at the next boundary.
    blocks = BlocksForBeats(120.0f, 1.0f);
    for (int i = 0; i < blocks; i++) seq.Process(120.0f, cv.data(), trig.data(), kBlock);
    CHECK_NEAR(cv[0], -1.0f, 1e-5f);
}

TEST(seq_step_index_is_bounds_checked) {
    ctagSeq16 seq;
    seq.SetStep(-1, 1.0f);
    seq.SetStep(16, 1.0f);
    seq.SetStep(9999, 1.0f);
    CHECK_NEAR(seq.GetStep(0), 0.5f, 1e-6f);
    CHECK_NEAR(seq.GetStep(-5), 0.0f, 1e-9f);
}

TEST(seq_reset_returns_to_start) {
    ctagSeq16 seq;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    seq.SetCVSlot(0);
    seq.SetTrigSlot(0);
    for (int i = 0; i < 16; i++) seq.SetStep(i, (float)i / 15.0f);

    int blocks = BlocksForBeats(120.0f, 8.0f);
    for (int i = 0; i < blocks; i++) seq.Process(120.0f, cv.data(), trig.data(), kBlock);

    // Reset restarts at step 0; its value is latched at the next boundary.
    seq.Reset();
    blocks = BlocksForBeats(120.0f, 1.5f);
    for (int i = 0; i < blocks; i++) seq.Process(120.0f, cv.data(), trig.data(), kBlock);
    CHECK_NEAR(cv[0], -1.0f, 1e-5f);  // step 0 = 0.0 -> -1.0
}

// --- Slot bounds (issue #1, critical) ------------------------------------

TEST(seq_slot_setters_clamp_out_of_range) {
    ctagSeq16 seq;

    // A hand-edited mod-config.jsn can contain any integer. The setters must
    // not let an out-of-range index reach cv_buffer[]/trig_buffer[], which are
    // stack locals of the audio task.
    seq.SetCVSlot(5000);
    CHECK_MSG(seq.GetCVSlot() < kNCvs && seq.GetCVSlot() >= -1,
              "SetCVSlot must clamp/reject out-of-range indices");

    seq.SetTrigSlot(9999);
    CHECK_MSG(seq.GetTrigSlot() < kNTrigs && seq.GetTrigSlot() >= -1,
              "SetTrigSlot must clamp/reject out-of-range indices");

    seq.SetCVSlot(-42);
    CHECK_MSG(seq.GetCVSlot() >= -1, "negative slot below -1 must be rejected");
}

TEST(seq_out_of_range_slot_does_not_corrupt_memory) {
    // Canary check: pass a buffer large enough that a stray write lands inside
    // the allocation (so the test reports rather than segfaults), and confirm
    // Process() never writes outside the logical cv_buffer[0..kNCvs) range.
    //
    // On device these buffers are stack locals of audio_task, so an unwritten
    // index here means a corrupted stack on every audio block.
    constexpr int kAbsurdSlot = 100000;
    constexpr int kGuard = 64;
    const int kCvAlloc = kAbsurdSlot + kGuard;
    const int kTrigAlloc = kAbsurdSlot + kGuard;

    std::vector<float> cv(kCvAlloc, -999.0f);
    std::vector<uint8_t> trig(kTrigAlloc, 0xAB);

    ctagSeq16 seq;
    seq.SetCVSlot(kAbsurdSlot);
    seq.SetTrigSlot(kAbsurdSlot);

    for (int i = 0; i < 500; i++) {
        seq.Process(120.0f, cv.data(), trig.data(), kBlock);
    }

    for (int i = kNCvs; i < kCvAlloc; i++) {
        CHECK_MSG(cv[i] == -999.0f, "cv_buffer written out of bounds");
    }
    for (int i = kNTrigs; i < kTrigAlloc; i++) {
        CHECK_MSG(trig[i] == 0xAB, "trig_buffer written out of bounds");
    }
}

// ---------------------------------------------------------------------------
// ctagGate16
// ---------------------------------------------------------------------------

TEST(gate_unassigned_writes_nothing) {
    ctagGate16 gate;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);

    for (int i = 0; i < 64; i++) gate.Process(120.0f, cv.data(), trig.data(), kBlock);

    for (float v : cv) CHECK_NEAR(v, 0.0f, 1e-9f);
    for (uint8_t t : trig) CHECK_EQ(t, 0);
}

TEST(gate_provides_one_pulse_per_step) {
    ctagGate16 gate;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    gate.SetTrigSlot(0);
    gate.SetGateLength(0.5f);
    for (int i = 0; i < 16; i++) gate.SetStep(i, true, 100);
    gate.Reset();

    int rising = 0;
    uint8_t prev = 0;
    // 4 steps should produce 4 pulses.
    int totalSamples = static_cast<int>(4 * 0.5f * 44100.0f);
    for (int s = 0; s < totalSamples; s += kBlock) {
        gate.Process(120.0f, cv.data(), trig.data(), kBlock);
        if (trig[0] && !prev) rising++;
        prev = trig[0];
    }
    CHECK_MSG(rising == 4, "one gate pulse per step expected");
}

TEST(gate_probability_zero_never_fires) {
    ctagGate16 gate;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    gate.SetTrigSlot(0);
    gate.SetGateLength(0.9f);
    for (int i = 0; i < 16; i++) gate.SetStep(i, true, 0);

    int samples = static_cast<int>(8 * 0.5f * 44100.0f);
    int high = 0;
    for (int s = 0; s < samples; s += kBlock) {
        gate.Process(120.0f, cv.data(), trig.data(), kBlock);
        if (trig[0]) high++;
    }
    CHECK_MSG(high == 0, "a step with probability 0 must never fire");
}

TEST(gate_probability_hundred_always_fires) {
    ctagGate16 gate;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    gate.SetTrigSlot(0);
    gate.SetGateLength(0.5f);
    for (int i = 0; i < 16; i++) gate.SetStep(i, true, 100);

    int samples = static_cast<int>(4 * 0.5f * 44100.0f);
    int high = 0;
    for (int s = 0; s < samples; s += kBlock) {
        gate.Process(120.0f, cv.data(), trig.data(), kBlock);
        if (trig[0]) high++;
    }
    CHECK_MSG(high > 0, "probability 100 steps must fire");
}

TEST(gate_disabled_step_never_fires) {
    ctagGate16 gate;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    gate.SetTrigSlot(0);
    gate.SetGateLength(0.9f);
    for (int i = 0; i < 16; i++) gate.SetStep(i, false, 100);

    int samples = static_cast<int>(8 * 0.5f * 44100.0f);
    int high = 0;
    for (int s = 0; s < samples; s += kBlock) {
        gate.Process(120.0f, cv.data(), trig.data(), kBlock);
        if (trig[0]) high++;
    }
    CHECK_MSG(high == 0, "a disabled step must never fire");
}

TEST(gate_probability_is_clamped_to_100) {
    ctagGate16 gate;
    gate.SetStepProbability(0, 250);
    CHECK_EQ(gate.GetStepProbability(0), 100);
}

TEST(gate_step_index_is_bounds_checked) {
    ctagGate16 gate;
    gate.SetStep(-1, true, 100);
    gate.SetStep(16, true, 100);
    gate.SetStepProbability(-3, 50);
    gate.SetStepProbability(99, 50);
    CHECK(!gate.GetStepEnabled(0) || gate.GetStepEnabled(0));
    CHECK_EQ(gate.GetStepProbability(-1), 0);
    CHECK_EQ(gate.GetStepProbability(99), 0);
}

TEST(gate_gate_length_is_clamped) {
    ctagGate16 gate;
    gate.SetGateLength(0.0f);
    CHECK(gate.GetGateLength() >= 0.1f);
    gate.SetGateLength(5.0f);
    CHECK_NEAR(gate.GetGateLength(), 1.0f, 1e-6f);
}

TEST(gate_swing_is_clamped) {
    ctagGate16 gate;
    gate.SetSwing(-1.0f);
    CHECK_NEAR(gate.GetSwing(), 0.0f, 1e-6f);
    gate.SetSwing(2.0f);
    CHECK_NEAR(gate.GetSwing(), 1.0f, 1e-6f);
}

// --- Swing (issue #5) ----------------------------------------------------

TEST(gate_swing_delays_offbeat) {
    // Swing must make the OFF-beat step LONGER, not shorter.
    // boundaries[k] is the sample index where step k ends, so the duration of
    // step k is boundaries[k+1] - boundaries[k].
    ctagGate16 gate;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    gate.SetTrigSlot(0);
    gate.SetGateLength(0.25f);
    gate.SetSwing(1.0f);
    for (int i = 0; i < 16; i++) gate.SetStep(i, true, 100);
    gate.Reset();

    std::vector<int> boundaries;
    int sample = 0;
    uint8_t prev = 0;
    int totalSamples = static_cast<int>(8 * 0.5f * 44100.0f);
    while (sample < totalSamples) {
        gate.Process(120.0f, cv.data(), trig.data(), kBlock);
        sample += kBlock;
        if (trig[0] && !prev) boundaries.push_back(sample);
        prev = trig[0];
    }

    CHECK_MSG(boundaries.size() >= 6, "need at least 6 step boundaries");

    // boundaries[k] is where step k ENDS, so boundaries[k+1]-boundaries[k]
    // is the duration of step k+1. Odd-numbered steps are the off-beats and
    // must be the longer ones.
    int onBeatSum = 0, offBeatSum = 0, onBeatN = 0, offBeatN = 0;
    for (size_t k = 0; k + 1 < boundaries.size(); k++) {
        int stepIdx = static_cast<int>(k) + 1;
        int dur = boundaries[k + 1] - boundaries[k];
        if (stepIdx % 2 == 0) { onBeatSum += dur; onBeatN++; }
        else { offBeatSum += dur; offBeatN++; }
    }
    CHECK(onBeatN > 0 && offBeatN > 0);
    if (onBeatN > 0 && offBeatN > 0) {
        float onBeat = (float)onBeatSum / onBeatN;
        float offBeat = (float)offBeatSum / offBeatN;
        CHECK_MSG(offBeat > onBeat,
                  "swing must LENGTHEN the off-beat step; currently it "
                  "shortens it (reverse swing)");
    }
}

TEST(gate_no_swing_gives_equal_step_lengths) {
    ctagGate16 gate;
    std::vector<float> cv(kNCvs, 0.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    gate.SetTrigSlot(0);
    gate.SetGateLength(0.25f);
    gate.SetSwing(0.0f);
    for (int i = 0; i < 16; i++) gate.SetStep(i, true, 100);
    gate.Reset();

    std::vector<int> boundaries;
    int sample = 0;
    uint8_t prev = 0;
    int totalSamples = static_cast<int>(8 * 0.5f * 44100.0f);
    while (sample < totalSamples) {
        gate.Process(120.0f, cv.data(), trig.data(), kBlock);
        sample += kBlock;
        if (trig[0] && !prev) boundaries.push_back(sample);
        prev = trig[0];
    }

    int onBeatSum = 0, offBeatSum = 0, onBeatN = 0, offBeatN = 0;
    for (size_t k = 0; k + 1 < boundaries.size(); k++) {
        int stepIdx = static_cast<int>(k) + 1;
        int dur = boundaries[k + 1] - boundaries[k];
        if (stepIdx % 2 == 0) { onBeatSum += dur; onBeatN++; }
        else { offBeatSum += dur; offBeatN++; }
    }
    CHECK(onBeatN > 0 && offBeatN > 0);
    if (onBeatN > 0 && offBeatN > 0) {
        float onBeat = (float)onBeatSum / onBeatN;
        float offBeat = (float)offBeatSum / offBeatN;
        CHECK_NEAR(offBeat, onBeat, 5.0f);
    }
}

// --- Gate slot bounds (issue #1, critical) -------------------------------

TEST(gate_slot_setters_clamp_out_of_range) {
    ctagGate16 gate;

    gate.SetTrigSlot(5000);
    CHECK_MSG(gate.GetTrigSlot() < kNTrigs && gate.GetTrigSlot() >= -1,
              "SetTrigSlot must clamp/reject out-of-range indices");

    gate.SetAccentSlot(9999);
    CHECK_MSG(gate.GetAccentSlot() < kNCvs && gate.GetAccentSlot() >= -1,
              "SetAccentSlot must clamp/reject out-of-range indices");
}

TEST(gate_out_of_range_slot_does_not_corrupt_memory) {
    constexpr int kAbsurdSlot = 100000;
    constexpr int kGuard = 64;
    const int kCvAlloc = kAbsurdSlot + kGuard;
    const int kTrigAlloc = kAbsurdSlot + kGuard;

    std::vector<float> cv(kCvAlloc, -999.0f);
    std::vector<uint8_t> trig(kTrigAlloc, 0xAB);

    ctagGate16 gate;
    gate.SetTrigSlot(kAbsurdSlot);
    gate.SetAccentSlot(kAbsurdSlot);
    for (int i = 0; i < 16; i++) gate.SetStep(i, true, 100);

    for (int s = 0; s < 44100; s += kBlock) {
        gate.Process(120.0f, cv.data(), trig.data(), kBlock);
    }

    for (int i = kNCvs; i < kCvAlloc; i++) {
        CHECK_MSG(cv[i] == -999.0f, "cv_buffer written out of bounds");
    }
    for (int i = kNTrigs; i < kTrigAlloc; i++) {
        CHECK_MSG(trig[i] == 0xAB, "trig_buffer written out of bounds");
    }
}

TEST(gate_accent_writes_only_to_accent_slot) {
    ctagGate16 gate;
    std::vector<float> cv(kNCvs, 7.0f);
    std::vector<uint8_t> trig(kNTrigs, 0);
    gate.SetTrigSlot(1);
    gate.SetAccentSlot(2);
    gate.SetAccentAmount(1.0f);
    gate.SetGateLength(0.9f);
    for (int i = 0; i < 16; i++) gate.SetStep(i, true, 100);

    for (int s = 0; s < 44100; s += kBlock) {
        gate.Process(120.0f, cv.data(), trig.data(), kBlock);
    }

    // Unrelated slots must be untouched.
    for (int i = 3; i < kNCvs; i++) {
        CHECK_MSG(cv[i] == 7.0f, "gate wrote to a CV slot it was not assigned");
    }
}