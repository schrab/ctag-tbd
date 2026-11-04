/*
 * Host tests for the BBA2 Elements render ladder (handoff doc Finding 2):
 * the render cost depends on the resonator's EXCITATION state (a struck,
 * high-geometry patch costs ~220-230k cycles while decayed passages cost
 * ~140k, mode count barely matters), so a static mode count either wastes
 * budget or overruns at the peaks. The ladder watches the measured
 * part.Process cycles and steps the resolution DOWN one step after a run
 * of over-budget blocks (one-way per plugin instance, A1S V9 parity —
 * no flip-flopping; /debug/setElementsMode or a reload restores).
 */
#include "test_framework.hpp"
#include "render_ladder.hpp"

using CTAG::SP::RenderLadder;

TEST(ladder_stays_at_full_when_under_budget) {
    RenderLadder l; // steps {24, 20, 16}, starts at 0 (24)
    // 32 kHz budget 240k; 0.85 threshold = 204k. Decayed render ~140k.
    for (int i = 0; i < 100; ++i)
        CHECK_EQ(l.Update(140000u, 240000u), 24u);
}

TEST(ladder_steps_down_after_overrun_streak) {
    RenderLadder l;
    // Excited peaks ~220k > 204k threshold: 8 consecutive over-budget
    // blocks step 24 -> 20.
    for (int i = 0; i < 7; ++i)
        CHECK_EQ(l.Update(220000u, 240000u), 24u);
    CHECK_EQ(l.Update(220000u, 240000u), 20u);
    // Continue over: 20 -> 16.
    for (int i = 0; i < 7; ++i)
        CHECK_EQ(l.Update(220000u, 240000u), 20u);
    CHECK_EQ(l.Update(220000u, 240000u), 16u);
    // Floor: stays at 16 forever.
    for (int i = 0; i < 20; ++i)
        CHECK_EQ(l.Update(220000u, 240000u), 16u);
}

TEST(ladder_streak_resets_on_healthy_block) {
    RenderLadder l;
    for (int i = 0; i < 7; ++i)
        l.Update(220000u, 240000u);
    l.Update(140000u, 240000u); // one healthy block breaks the streak
    for (int i = 0; i < 7; ++i)
        CHECK_EQ(l.Update(220000u, 240000u), 24u);
    CHECK_EQ(l.Update(220000u, 240000u), 20u); // 8th fresh over block steps
}

TEST(ladder_uses_85_percent_threshold_not_raw_budget) {
    RenderLadder l;
    // 210k is under the raw 240k budget but over the 0.85 threshold
    // (204k): the loop overhead (~8k) plus Marbles must fit too.
    for (int i = 0; i < 7; ++i)
        l.Update(210000u, 240000u);
    CHECK_EQ(l.Update(210000u, 240000u), 20u);
}

TEST(ladder_44k_budget_scales) {
    RenderLadder l; // 44.1 kHz budget 174,150; threshold 148,027
    for (int i = 0; i < 7; ++i)
        l.Update(160000u, 174150u);
    CHECK_EQ(l.Update(160000u, 174150u), 20u);
}
