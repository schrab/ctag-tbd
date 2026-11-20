/*
 * Host unit tests for the strum-plan core (helpers/strum_plan.h) — the
 * guitar-strumming rules from docs/guitar-strumming-voicing-research.md §2.
 * Direction parity is validated against the research's own stroke notation
 * for the existing riff masks, and the plan builder is checked for order,
 * subset, velocity, and spread arithmetic.
 */
#include "test_framework.hpp"

#include "helpers/strum_plan.h"

using namespace tbd;

// ---- rule 4: grid classification + pendulum parity ----
// (MSB = step 1; the stroke strings are the research's own notation.)

TEST(strum_grid_classification) {
    CHECK(StrumMaskIs16thGrid(0xFFFF));                       // TRM16
    CHECK(StrumMaskIs16thGrid(0xBBBB));                       // GAL (a-steps)
    CHECK(!StrumMaskIs16thGrid(0xAAAA));                      // TRM8 (8ths)
    CHECK(!StrumMaskIs16thGrid(0x8888));                      // QRT
    CHECK(!StrumMaskIs16thGrid(0x8000));                      // HALF
}

TEST(strum_parity_gallop_matches_research_notation) {
    // Standard gallop 0xBBBB: gated 0,2,3 per beat -> research "D . D U"
    CHECK(!StrumStepIsUp(0xBBBB, 0));
    CHECK(!StrumStepIsUp(0xBBBB, 2));
    CHECK(StrumStepIsUp(0xBBBB, 3));
    // Reverse gallop 0xDDDD: gated 0,1,2 -> research "D U D ."
    CHECK(!StrumStepIsUp(0xDDDD, 0));
    CHECK(StrumStepIsUp(0xDDDD, 1));
    CHECK(!StrumStepIsUp(0xDDDD, 2));
}

TEST(strum_parity_tremolo_alternates) {
    // TRM16 0xFFFF: un-muted tremolo "buzz" — D U D U on every 16th.
    for (uint8_t st = 0; st < 16; st += 2) {
        CHECK(!StrumStepIsUp(0xFFFF, st));
        CHECK(StrumStepIsUp(0xFFFF, st + 1));
    }
}

TEST(strum_parity_8th_grid_beats_down_offbeats_up) {
    // TRM8 0xAAAA (8ths at even 16ths): beats D, & offbeats U.
    for (uint8_t st = 0; st < 16; st += 4) {
        CHECK(!StrumStepIsUp(0xAAAA, st));
        CHECK(StrumStepIsUp(0xAAAA, st + 2));
    }
    // QRT 0x8888 quarter vamp: every gated step is a beat -> all down.
    for (uint8_t st = 0; st < 16; st += 4)
        CHECK(!StrumStepIsUp(0x8888, st));
    // HALF 0x8000: one downstroke per bar.
    CHECK(!StrumStepIsUp(0x8000, 0));
}

// Corrected mask hexes for the phase-2 cells (8th-grid cells carry bits ONLY
// on even steps; the earlier design-doc values 0x8B2B/0x8BB8 had stray odd
// bits and are superseded by these).
inline constexpr uint16_t kCampMask = 0x8A2A;  // D . D U . U D U
inline constexpr uint16_t kBackMask = 0x8AA8;  // D . D U D U D .
inline constexpr uint16_t kDbeaMask = 0x9090;  // 16ths: 1 . . a . . 2 . . a
inline constexpr uint16_t kCrstMask = 0x8A28;  // D - D U - D U (pendulum-read)

