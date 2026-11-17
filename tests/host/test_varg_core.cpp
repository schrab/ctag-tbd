/*
 * Host unit tests for the VARG black-metal algorithm of the ARP modulators
 * (kArpAlgoVarg = 5). Harmonic content:
 * docs/Black Metal Chords, Scales, Harmonies, and Progressions.md.
 * VARG runs INSIDE the existing ARP family — no extra slots/page; its
 * outputs are the ARP's pitch CV (cv 104/105) and gate/accent/slide trigs.
 */
#include "test_framework.hpp"

#include "helpers/ctagSlotBounds.hpp"

TEST(varg_slot_space_unchanged) {
    // VARG adds NO slots: the SEQ extension frontier is unchanged
    CHECK(CTAG::SP::HELPERS::ClampCVSlot(105) == 105);
    CHECK(CTAG::SP::HELPERS::ClampCVSlot(106) == -1);
    CHECK(CTAG::SP::HELPERS::ClampTrigSlot(50) == 50);
    CHECK(CTAG::SP::HELPERS::ClampTrigSlot(51) == -1);
}

// ---- core: tables + the algorithm itself ----

#include "SeqEngineCore.hpp"

using namespace CTAG::SEQ;

TEST(varg_tones_and_progressions_valid) {
    // every quality has a tone set starting at the root
    for (uint8_t q = 0; q < VQ_COUNT; ++q) {
        CHECK(kVargTones[q].n >= 2);
        CHECK(kVargTones[q].st[0] == 0);
        for (uint8_t i = 1; i < kVargTones[q].n; ++i)
            CHECK(kVargTones[q].st[i] > kVargTones[q].st[i - 1]);
    }
    // every progression: 2..6 chords, offsets within an octave, valid quality
    for (uint8_t p = 1; p < kVargProgCount; ++p) {
        const VargProgression &pr = kVargProgressions[p];
        CHECK(pr.len >= 2);
        CHECK(pr.len <= 6);
        for (uint8_t c = 0; c < pr.len; ++c) {
            CHECK(pr.chords[c].off <= 11);
            CHECK(pr.chords[c].qual < VQ_COUNT);
        }
    }
}

TEST(varg_progressions_match_doc_examples) {
    // Weitzmann M3 cycle at Eb (doc: Ebm - Cbm/Bm - Gm - Bb)
    {
        const VargProgression &p = kVargProgressions[4];
        CHECK(p.len == 4);
        CHECK(p.chords[0].off == 0 && p.chords[0].qual == VQ_MIN);   // Ebm
        CHECK(p.chords[1].off == 8 && p.chords[1].qual == VQ_MIN);   // B
        CHECK(p.chords[2].off == 3 && p.chords[2].qual == VQ_MIN);   // Gm
        CHECK(p.chords[3].off == 7 && p.chords[3].qual == VQ_MAJ);   // Bb
    }
    // add9/dim7/#vii ostinato at A (doc: Am(add9) - D#dim7 - G#m - A)
    {
        const VargProgression &p = kVargProgressions[5];
        CHECK(p.chords[0].off == 0 && p.chords[0].qual == VQ_ADD9);
        CHECK(p.chords[1].off == 6 && p.chords[1].qual == VQ_DIM7);
        CHECK(p.chords[2].off == 11 && p.chords[2].qual == VQ_MIN);
        CHECK(p.chords[3].off == 0 && p.chords[3].qual == VQ_MAJ);
    }
    // Freezing Moon pool (doc: E5 A5 A#5 G5 F5 D#5)
    {
        const VargProgression &p = kVargProgressions[8];
        const uint8_t pool[6] = {0, 5, 6, 3, 1, 11};
        CHECK(p.len == 6);
        for (int c = 0; c < 6; ++c) {
            CHECK(p.chords[c].off == pool[c]);
            CHECK(p.chords[c].qual == VQ_P5);
        }
    }
}

TEST(varg_bias_bands) {
    // RAW: everything collapses to power chords
    {
        ArpSource s{}; s.p[2] = 20;
        for (int i = 0; i < 4; ++i)
            CHECK(VargBiasQuality(s, (uint8_t)(VQ_MIN + (i & 3)), s.p[2]) == VQ_P5);
    }
    // TRIAD: each progression keeps its own quality law
    {
        ArpSource s{}; s.p[2] = 50;
        const uint8_t bases[4] = {VQ_MIN, VQ_MAJ, VQ_ADD9, VQ_P5};
        for (int i = 0; i < 4; ++i)
            CHECK(VargBiasQuality(s, bases[i], s.p[2]) == bases[i]);
    }
    // DISS at 100: a minor chord never stays minor; deterministic per seed
    {
        ArpSource a{}, b{};
        a.p[2] = 100; a.rng = 0xcafebabeu;
        b = a;
        for (int i = 0; i < 16; ++i) {
            const uint8_t qa = VargBiasQuality(a, VQ_MIN, 100);
            const uint8_t qb = VargBiasQuality(b, VQ_MIN, 100);
            CHECK(qa == qb);
            CHECK(qa == VQ_M7B5 || qa == VQ_DIM7 || qa == VQ_MB9);
        }
    }
}

