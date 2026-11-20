/*
 * Host unit tests for the VARG black-metal algorithm of the ARP modulators
 * (kArpAlgoVarg = 5). Harmonic content:
 * docs/Black Metal Chords, Scales, Harmonies, and Progressions.md.
 * VARG runs INSIDE the existing ARP family — no extra slots/page; its
 * outputs are the ARP's pitch CV (cv 104/105) and gate/accent/slide trigs.
 */
#include "test_framework.hpp"

#include "helpers/ctagSlotBounds.hpp"
#include "helpers/varg_chord.h"
#include "rings/dsp/string_synth_part.h"

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

TEST(varg_strum_direction_stamped) {
    // ArpBuildVarg stamps the grid-locked pendulum into ArpStep bit 3 for
    // the TBDings strum engine (helpers/strum_plan.h rule 4)
    ArpExt e{};
    e.st.varg.chordIdx = 0xFF;
    ArpSource s{};
    s.p[0] = 10; s.p[1] = 0; s.p[2] = 50; s.p[3] = 0;   // TRM16
    s.rng = 11;
    ArpBuildVarg(s, e);
    for (int st = 0; st < 16; ++st)
        CHECK(((e.steps[st].flags & 8u) != 0) == ((st & 1u) != 0));

    // TRM8 (8th grid): beats down, & offbeats up
    ArpExt e8{};
    e8.st.varg.chordIdx = 0xFF;
    ArpSource s8{};
    s8.p[0] = 10; s8.p[1] = 7; s8.p[2] = 50; s8.p[3] = 0;   // (7*20)/101 = 1
    s8.rng = 11;
    ArpBuildVarg(s8, e8);
    for (int st = 0; st < 16; st += 2) {
        CHECK((e8.steps[st].flags & 1u) != 0);
        CHECK(((e8.steps[st].flags & 8u) != 0) == ((st & 2u) != 0));
    }
}

TEST(varg_strum_cells_present) {
    // research §3 strum cells appended after the original 16 masks; hexes
    // pinned here so a stray nibble edit cannot drift (see test_strum_plan)
    CHECK(kVargMasks[16] == 0x8A2A);   // CAMP  D . D U . U D U
    CHECK(kVargMasks[17] == 0x8AA8);   // BACK  D . D U D U D .
    CHECK(kVargMasks[18] == 0x9292);   // DBEA fixed from 0x9090 (quarter hits)
    CHECK(kVargMasks[19] == 0x8A28);   // CRST  D - D U - D U
    CHECK(kVargMasks[20] == 0x9248);   // TRIP
    CHECK(kVargMasks[21] == 0x88D2);   // QTRIP
    CHECK(kVargMasks[22] == 0x8421);   // P516
    CHECK(kVargMasks[23] == 0x8420);   // WALTZ
    CHECK(kVargMaskCount == 25);
    CHECK((100 * kVargMaskCount) / 101 == kVargMaskCount - 1);
    CHECK((70 * kVargMaskCount) / 101 == 17);
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
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(1, 99);   // clamped to 20
    CHECK(CTAG::DRIVERS::SeqEngine::GetArpAlgo(1) == 20);
}

// ---- VARG Chord: polyphonic block chords (algo 6) ----

TEST(varg_voicing_cv_roundtrip) {
    // The optional `vargChord` CV override carries voicing/(N-1); the wrapper
    // decodes with lround(cv*(N-1)). Every voicing index must survive that path.
    for (int v = 0; v < kVargVoicingCount; ++v) {
        const float cv = (float)v / (float)(kVargVoicingCount - 1);
        CHECK(lroundf(cv * (float)(kVargVoicingCount - 1)) == v);
    }
}

TEST(varg_voicing_map_covers_every_quality) {
    CHECK(kVargToVoicing[VQ_P5]   == 17);  // explicit 1-5-1 power chord
    CHECK(kVargToVoicing[VQ_MAJ]  == 10);  // existing "M"
    CHECK(kVargToVoicing[VQ_MIN]  == 3);   // existing "m"
    for (uint8_t q = 0; q < VQ_COUNT; ++q)
        CHECK(kVargToVoicing[q] < 18);
    for (uint8_t a = 0; a < VQ_COUNT; ++a)
        for (uint8_t b = (uint8_t)(a + 1); b < VQ_COUNT; ++b)
            CHECK(kVargToVoicing[a] != kVargToVoicing[b]);
}

TEST(varg_chord_algo_encoding) {
    CTAG::DRIVERS::SeqEngine::Init();
    for (int v = 18; v <= 20; ++v) {
        CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, v);
        CHECK(CTAG::DRIVERS::SeqEngine::GetArpAlgo(0) == v);
    }
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 99);
    CHECK(CTAG::DRIVERS::SeqEngine::GetArpAlgo(0) == 20);
}