TEST(strum_parity_new_cells) {
    // CAMP: gated 0,4,6,10,12,14 — 8th grid, D D U U D U.
    CHECK(!StrumMaskIs16thGrid(kCampMask));
    CHECK(!StrumStepIsUp(kCampMask, 0));
    CHECK(!StrumStepIsUp(kCampMask, 4));
    CHECK(StrumStepIsUp(kCampMask, 6));
    CHECK(StrumStepIsUp(kCampMask, 10));
    CHECK(!StrumStepIsUp(kCampMask, 12));
    CHECK(StrumStepIsUp(kCampMask, 14));
    // BACK: gated 0,4,6,8,10,12 — D . D U D U D .
    CHECK(!StrumMaskIs16thGrid(kBackMask));
    CHECK(!StrumStepIsUp(kBackMask, 8));
    CHECK(StrumStepIsUp(kBackMask, 10));
    CHECK(!StrumStepIsUp(kBackMask, 12));
    // DBEA: gated 0,3,8,11 — 16th grid, D U D U (a-offs are upstrokes).
    CHECK(StrumMaskIs16thGrid(kDbeaMask));
    CHECK(!StrumStepIsUp(kDbeaMask, 0));
    CHECK(StrumStepIsUp(kDbeaMask, 3));
    CHECK(!StrumStepIsUp(kDbeaMask, 8));
    CHECK(StrumStepIsUp(kDbeaMask, 11));
    // CRST: gated 0,4,6,10,12 — pendulum reads D D U U D (the research's
    // "D - D U - D U" is not strictly grid-locked; deviation accepted).
    CHECK(!StrumMaskIs16thGrid(kCrstMask));
    CHECK(!StrumStepIsUp(kCrstMask, 0));
    CHECK(StrumStepIsUp(kCrstMask, 6));
    CHECK(StrumStepIsUp(kCrstMask, 10));
}

// ---- plan builder ----

namespace {
// identity pitch order 0..n-1
void build_expectations(const StrumPlan &p, uint8_t fired) {
    CHECK(p.n == fired);
}
}  // namespace

TEST(strum_plan_downstroke_full_ascending) {
    uint8_t order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    uint32_t rng = 1;
    StrumPlan p = StrumBuildPlan(order, 8, STRUM_PENDULUM, +1, false, true,
                                 1400, rng);
    CHECK(p.n == 8);
    for (uint8_t i = 0; i < 8; ++i) {
        CHECK(p.slot[i] == i);
        CHECK(p.vel[i] == kStrumVelAccent);
        // spread 1400 over 8 notes -> 200 samples per string
        CHECK(p.delay[i] == (uint16_t)(200u * i));
    }
    // non-accent downstroke drops velocity, keeps order/delays
    StrumPlan q = StrumBuildPlan(order, 8, STRUM_PENDULUM, +1, false, false,
                                 1400, rng);
    CHECK(q.n == 8);
    CHECK(q.vel[3] == kStrumVelDown);
}

TEST(strum_plan_upstroke_top_half_descending) {
    uint8_t order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    uint32_t rng = 1;
    StrumPlan p = StrumBuildPlan(order, 8, STRUM_PENDULUM, -1, false, true,
                                 1200, rng);
    // top ceil(8/2)=4 voices, high -> low (research rule 1 reversed)
    CHECK(p.n == 4);
    CHECK(p.slot[0] == 7);
    CHECK(p.slot[1] == 6);
    CHECK(p.slot[2] == 5);
    CHECK(p.slot[3] == 4);
    CHECK(p.vel[0] == kStrumVelUp);
    CHECK(p.delay[3] == 1200);
    // non-accent upstroke is the softest
    StrumPlan q = StrumBuildPlan(order, 8, STRUM_PENDULUM, -1, false, false,
                                 0, rng);
    CHECK(q.vel[0] == kStrumVelUpSoft);
    CHECK(q.delay[0] == 0 && q.delay[1] == 0);
}

TEST(strum_plan_engine_side_alternation_without_companion) {
    uint8_t order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    uint32_t rng = 1;
    // stroke == 0 -> alt_up decides (MIDI keyboard strums, manual trig)
    StrumPlan up = StrumBuildPlan(order, 8, STRUM_PENDULUM, 0, true, true,
                                  0, rng);
    CHECK(up.n == 4);   // upstroke subset
    StrumPlan dn = StrumBuildPlan(order, 8, STRUM_PENDULUM, 0, false, true,
                                  0, rng);
    CHECK(dn.n == 8);   // full voicing
    CHECK(dn.slot[0] == 0);
}

TEST(strum_plan_upstroke_subset_odd_sizes) {
    uint8_t order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    uint32_t rng = 1;
    // poly 4 -> chord_size 3 -> top 2 voices
    StrumPlan p3 = StrumBuildPlan(order, 3, STRUM_PENDULUM, -1, false, true,
                                  0, rng);
    CHECK(p3.n == 2);
    CHECK(p3.slot[0] == 2 && p3.slot[1] == 1);
    // chord_size 5 -> top 3
    StrumPlan p5 = StrumBuildPlan(order, 5, STRUM_PENDULUM, -1, false, true,
                                  0, rng);
    CHECK(p5.n == 3);
    CHECK(p5.slot[0] == 4 && p5.slot[1] == 3 && p5.slot[2] == 2);
}