TEST(varg_progression_advance_and_first_bar) {
    ArpExt e{};
    e.st.varg.chordIdx = 0xFF;                    // unloaded marker
    ArpSource s{};
    s.p[0] = 10;                                  // prog (10*11)/101 = 1 -> i-bVII
    s.p[1] = 0;                                   // TRM16
    s.p[2] = 50;                                  // TRIAD
    s.rng = 7;
    ArpBuildVarg(s, e);
    CHECK(e.st.varg.chordIdx == 0);               // first bar = tonic
    CHECK(e.st.varg.curOff == 0);
    CHECK(e.st.varg.curQual == VQ_MIN);
    ArpBuildVarg(s, e);
    CHECK(e.st.varg.chordIdx == 1);               // i -> bVII
    CHECK(e.st.varg.curOff == 10);
    CHECK(e.st.varg.curQual == VQ_MAJ);
    ArpBuildVarg(s, e);
    CHECK(e.st.varg.chordIdx == 0);               // wraps back to i
    CHECK(e.st.varg.curOff == 0);
}

TEST(varg_pattern_gates_and_tones) {
    ArpExt e{};
    e.st.varg.chordIdx = 0xFF;
    ArpSource s{};
    s.p[0] = 10;      // i-bVII
    s.p[1] = 0;       // TRM16: every step gated
    s.p[2] = 50;      // TRIAD
    s.p[3] = 0;       // no mutation
    s.range = 1;
    s.rng = 11;
    ArpBuildVarg(s, e);
    // all 16 steps gated; accents on the quarters
    for (int st = 0; st < 16; ++st) {
        CHECK((e.steps[st].flags & 1u) != 0);
        CHECK(((e.steps[st].flags & 2u) != 0) == ((st & 3u) == 0));
        CHECK((e.steps[st].flags & 4u) == 0);     // no slides
        CHECK(e.steps[st].oct == 0);              // range 1
    }
    // MIN triad tones cycle 0,3,7 over the gated steps
    const uint8_t want[3] = {0, 3, 7};
    for (int st = 0; st < 16; ++st)
        CHECK(e.steps[st].off == want[st % 3]);
}

TEST(varg_range_octave_climb) {
    ArpExt e{};
    e.st.varg.chordIdx = 0xFF;
    ArpSource s{};
    s.p[0] = 10; s.p[1] = 0; s.p[2] = 50; s.p[3] = 0;
    s.range = 2;
    ArpBuildVarg(s, e);
    // second tone cycle moves up an octave; third cycle back down (range 2)
    CHECK(e.steps[0].oct == 0);    // k=0 -> cycle 0
    CHECK(e.steps[3].oct == 1);    // k=3 -> cycle 1
    CHECK(e.steps[6].oct == 0);    // k=6 -> cycle 2 % 2 = 0
}

TEST(varg_note_calc_semitone_exact) {
    // ArpCalcNote for VARG: 48 + root + curOff + step.off (semitones)
    ArpSource s{};
    s.algo = kArpAlgoVarg;
    s.root = 9;                       // A
    ArpExt e{};
    e.st.varg.curOff = 0;             // i
    ArpStep st{};
    st.off = 0;
    CHECK(ArpCalcNote(s, e, st) == 57);   // A3
    st.off = 3;
    CHECK(ArpCalcNote(s, e, st) == 60);   // C4 (minor third)
    st.off = 7;
    CHECK(ArpCalcNote(s, e, st) == 64);   // E4
    e.st.varg.curOff = 10;            // bVII = G
    st.off = 0;
    CHECK(ArpCalcNote(s, e, st) == 67);   // G4
    // octave climb folds within 28..96 (67+14+24 = 105 folds once to 93)
    st.off = 14;
    st.oct = 2;
    CHECK(ArpCalcNote(s, e, st) == 93);
}

