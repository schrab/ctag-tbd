#include "test_framework.hpp"
#include "SeqEngineCore.hpp"

using namespace CTAG::SEQ;

namespace {
ArpSource VargSource(uint8_t algo = kArpAlgoVarg) {
    ArpSource s{};
    s.algo = algo;
    s.range = 1;
    s.p[0] = 10;  // i-bVII
    s.p[2] = 50;  // TRIAD
    s.rng = 0x12345678u;
    return s;
}

ArpExt UnloadedVarg() {
    ArpExt e{};
    e.st.varg.chordIdx = 0xFF;
    return e;
}
}

TEST(varg_order_raw_values_match_nine_panel_choices) {
    // Changing the generator's choice divisor must not change the order
    // heard for the same raw value displayed by the panel.
    const int8_t expected[9][6] = {
        {0, 3, 7, 0, 3, 7},  // Up
        {7, 3, 0, 7, 3, 0},  // Down
        {0, 3, 7, 7, 3, 0},  // Pendulum
        {0, 7, 3, 0, 7, 3},  // Converge
        {3, 7, 0, 3, 7, 0},  // Diverge
        {},                  // Random: checked separately below
        {0, 3, 7, 0, 3, 7},  // Thru
        {0, 3, 7, 7, 7, 3},  // Broken1
        {3, 7, 0, 3, 7, 0},  // Broken2
    };
    for (uint8_t algo : {kArpAlgoVarg, kArpAlgoVargChord}) {
        for (int raw = 0; raw <= 100; ++raw) {
            const int order = raw * 9 / 101;
            if (order == 5) continue;
            auto s = VargSource(algo);
            auto e = UnloadedVarg();
            s.p[5] = (uint8_t)raw;
            ArpRebuildPattern(s, e, false);
            for (int st = 0; st < 6; ++st)
                CHECK_EQ(e.steps[st].off, expected[order][st]);
        }
    }
}

TEST(varg_converge_repeats_complete_outside_in_cycles) {
    const uint8_t expected[5][5] = {
        {0}, {0, 1}, {0, 2, 1}, {0, 3, 1, 2}, {0, 4, 1, 3, 2}
    };
    uint32_t rng = 123;
    for (uint8_t n = 1; n <= 5; ++n)
        for (int k = 0; k < 4 * n; ++k)
            CHECK_EQ(VargOrderIndex(3, (uint8_t)k, n, rng), expected[n - 1][k % n]);
}

TEST(varg_diverge_repeats_complete_inside_out_cycles) {
    const uint8_t expected[5][5] = {
        {0}, {0, 1}, {1, 2, 0}, {1, 2, 0, 3}, {2, 3, 1, 4, 0}
    };
    uint32_t rng = 123;
    for (uint8_t n = 1; n <= 5; ++n)
        for (int k = 0; k < 4 * n; ++k)
            CHECK_EQ(VargOrderIndex(4, (uint8_t)k, n, rng), expected[n - 1][k % n]);
}

TEST(varg_random_uses_more_than_lcg_low_bits) {
    // Modulo the LCG's low bits produces strict alternation for n=2 and
    // periodic four-note walks for n=4, rather than random choices.
    for (uint8_t n : {2, 4}) {
        uint32_t rng = 0x12345678u;
        uint32_t seen = 0;
        bool repeated = false;
        uint8_t prev = 255;
        for (int k = 0; k < 64; ++k) {
            const uint8_t tone = VargOrderIndex(5, (uint8_t)k, n, rng);
            CHECK(tone < n);
            seen |= 1u << tone;
            repeated |= tone == prev;
            prev = tone;
        }
        CHECK_EQ(seen, (1u << n) - 1u);
        CHECK(repeated);
    }
}