TEST(varg_chord_cv_carries_packed_chord) {
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 18);   // VARG Chord, range 1
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 0, 10);   // i-bVII
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 1, 0);    // TRM16
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 2, 50);   // TRIAD
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 3, 0);    // no mutation
    uint8_t trig[51]; float cv[106];
    memset(trig, 1, sizeof(trig));
    memset(cv, 0, sizeof(cv));
    for (int b = 0; b < 64; ++b) {
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
        if (trig[45] == 0) break;
    }
    // Chord root is a normal 0.2 V/oct pitch: at the default root (0) and
    // first-bar tonic (off 0) the CV must be 0 V. Voicing is NOT on a CV
    // slot — it travels as an internal companion (GetVargVoicing).
    CHECK(fabsf(cv[104]) < 1e-5f);
    // The companion carries the Rings VOICING ROW, not the raw quality, so
    // TBDings plays the exact progression chord (i-bVII tonic = minor -> row 3).
    CHECK(CTAG::DRIVERS::SeqEngine::GetVargVoicing(0) == kVargToVoicing[VQ_MIN]);
}

TEST(varg_chord_companion_is_voicing_row_not_quality) {
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 18);   // VARG Chord range 1
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 0, 10);   // i-bVII
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 1, 0);    // TRM16
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 2, 0);    // RAW bias -> VQ_P5 (quality 0)
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 3, 0);    // no mutation
    uint8_t trig[51]; float cv[106];
    memset(trig, 1, sizeof(trig));
    memset(cv, 0, sizeof(cv));
    for (int b = 0; b < 64; ++b) {
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
        if (trig[45] == 0) break;
    }
    // RAW bias forces chord quality VQ_P5 (0). The companion must carry the
    // mapped Rings voicing ROW 1 ("5" power chord), not the raw quality 0
    // ("OCT"). A regression to returning curQual would play the wrong chord.
    CHECK(kVargToVoicing[VQ_P5] != (uint8_t)VQ_P5);   // test is meaningful
    CHECK(CTAG::DRIVERS::SeqEngine::GetVargVoicing(0) == kVargToVoicing[VQ_P5]);
}

TEST(varg_chord_gates_follow_mask) {
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 18);
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 1, 15);   // DBL 0xF000
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 3, 0);
    uint8_t trig[51]; float cv[106];
    memset(trig, 1, sizeof(trig)); memset(cv, 0, sizeof(cv));
    int gated = 0;
    for (int b = 0; b < 2100; ++b) {
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
        if (trig[45] == 0) ++gated;
    }
    CHECK(gated > 0);
}

// ---- OCTAVE TRANSPOSE (all note-generating ARP algorithms) ----

// Run one first-gated step with a fixed algo/octave and return its pitch CV.
static float arpFirstGateCv(int algoValue, int octave) {
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, algoValue);
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 0, 100);   // density (no silent algo)
    CTAG::DRIVERS::SeqEngine::SetArpOct(0, octave);
    uint8_t trig[51]; float cv[106];
    memset(trig, 1, sizeof(trig));
    memset(cv, 0, sizeof(cv));
    for (int b = 0; b < 2500; ++b) {
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
        if (trig[45] == 0) return cv[104];
    }
    return -1000.0f;
}

TEST(arp_octave_setter_clamps) {
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetArpOct(0, 99);
    CHECK(CTAG::DRIVERS::SeqEngine::GetArpOct(0) == 2);
    CTAG::DRIVERS::SeqEngine::SetArpOct(0, -99);
    CHECK(CTAG::DRIVERS::SeqEngine::GetArpOct(0) == -2);
    CTAG::DRIVERS::SeqEngine::SetArpOct(0, 0);
    CHECK(CTAG::DRIVERS::SeqEngine::GetArpOct(0) == 0);
}

TEST(arp_octave_transposes_every_generator) {
    // one encoded value per algorithm: Acid, Motif, Markov, Metro,
    // Deja-Vu, VARG, VARG Chord
    const int algos[7] = {0, 3, 6, 9, 12, 15, 18};
    for (int a = 0; a < 7; ++a) {
        const float base = arpFirstGateCv(algos[a], 0);
        CHECK(base > -1000.0f);
        const float up1 = arpFirstGateCv(algos[a], 1);
        const float down2 = arpFirstGateCv(algos[a], -2);
        // one octave = exactly 0.2 V on the pitch CV, every generator
        CHECK(fabsf(up1 - (base + 0.2f)) < 1e-5f);
        CHECK(fabsf(down2 - (base - 0.4f)) < 1e-5f);
    }
}