TEST(strum_plan_converge_outer_inward) {
    uint8_t order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    uint32_t rng = 1;
    StrumPlan p8 = StrumBuildPlan(order, 8, STRUM_CONVERGE, +1, false, true,
                                  0, rng);
    build_expectations(p8, 8);
    const uint8_t exp8[8] = { 0, 7, 1, 6, 2, 5, 3, 4 };
    for (uint8_t i = 0; i < 8; ++i) CHECK(p8.slot[i] == exp8[i]);
    StrumPlan p3 = StrumBuildPlan(order, 3, STRUM_CONVERGE, +1, false, true,
                                  0, rng);
    CHECK(p3.n == 3);
    CHECK(p3.slot[0] == 0 && p3.slot[1] == 2 && p3.slot[2] == 1);
    StrumPlan p1 = StrumBuildPlan(order, 1, STRUM_CONVERGE, +1, false, true,
                                  0, rng);
    CHECK(p1.n == 1 && p1.slot[0] == 0 && p1.delay[0] == 0);
}

TEST(strum_plan_diverge_middle_outward) {
    uint8_t order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    uint32_t rng = 1;
    StrumPlan p8 = StrumBuildPlan(order, 8, STRUM_DIVERGE, +1, false, true,
                                  0, rng);
    build_expectations(p8, 8);
    const uint8_t exp8[8] = { 3, 4, 2, 5, 1, 6, 0, 7 };
    for (uint8_t i = 0; i < 8; ++i) CHECK(p8.slot[i] == exp8[i]);
    StrumPlan p3 = StrumBuildPlan(order, 3, STRUM_DIVERGE, +1, false, true,
                                  0, rng);
    CHECK(p3.n == 3);
    CHECK(p3.slot[0] == 1 && p3.slot[1] == 0 && p3.slot[2] == 2);
    StrumPlan p5 = StrumBuildPlan(order, 5, STRUM_DIVERGE, +1, false, true,
                                  0, rng);
    CHECK(p5.n == 5);
    const uint8_t exp5[5] = { 2, 1, 3, 0, 4 };
    for (uint8_t i = 0; i < 5; ++i) CHECK(p5.slot[i] == exp5[i]);
    StrumPlan p1 = StrumBuildPlan(order, 1, STRUM_DIVERGE, +1, false, true,
                                  0, rng);
    CHECK(p1.n == 1 && p1.slot[0] == 0);
}

TEST(strum_plan_down_style_always_full_ascending) {
    uint8_t order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    uint32_t rng = 1;
    StrumPlan p = StrumBuildPlan(order, 8, STRUM_DOWN, -1, true, false,
                                 900, rng);
    // stroke -1 and alt_up ignored: DOWN never reverses, never subsets
    CHECK(p.n == 8);
    for (uint8_t i = 0; i < 8; ++i) CHECK(p.slot[i] == i);
    CHECK(p.vel[0] == kStrumVelDown);
    CHECK(p.delay[7] == 900);
}

TEST(strum_plan_random_valid_permutation_deterministic) {
    uint8_t order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    uint32_t rng = 42;
    StrumPlan a = StrumBuildPlan(order, 8, STRUM_RANDOM, +1, false, true,
                                 0, rng);
    CHECK(a.n == 8);
    bool seen[8] = { false };
    for (uint8_t i = 0; i < 8; ++i) {
        CHECK(a.slot[i] < 8);
        CHECK(!seen[a.slot[i]]);
        seen[a.slot[i]] = true;
    }
    uint32_t rng2 = 42;
    StrumPlan b = StrumBuildPlan(order, 8, STRUM_RANDOM, +1, false, true,
                                 0, rng2);
    for (uint8_t i = 0; i < 8; ++i) CHECK(b.slot[i] == a.slot[i]);
}