TEST(varg_random_selects_only_on_gated_steps) {
    auto dense = VargSource();
    dense.p[5] = 57;  // Random in the nine-choice panel mapping
    auto denseExt = UnloadedVarg();
    ArpBuildVarg(dense, denseExt, false);

    auto sparse = VargSource();
    sparse.p[1] = 9;  // QRT: gates at 0, 4, 8, 12
    sparse.p[5] = 57;
    auto sparseExt = UnloadedVarg();
    ArpBuildVarg(sparse, sparseExt, false);
    for (int k = 0; k < 4; ++k) {
        CHECK((sparseExt.steps[k * 4].flags & 1u) != 0);
        CHECK_EQ(sparseExt.steps[k * 4].off, denseExt.steps[k].off);
    }
    uint32_t expectedRng = 0x12345678u;
    for (int k = 0; k < 4; ++k) VargOrderIndex(5, (uint8_t)k, 3, expectedRng);
    CHECK_EQ(sparse.rng, expectedRng);
}

TEST(varg_held_chord_keeps_resolved_dissonant_quality) {
    for (uint8_t algo : {kArpAlgoVarg, kArpAlgoVargChord}) {
        auto s = VargSource(algo);
        s.p[2] = 100;
        auto e = UnloadedVarg();
        ArpRebuildPattern(s, e, false);
        const uint8_t quality = e.st.varg.curQual;
        const uint32_t rng = s.rng;
        for (int bar = 0; bar < 16; ++bar) {
            ArpRebuildPattern(s, e, false);
            CHECK_EQ(e.st.varg.chordIdx, 0);
            CHECK_EQ(e.st.varg.curOff, 0);
            CHECK_EQ(e.st.varg.curQual, quality);
            CHECK_EQ(s.rng, rng);  // No quality lottery on held-bar rebuilds
        }
        ArpRebuildPattern(s, e, true);
        CHECK_EQ(e.st.varg.chordIdx, 1);
        CHECK_EQ(e.st.varg.curOff, 10);
        CHECK_EQ(e.st.varg.curQual, VQ_AUG);
    }
}

TEST(varg_false_rebuild_initializes_an_unloaded_chord) {
    auto s = VargSource();
    auto e = UnloadedVarg();
    ArpRebuildPattern(s, e, false);
    CHECK_EQ(e.st.varg.chordIdx, 0);
    CHECK_EQ(e.st.varg.curOff, 0);
    CHECK_EQ(e.st.varg.curQual, VQ_MIN);
    CHECK_EQ(e.steps[1].off, 3);
}

TEST(varg_explicit_revoice_changes_quality_without_advancing) {
    auto s = VargSource();
    auto e = UnloadedVarg();
    ArpRebuildPattern(s, e, true);
    ArpRebuildPattern(s, e, true);  // Step to bVII
    s.p[2] = 0;
    ArpRebuildPattern(s, e, false, true);
    CHECK_EQ(e.st.varg.chordIdx, 1);
    CHECK_EQ(e.st.varg.curOff, 10);
    CHECK_EQ(e.st.varg.curQual, VQ_P5);

    s.p[0] = 19;  // i-bVI-bVII, same index now points to bVI
    s.p[2] = 50;
    ArpRebuildPattern(s, e, false, true);
    CHECK_EQ(e.st.varg.chordIdx, 1);
    CHECK_EQ(e.st.varg.curOff, 8);
    CHECK_EQ(e.st.varg.curQual, VQ_MAJ);
}

TEST(varg_weitzmann_cycle_at_eb_plays_eb_b_g_bb) {
    auto s = VargSource();
    s.root = 3;    // Eb
    s.p[0] = 37;  // Weitzmann
    auto e = UnloadedVarg();
    const uint8_t expectedNotes[4] = {51, 59, 55, 58};
    const uint8_t expectedQualities[4] = {VQ_MIN, VQ_MIN, VQ_MIN, VQ_MAJ};
    for (int chord = 0; chord < 4; ++chord) {
        ArpBuildVarg(s, e);
        CHECK_EQ(ArpCalcNote(s, e, e.steps[0]), expectedNotes[chord]);
        CHECK_EQ(e.st.varg.curQual, expectedQualities[chord]);
    }
}