TEST(arp_octave_config_roundtrip) {
    char buf[1024];
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetArpOct(1, -2);
    CTAG::DRIVERS::SeqEngine::SerializeConfig(buf, sizeof(buf));
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::DeserializeConfig(buf);
    CHECK(CTAG::DRIVERS::SeqEngine::GetArpOct(1) == -2);
    // a config predating the key leaves the default untouched
    CTAG::DRIVERS::SeqEngine::Init();
    CHECK(CTAG::DRIVERS::SeqEngine::GetArpOct(0) == 0);
}

#include "rings/dsp/string_synth_part.h"

TEST(varg_chord_voicings_keep_quality_identity) {
    using namespace rings;
    CHECK(kNumChords == 18);
    const uint8_t aug = kVargToVoicing[VQ_AUG];
    const uint8_t mb9 = kVargToVoicing[VQ_MB9];
    const float *a = ChordVoicing(4, aug);
    const float *m = ChordVoicing(4, mb9);
    CHECK(fabsf(a[0]) < 0.01f && fabsf(a[1] - 4.f) < 0.01f && fabsf(a[2] - 8.f) < 0.01f);
    CHECK(fabsf(m[0]) < 0.01f && fabsf(m[1] - 1.f) < 0.01f && fabsf(m[2] - 7.f) < 0.01f);
    const uint8_t dim7 = kVargToVoicing[VQ_DIM7];
    const uint8_t m7b5 = kVargToVoicing[VQ_M7B5];
    const uint8_t add9 = kVargToVoicing[VQ_ADD9];
    CHECK(fabsf(ChordVoicing(4, dim7)[2] - 6.f) < 0.01f);
    CHECK(fabsf(ChordVoicing(4, m7b5)[2] - 6.f) < 0.01f);
    CHECK(fabsf(ChordVoicing(4, add9)[2] - 7.f) < 0.01f);
    // RAW quality is the explicit 1-5-1 power chord (root / fifth / octave).
    const uint8_t p5 = kVargToVoicing[VQ_P5];
    CHECK(p5 == 17);
    const float *pw = ChordVoicing(4, p5);
    CHECK(fabsf(pw[0]) < 0.01f && fabsf(pw[1] - 7.f) < 0.01f && fabsf(pw[2] - 12.f) < 0.01f);
    CHECK(ChordSize(1) == 8); CHECK(ChordSize(2) == 6); CHECK(ChordSize(4) == 3);
    for (int p = 1; p <= 4; ++p)
        for (uint8_t q = 0; q < VQ_COUNT; ++q)
            CHECK(ChordVoicing(p, kVargToVoicing[q]) != nullptr);
}



// ---- per-note guitar string index companion ----

TEST(varg_string_index_uses_all_three_fretted_strings) {
    // ArpBuildVarg stamps the gated-step ordinal mod 4 into every step.
    ArpSource src{};
    ArpExt ext{};
    src.algo = kArpAlgoVarg;
    src.range = 1;
    src.p[0] = 1;   // i-bVII
    src.p[1] = 0;   // TRM16 (all steps gated)
    src.p[2] = 50;  // TRIAD
    src.p[3] = 0;
    ArpBuildVarg(src, ext, true);
    bool seen[3] = {false, false, false};
    uint8_t gated = 0;
    for (uint8_t st = 0; st < 16; ++st) {
        CHECK(ext.steps[st].str < 3);
        if (ext.steps[st].flags & 1u) {
            seen[ext.steps[st].str] = true;
            ++gated;
        }
    }
    CHECK(gated == 16);
    for (int s = 0; s < 3; ++s) CHECK(seen[s]);
    // determinism across rebuilds
    ArpExt ext2{};
    ArpBuildVarg(src, ext2, true);
    for (uint8_t st = 0; st < 16; ++st)
        CHECK(ext.steps[st].str == ext2.steps[st].str);
}

TEST(varg_string_companion_for_both_algos) {
    CTAG::DRIVERS::SeqEngine::Init();
    uint8_t trig[51]; float cv[106];
    memset(trig, 1, sizeof(trig));
    memset(cv, 0, sizeof(cv));
    // Non-VARG algo publishes nothing
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 0);
    CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
    CHECK(CTAG::DRIVERS::SeqEngine::GetVargString(0) == -1);

    for (int enc : {15, 18}) {   // algo 5 (note ostinato), algo 6 (chord)
        CTAG::DRIVERS::SeqEngine::Init();
        CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, enc);
        CTAG::DRIVERS::SeqEngine::SetArpP(0, 1, 0);   // TRM16
        CTAG::DRIVERS::SeqEngine::SetArpP(0, 3, 0);
        bool seen[3] = {false, false, false};
        for (int b = 0; b < 2600; ++b) {
            CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
            const int8_t s = CTAG::DRIVERS::SeqEngine::GetVargString(0);
            CHECK(s >= 0 && s < 3);
            if (s >= 0 && s < 3) seen[s] = true;
        }
        for (int s = 0; s < 3; ++s) CHECK(seen[s]);
    }
}

