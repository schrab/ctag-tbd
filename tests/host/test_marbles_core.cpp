/*
 * Host unit tests for the Marbles random-modulator core.
 *
 * Compiles the real Mutable Instruments sources from the eurorack submodule
 * (the same .cc files the firmware builds) and drives them with the same
 * wiring MarblesEngine::Process() uses: size==1 blocks at 1378.125 Hz
 * (44100 / 32).
 */
#include "test_framework.hpp"

#include <cmath>
#include <vector>

#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "stmlib/utils/gate_flags.h"

#include "ctagSlotBounds.hpp"

using namespace marbles;

namespace {

constexpr float kSr = 44100.0f / 32.0f;   // 1378.125 Hz block tick
constexpr size_t kSize = 1;

const Ratio kYRatios[12] = {
    {1, 64}, {1, 48}, {1, 32}, {1, 24}, {1, 16}, {1, 12},
    {1, 8},  {1, 6},  {1, 4},  {1, 3},  {1, 2},  {1, 1},
};

// Mirrors MarblesEngine::Process() parameter wiring with fixed defaults.
struct Engine {
    RandomGenerator rng;
    RandomStream stream;
    TGenerator t;
    XYGenerator xy;

    float ramp_external = 0.f, ramp_master = 0.f, ramp_slave0 = 0.f, ramp_slave1 = 0.f;
    bool gates[2] = {false, false};
    float voltages[4] = {0.f, 0.f, 0.f, 0.f};
    float prev_master = 0.f;
    bool master_wrapped = false;

    void Init(uint32_t seed) {
        rng.Init(seed);
        stream.Init(&rng);
        t.Init(&stream, kSr);
        xy.Init(&stream, kSr);
        Scale s;
        s.InitMajor();
        for (int i = 0; i < 6; ++i) xy.LoadScale(i, s);
    }

    void SetDefaults() {
        t.set_model(T_GENERATOR_MODEL_COMPLEMENTARY_BERNOULLI);
        t.set_range(T_GENERATOR_RANGE_1X);
        t.set_rate(0.0f);
        t.set_bias(0.5f);
        t.set_jitter(0.0f);
        t.set_deja_vu(0.0f);
        t.set_length(8);
        t.set_pulse_width_mean(90 / 256.0f);
        t.set_pulse_width_std(0.0f);
    }

    // ext_level: external clock gate level for this tick (MIDI-clock edges).
    void Tick(bool use_ext_clock, bool ext_level) {
        static stmlib::GateFlags flags = stmlib::GATE_FLAG_LOW;
        flags = stmlib::ExtractGateFlags(flags, ext_level);

        Ramps ramps;
        ramps.external = &ramp_external;
        ramps.master = &ramp_master;
        ramps.slave[0] = &ramp_slave0;
        ramps.slave[1] = &ramp_slave1;

        bool reset = false;
        t.Process(use_ext_clock, &reset, &flags, ramps, gates, kSize);

        GroupSettings x, y;
        x.control_mode = CONTROL_MODE_IDENTICAL;
        x.voltage_range = VOLTAGE_RANGE_NARROW;
        x.register_mode = false;
        x.register_value = 0.0f;
        x.spread = 0.5f;
        x.bias = 0.5f;
        x.steps = 1.0f;          // fully quantized
        x.deja_vu = 0.0f;
        x.length = 8;
        x.ratio.p = 1; x.ratio.q = 1;
        x.scale_index = 0;       // major
        y = x;
        y.voltage_range = VOLTAGE_RANGE_FULL;
        y.ratio = kYRatios[11];

        ClockSource cs = CLOCK_SOURCE_INTERNAL_T1_T2_T3;
        xy.Process(cs, x, y, &reset, &flags, ramps, voltages, kSize);

        master_wrapped = ramp_master < prev_master;
        prev_master = ramp_master;
    }
};

} // namespace

TEST(SlotBoundsCoverMarblesOutputs) {
    CHECK_EQ(CTAG::SP::HELPERS::ClampCVSlot(98), 98);
    CHECK_EQ(CTAG::SP::HELPERS::ClampCVSlot(101), 101);
    CHECK_EQ(CTAG::SP::HELPERS::ClampCVSlot(102), -1);
    CHECK_EQ(CTAG::SP::HELPERS::ClampTrigSlot(40), 40);
    CHECK_EQ(CTAG::SP::HELPERS::ClampTrigSlot(42), 42);
    CHECK_EQ(CTAG::SP::HELPERS::ClampTrigSlot(43), -1);
}

TEST(DeterminismSameSeedSameSequence) {
    std::vector<bool> a, b;
    for (int run = 0; run < 2; run++) {
        Engine e;
        e.Init(0xC0FFEE);
        e.SetDefaults();
        std::vector<bool> &out = (run == 0) ? a : b;
        for (int i = 0; i < 4000; i++) {
            e.Tick(false, false);
            out.push_back(e.gates[0]);
            out.push_back(e.gates[1]);
            for (int v = 0; v < 4; v++) out.push_back(e.voltages[v] > 0.0f);
        }
    }
    CHECK_EQ(a.size(), b.size());
    CHECK(a == b);
}