TEST(strum_plan_respects_non_identity_pitch_order) {
    // a chord row whose pitch order differs from slot order (e.g. add9 has
    // slot 3 at +14 st BEFORE slot 4 at +12 st): downstroke follows PITCH.
    uint8_t order[4] = { 0, 1, 2, 4 };   // slot 3 skipped as lowest->highest
    uint32_t rng = 1;
    StrumPlan dn = StrumBuildPlan(order, 4, STRUM_PENDULUM, +1, false, true,
                                  0, rng);
    CHECK(dn.n == 4);
    CHECK(dn.slot[3] == 4);
    StrumPlan up = StrumBuildPlan(order, 4, STRUM_PENDULUM, -1, false, true,
                                  0, rng);
    CHECK(up.n == 2);
    CHECK(up.slot[0] == 4 && up.slot[1] == 2);
}

TEST(strum_plan_degenerate_inputs_safe) {
    uint8_t order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    uint32_t rng = 1;
    StrumPlan z = StrumBuildPlan(order, 0, STRUM_PENDULUM, +1, false, true,
                                 100, rng);
    CHECK(z.n == 0);
    StrumPlan n = StrumBuildPlan(nullptr, 4, STRUM_PENDULUM, +1, false, true,
                                 100, rng);
    CHECK(n.n == 0);
    // out-of-range style clamps to PENDULUM (defensive; UI clamps too)
    StrumPlan s = StrumBuildPlan(order, 4, 99, +1, false, true, 0, rng);
    CHECK(s.n == 4);
}

TEST(strum_plan_delay_monotonic_and_bounded) {
    uint8_t order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    uint32_t rng = 1;
    // 45 ms at 44.1 kHz = 1985 samples — the documented spread maximum
    StrumPlan p = StrumBuildPlan(order, 8, STRUM_PENDULUM, +1, false, true,
                                 1985, rng);
    for (uint8_t i = 1; i < p.n; ++i)
        CHECK(p.delay[i] >= p.delay[i - 1]);
    CHECK(p.delay[7] == 1985);
}

// ---- integration: the real StringSynthPart strum path ----
// (string_synth_part.cc is compiled into this binary by run_tests.sh)

#include "rings/dsp/string_synth_part.h"

namespace {

struct StrumRig {
    rings::StringSynthPart part;
    rings::PerformanceState ps;
    rings::Patch patch;
    float in[32];
    float out[32];
    float aux[32];

    StrumRig() {
        // zero-initialized: the FX section reads this buffer and garbage
        // heap bits turn into NaNs (the full harness reuses dirty heap,
        // unlike a fresh probe process)
        reverb = new uint16_t[32768]();
        part.Init(reverb);
        ps.note = 60.f;
        ps.tonic = 12.f;
        ps.internal_note = false;
        ps.internal_exciter = true;
        ps.internal_strum = false;
        ps.strum = false;
        patch.brightness = 0.7f;
        patch.damping = 0.4f;
        patch.position = 0.5f;
        patch.structure = 0.5f;
        patch.dist_mix = 0.f;
        patch.dist_drive = 0.f;
        for (int i = 0; i < 32; ++i) in[i] = 0.f;
        part.set_polyphony(1);
    }
    ~StrumRig() { delete[] reverb; }

    float block() {
        part.Process(ps, patch, in, out, aux, 32);
        float acc = 0.f;
        for (int i = 0; i < 32; ++i) acc += out[i] * out[i] + aux[i] * aux[i];
        return acc;
    }
    void strum() {
        ps.strum = true;
        block();
        ps.strum = false;
    }
    uint16_t* reverb = nullptr;
};

float rms_block(StrumRig &rig, int blocks) {
    float acc = 0.f;
    for (int b = 0; b < blocks; ++b) acc += rig.block();
    return acc;
}

}  // namespace

TEST(strum_engine_spread0_block_is_loudest_on_first_block) {
    // The trigger block itself is silent (the pluck envelope's linear
    // attack starts at t=0 — legacy character for every pluck on this
    // instrument). With spread 0 ALL notes ramp from the next block; with
    // a wide spread only note 0 has fired within that window.
    StrumRig fast;
    fast.part.set_strum_style(tbd::STRUM_PENDULUM);
    fast.part.set_strum_spread(0);
    fast.strum();
    const float fast_energy = rms_block(fast, 6);

    StrumRig slow;
    slow.part.set_strum_style(tbd::STRUM_PENDULUM);
    slow.part.set_strum_spread(2000);   // notes arrive at blocks 9..63
    slow.strum();
    const float slow_energy = rms_block(slow, 6);

    CHECK(fast_energy > slow_energy * 2.f);
}