TEST(varg_generative_markov_reachable_and_deterministic) {
    ArpExt a{}, b{};
    a.st.varg.chordIdx = 0xFF;
    b = a;
    ArpSource s{};
    s.p[0] = 0;        // generative
    s.p[2] = 50;
    s.rng = 0xabcdef01u;
    ArpSource t = s;
    uint32_t visited[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t prev = 0xFF;
    for (int i = 0; i < 2000; ++i) {
        ArpBuildVarg(s, a);
        ArpBuildVarg(t, b);
        CHECK(a.st.varg.chordIdx == b.st.varg.chordIdx);   // determinism
        CHECK(a.st.varg.curOff == b.st.varg.curOff);
        CHECK(a.st.varg.chordIdx != prev);                 // no self-repeat
        prev = a.st.varg.chordIdx;
        visited[a.st.varg.chordIdx]++;
    }
    for (int i = 0; i < 9; ++i) CHECK(visited[i] > 0);
}

TEST(varg_mutation_deterministic) {
    ArpExt e1{}, e2{};
    e1.st.varg.chordIdx = e2.st.varg.chordIdx = 0xFF;
    ArpSource s{};
    s.p[0] = 10; s.p[1] = 0; s.p[2] = 50; s.p[3] = 80;
    s.rng = 0x12345678u;
    ArpSource t = s;
    ArpBuildVarg(s, e1);
    ArpBuildVarg(t, e2);
    for (int st = 0; st < 16; ++st) CHECK(e1.steps[st].flags == e2.steps[st].flags);
    // repeated bars with the same seed keep producing identical bars
    ArpBuildVarg(s, e1);
    ArpSource u = t;
    ArpBuildVarg(u, e2);
    for (int st = 0; st < 16; ++st) CHECK(e1.steps[st].flags == e2.steps[st].flags);
}

TEST(varg_rebuild_pattern_dispatch) {
    ArpSource s{};
    s.algo = kArpAlgoVarg;
    s.p[0] = 10; s.p[1] = 0; s.p[2] = 50; s.p[3] = 0;
    s.range = 1;
    ArpExt e{};
    e.st.varg.chordIdx = 0xFF;
    ArpRebuildPattern(s, e);
    CHECK(e.st.varg.curOff == 0);
    CHECK(e.steps[0].off == kVargTones[VQ_MIN].st[0]);
}

// ---- shell integration: the arp emits the chord sequence ----

#define MODTRANSPORT_HOST 1
#include "ModTransport.hpp"
#include "SeqEngine.hpp"

TEST(varg_shell_chord_sequence_on_pitch_cv) {
    CTAG::DRIVERS::SeqEngine::Init();
    // ARP1 -> VARG, 1 octave; i-bVII; TRM16; TRIAD; no mutation
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 15);   // algo 5, range 1
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 0, 10);   // prog 1
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 1, 0);    // TRM16
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 2, 50);   // TRIAD
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 3, 0);    // no mutate
    CTAG::DRIVERS::SeqEngine::SetArpDiv(0, 7);     // 1/16
    CTAG::DRIVERS::SeqEngine::SetRoot(9);          // A
    CTAG::DRIVERS::SeqEngine::SetRootMode(0);
    uint8_t trig[51]; float cv[106];
    memset(trig, 1, sizeof(trig));
    memset(cv, 0, sizeof(cv));
    // 120 BPM internal, 1 ms blocks: 0.048 PPQ/block, 1/16 = 6 PPQ =
    // 125 blocks/step, 16 steps = 2000 blocks/bar. Run ~2.2 bars.
    bool seen[128] = {false};
    for (int b = 0; b < 4400; ++b) {
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
        const int note = (int)(cv[104] / 0.2f * 12.0f + 48.0f + 0.5f);
        if (note >= 0 && note < 128) seen[note] = true;
    }
    // bar 1 = A minor triad (A3/C4/E4), bar 2 = G major (G4/B4/D5)
    CHECK(seen[57]);   // A3
    CHECK(seen[60]);   // C4
    CHECK(seen[64]);   // E4
    CHECK(seen[67]);   // G4
    CHECK(seen[71]);   // B4
    CHECK(seen[74]);   // D5
}

TEST(varg_shell_gates_follow_mask) {
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 15);   // VARG range 1
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 1, 100);  // rhythm idx 15 = DBL 0xF000
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 3, 0);
    uint8_t trig[51]; float cv[106];
    memset(trig, 1, sizeof(trig));
    memset(cv, 0, sizeof(cv));
    int gated = 0;
    for (int b = 0; b < 2100; ++b) {   // a bit over one bar
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
        if (trig[45] == 0) ++gated;    // ARP1 gate trig open
    }
    CHECK(gated > 0);                  // the first four steps were gated
}

TEST(varg_shell_algo_encoding) {
    // GetArpAlgo/SetArpAlgo roundtrip for the 6th algorithm (v = algo*3+range-1)
    CTAG::DRIVERS::SeqEngine::Init();
    for (int v = 15; v <= 17; ++v) {
        CTAG::DRIVERS::SeqEngine::SetArpAlgo(1, v);
        CHECK(CTAG::DRIVERS::SeqEngine::GetArpAlgo(1) == v);
    }
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(1, 99);   // clamped to 17
    CHECK(CTAG::DRIVERS::SeqEngine::GetArpAlgo(1) == 17);
}