TEST(varg_voicing4_matches_rings_chord_table) {
    // The VARG burst must voice from the same table the string synth uses.
    for (int row = 0; row < CTAG::SEQ::kVargVoicingCount; ++row) {
        const float *r = rings::ChordVoicing(4, row);
        CHECK(r != nullptr);
        for (int i = 0; i < 3; ++i)
            CHECK(CTAG::SEQ::kVargVoicing3[row][i] == (int8_t)lroundf(r[i]));
    }
}

TEST(varg_ghost_transients_and_burst_velocity) {
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 18);
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 1, 5);   // choice 1 = TRM8 -> odd steps silent
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 3, 0);
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 4, 20);  // Pendulum
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 5, 50);
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 6, 100); // ghosts on every silent step
    CHECK(CTAG::DRIVERS::SeqEngine::GetArpP(0, 6) == 100);
    uint8_t trig[51]; float cv[106];
    memset(trig, 1, sizeof(trig)); memset(cv, 0, sizeof(cv));
    bool sawGhost = false, sawTransient = false;
    int velocities = 0;
    for (int b = 0; b < 2600; ++b) {
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
        const uint8_t g = CTAG::DRIVERS::SeqEngine::GetVargGhost(0);
        if (g == 1) sawGhost = true;
        if (g == 2) sawTransient = true;
        const uint8_t v = CTAG::DRIVERS::SeqEngine::GetVargBurstVel(0);
        if (v > 0 && v < 100) ++velocities;
    }
    CHECK(sawGhost);
    CHECK(sawTransient);
    CHECK(velocities > 0);   // velocity ladder / attenuation is delivered
}

TEST(varg_chord_strum_burst_emits_one_note_per_string) {
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 18);   // VARG Chord, range 1
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 0, 1);    // i-bVII
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 1, 0);    // TRM16
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 2, 50);   // TRIAD
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 3, 0);
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 4, 20);   // P5 style -> Pendulum
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 5, 50);   // P6 spread -> ~22 ms
    CHECK(CTAG::DRIVERS::SeqEngine::GetArpP(0, 4) == 20);
    uint8_t trig[51]; float cv[106];
    memset(trig, 1, sizeof(trig));
    memset(cv, 0, sizeof(cv));

    int edges = 0, burstBlocks = 0;
    bool lastGateLow = false;
    int stringsSeen[4] = {0, 0, 0, 0};
    int prevString = -1;
    int lowRun = 0;
    for (int b = 0; b < 2600; ++b) {
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
        const bool low = trig[45] == 0;
        if (CTAG::DRIVERS::SeqEngine::GetVargBurst(0)) ++burstBlocks;
        if (low && !lastGateLow) {
            ++edges;
            const int8_t s = CTAG::DRIVERS::SeqEngine::GetVargString(0);
            CHECK(s >= 0 && s < 3);
            if (s >= 0 && s < 3) ++stringsSeen[s];
            prevString = s;
        }
        if (low) ++lowRun; else lowRun = 0;
        if (lowRun > 1) CHECK(false);   // burst pulses are one block only
        lastGateLow = low;
    }
    CHECK(edges > 16);          // more edges than 16 pattern steps -> burst
    CHECK(burstBlocks > 0);
    for (int s = 0; s < 3; ++s) CHECK(stringsSeen[s] > 0);

    // P5 = 0 (Block) keeps the legacy single whole-chord gate per step.
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 18);
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 1, 0);
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 3, 0);
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 4, 0);
    int legacyEdges = 0;
    lastGateLow = false;
    for (int b = 0; b < 2600; ++b) {
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
        const bool low = trig[45] == 0;
        if (low && !lastGateLow) ++legacyEdges;
        CHECK(CTAG::DRIVERS::SeqEngine::GetVargBurst(0) == 0);
        lastGateLow = low;
    }
    // Same window and tempo: the burst must produce substantially more
    // note edges than the legacy single-gate-per-step behaviour.
    CHECK(legacyEdges < edges);
    CHECK(edges >= legacyEdges * 2);
}