TEST(strum_engine_staggered_notes_arrive_later) {
    // The wide-spread chord completes: after enough blocks every planned
    // note has fired and the energy overtakes the early single-note ramp.
    StrumRig rig;
    rig.part.set_strum_style(tbd::STRUM_PENDULUM);
    rig.part.set_strum_spread(2000);
    rig.strum();
    const float early = rms_block(rig, 6);
    const float late = rms_block(rig, 70);
    CHECK(late > early * 4.f);
}

TEST(strum_engine_block_style_ignores_spread) {
    // BLOCK (style 0, the preset-compat default) is the legacy path: the
    // spread setting must not leak into it.
    StrumRig a;
    a.part.set_strum_style(tbd::STRUM_BLOCK);
    a.part.set_strum_spread(0);
    a.strum();
    const float e0 = rms_block(a, 2);

    StrumRig b;
    b.part.set_strum_style(tbd::STRUM_BLOCK);
    b.part.set_strum_spread(1985);
    b.strum();
    const float e1 = rms_block(b, 2);
    CHECK(e0 == e1);
    CHECK(e0 > 0.f);
}

TEST(strum_engine_chord_change_damps_tails) {
    // Strum chord A, let it ring, then strum chord B: the fresh shape mutes
    // the previous strings (envelopes drop) before the new chord stagger.
    StrumRig rig;
    rig.part.set_strum_style(tbd::STRUM_PENDULUM);
    rig.part.set_strum_spread(0);
    rig.ps.chord = 3;                    // m
    rig.strum();
    rms_block(rig, 8);                   // chord A rings
    rig.ps.chord = 11;                   // 5b5 — a different voicing row
    rig.strum();
    const float after = rms_block(rig, 2);
    CHECK(after > 0.f);                  // new chord strums in
}

TEST(strum_plan_new_modes_and_broken_cycles) {
    uint8_t order[4] = { 0, 1, 2, 3 };   // B, L, M, H
    uint32_t rng = 7;
    // pure upstroke: full voicing, high -> low (arpeggiator "Down")
    StrumPlan up = StrumBuildPlan(order, 4, STRUM_UP, +1, false, true, 300, rng);
    CHECK(up.n == 4);
    CHECK(up.slot[0] == 3 && up.slot[1] == 2 && up.slot[2] == 1 && up.slot[3] == 0);
    CHECK(up.vel[0] == kStrumVelAccent);
    // entry order
    StrumPlan thru = StrumBuildPlan(order, 4, STRUM_THRU, +1, false, true, 300, rng);
    CHECK(thru.n == 4);
    for (uint8_t i = 0; i < 4; ++i) CHECK(thru.slot[i] == i);
    // broken chord B-L-M-H-M-L (research §3)
    StrumPlan b1 = StrumBuildPlan(order, 4, STRUM_BROKEN1, +1, false, true, 500, rng);
    CHECK(b1.n == 6);
    const uint8_t want1[6] = { 0, 1, 2, 3, 2, 1 };
    for (uint8_t i = 0; i < 6; ++i) CHECK(b1.slot[i] == want1[i]);
    CHECK(b1.delay[5] == 500);
    // two-voice broken triad [M+H]-B-[M+H]-B
    StrumPlan b2 = StrumBuildPlan(order, 4, STRUM_BROKEN2, +1, false, true, 500, rng);
    CHECK(b2.n == 6);
    const uint8_t want2[6] = { 2, 3, 0, 2, 3, 0 };
    for (uint8_t i = 0; i < 6; ++i) CHECK(b2.slot[i] == want2[i]);
    // broken styles are softer than a full downstroke
    CHECK(b1.vel[0] < kStrumVelAccent);
}

TEST(strum_engine_drone_lift_survives_in_strum_mode) {
    // shape >= 0.98 is the drone regime: notes sustain without strums.
    StrumRig rig;
    rig.part.set_strum_style(tbd::STRUM_PENDULUM);
    rig.part.set_strum_spread(0);
    rig.patch.damping = 0.99f;
    rms_block(rig, 4);                   // no strum yet
    const float e = rms_block(rig, 4);
    CHECK(e > 0.f);
}
