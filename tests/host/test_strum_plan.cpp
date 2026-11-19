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