TEST(InternalMasterClockRate) {
    // range 1x, rate 0 st -> base 2 Hz -> 2 wraps per second of block ticks.
    Engine e;
    e.Init(1);
    e.SetDefaults();
    int wraps = 0;
    int ticks = (int)(10.0f * kSr);   // 10 seconds
    for (int i = 0; i < ticks; i++) {
        e.Tick(false, false);
        if (e.master_wrapped) wraps++;
    }
    // 10 s at 2 Hz = 20 wraps; the phase accumulator is exact per-tick so
    // allow only ±1 for float rounding.
    CHECK_MSG(wraps >= 19 && wraps <= 21,
              "expected ~20 master wraps in 10 s, got " + std::to_string(wraps));
}

TEST(ExternalClockExtraction) {
    // 24 PPQN-style clock: period 28 ticks -> ~49.2 Hz. With t rate = 0
    // (1:1), the master phase must wrap once per clock period after the
    // predictor settles.
    Engine e;
    e.Init(2);
    e.SetDefaults();
    const int period = 28;
    int wraps = 0;
    int ticks = period * 400;   // 400 clock periods
    for (int i = 0; i < ticks; i++) {
        bool level = (i % period) < 2;
        e.Tick(true, level);
        if (e.master_wrapped) wraps++;
    }
    // RampExtractor needs some periods to train; 400 periods leave plenty.
    CHECK_MSG(wraps >= 380 && wraps <= 420,
              "expected ~400 master wraps from external clock, got " +
                  std::to_string(wraps));
}

TEST(XQuantizedToMajorScale) {
    // steps=1 -> fully quantized. Narrow range maps to 0..2 V, and the
    // major scale has all 12 degrees -> every output must be a multiple
    // of 1/12 V.
    Engine e;
    e.Init(3);
    e.SetDefaults();
    int checked = 0;
    float prev[3] = {0, 0, 0};
    // 100k ticks = ~72 s = ~145 master wraps at the 2 Hz default clock.
    for (int i = 0; i < 100000; i++) {
        e.Tick(false, false);
        for (int c = 0; c < 3; c++) {
            if (std::fabs(e.voltages[c] - prev[c]) > 1e-9f) {
                float semitones = e.voltages[c] * 12.0f;
                float frac = std::fabs(semitones - std::round(semitones));
                CHECK_MSG(frac < 0.02f,
                          "X output off scale grid: " +
                              std::to_string(e.voltages[c]) + " V");
                if (frac >= 0.02f) return;
                checked++;
            }
            prev[c] = e.voltages[c];
        }
    }
    CHECK_MSG(checked > 50, "expected many X value changes, got " +
                                std::to_string(checked));
}

TEST(MarkovModelIsReachableAndVaries) {
    // The upstream-hidden Markov heuristic model: selectable and producing
    // varied (not stuck) decisions. Decisions happen on master wraps
    // (~2 Hz), so compare fired pulses against wrap count.
    Engine e;
    e.Init(4);
    e.SetDefaults();
    e.t.set_model(T_GENERATOR_MODEL_MARKOV);
    e.t.set_bias(0.5f);
    int wraps = 0, pulses0 = 0, pulses1 = 0;
    bool prevG0 = false, prevG1 = false;
    const int N = 200000;   // ~145 s -> ~290 master decisions
    for (int i = 0; i < N; i++) {
        e.Tick(false, false);
        if (e.gates[0] && !prevG0) pulses0++;
        if (e.gates[1] && !prevG1) pulses1++;
        prevG0 = e.gates[0];
        prevG1 = e.gates[1];
        if (e.master_wrapped) wraps++;
    }
    CHECK_MSG(wraps > 250, "expected >250 master wraps, got " + std::to_string(wraps));
    CHECK_MSG(pulses0 > wraps / 20 && pulses0 < wraps * 9 / 10,
              "t1 markov pulse rate out of range: " + std::to_string(pulses0) +
                  "/" + std::to_string(wraps));
    CHECK_MSG(pulses1 > wraps / 20 && pulses1 < wraps * 9 / 10,
              "t3 markov pulse rate out of range: " + std::to_string(pulses1) +
                  "/" + std::to_string(wraps));
}

TEST(DejaVuLoopRepeats) {
    // deja-vu amount exactly 0.5 with dv ON -> mutate probability 0 -> the
    // t section replays its loop buffer with period = length.
    Engine e;
    e.Init(5);
    e.SetDefaults();
    e.t.set_model(T_GENERATOR_MODEL_INDEPENDENT_BERNOULLI);
    e.t.set_deja_vu(0.5f);
    e.t.set_length(4);

    // collect gate bits at each master wrap for 4*length wraps
    std::vector<int> bits;
    while (bits.size() < 16) {
        e.Tick(false, false);
        if (e.master_wrapped) {
            bits.push_back((e.gates[0] ? 1 : 0) | (e.gates[1] ? 2 : 0));
        }
    }
    // The first cycle is a power-on transient (SlaveRamp's member-state
    // quirk on the very first decision); the loop locks from cycle 2 on.
    if (bits.size() >= 12) {
        for (size_t i = 8; i < bits.size(); i++) {
            if (bits[i] != bits[i - 4]) {
                CHECK_MSG(false, "deja-vu loop not repeating with period 4 at " +
                                     std::to_string(i));
                return;
            }
        }
    }
    CHECK(bits.size() >= 12);
}
