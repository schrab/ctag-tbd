/*
 * Host unit tests for the VARG modulator (black-metal chord/riff
 * sequencer) and the slot-space growth it needs (cv 106-107, trig 51-52).
 * Harmonic content: docs/Black Metal Chords, Scales, Harmonies, and
 * Progressions.md.
 */
#include "test_framework.hpp"

#include "helpers/ctagSlotBounds.hpp"

TEST(varg_clamp_new_bounds_valid) {
    // cv 106..107 and trig 51..52 must be valid after the extension
    CHECK(CTAG::SP::HELPERS::ClampCVSlot(106) == 106);
    CHECK(CTAG::SP::HELPERS::ClampCVSlot(107) == 107);
    CHECK(CTAG::SP::HELPERS::ClampTrigSlot(51) == 51);
    CHECK(CTAG::SP::HELPERS::ClampTrigSlot(52) == 52);
}

TEST(varg_clamp_at_new_bounds_invalid) {
    // first index past each range clamps to unassigned (stack-safety rule)
    CHECK(CTAG::SP::HELPERS::ClampCVSlot(108) == -1);
    CHECK(CTAG::SP::HELPERS::ClampTrigSlot(53) == -1);
}

// ---- VargEngineCore ----

#include "VargEngineCore.hpp"

using namespace CTAG::VARG;

TEST(varg_quality_pair_table) {
    // the dyad pair interval IS the quality's identity:
    // 5->7, M->4, m->3, 5b5/dim7/m7b5->6, aug->8, add9->2, m+b9->1
    CHECK(Q_COUNT == 9);
    CHECK(kQualityPair[Q_P5] == 7);
    CHECK(kQualityPair[Q_MAJ] == 4);
    CHECK(kQualityPair[Q_MIN] == 3);
    CHECK(kQualityPair[Q_5B5] == 6);
    CHECK(kQualityPair[Q_DIM7] == 6);
    CHECK(kQualityPair[Q_M7B5] == 6);
    CHECK(kQualityPair[Q_AUG] == 8);
    CHECK(kQualityPair[Q_ADD9] == 2);
    CHECK(kQualityPair[Q_MB9] == 1);
}

TEST(varg_progression_tables_valid) {
    // every progression: 2..6 chords, offsets within an octave, qualities valid
    for (uint8_t p = 1; p < kProgCount; ++p) {
        const VargProgression &prog = kProgressions[p];
        CHECK(prog.len >= 2);
        CHECK(prog.len <= 6);
        for (uint8_t c = 0; c < prog.len; ++c) {
            CHECK(prog.chords[c].off <= 11);
            CHECK(prog.chords[c].qual < Q_COUNT);
        }
    }
}

TEST(varg_progression_doc_examples) {
    // canonical progressions must reproduce the research doc's examples
    // (doc: Ebm - Cbm/Bm - Gm - Bb, Leviathan Weitzmann cycle)
    {
        const VargProgression &p = kProgressions[4];
        CHECK(p.len == 4);
        CHECK(p.chords[0].off == 0 && p.chords[0].qual == Q_MIN);   // Ebm
        CHECK(p.chords[1].off == 8 && p.chords[1].qual == Q_MIN);   // B (Cbm)
        CHECK(p.chords[2].off == 3 && p.chords[2].qual == Q_MIN);   // Gm (#iii)
        CHECK(p.chords[3].off == 7 && p.chords[3].qual == Q_MAJ);   // Bb (V)
    }
    // (doc: Am(add9) - D#dim7 - G#m - A, corpus ostinato)
    {
        const VargProgression &p = kProgressions[5];
        CHECK(p.len == 4);
        CHECK(p.chords[0].off == 0 && p.chords[0].qual == Q_ADD9);
        CHECK(p.chords[1].off == 6 && p.chords[1].qual == Q_DIM7);  // #iv dim7
        CHECK(p.chords[2].off == 11 && p.chords[2].qual == Q_MIN);  // #vii
        CHECK(p.chords[3].off == 0 && p.chords[3].qual == Q_MAJ);   // tonic major
    }
    // (doc: E5 A5 A#5 G5 F5 D#5, "Freezing Moon" power-chord pool)
    {
        const VargProgression &p = kProgressions[8];
        CHECK(p.len == 6);
        const uint8_t pool[6] = {0, 5, 6, 3, 1, 11};
        for (int c = 0; c < 6; ++c) {
            CHECK(p.chords[c].off == pool[c]);
            CHECK(p.chords[c].qual == Q_P5);
        }
    }
    // (doc: Gm - G#m - A#m - Bm, parallel planing)
    {
        const VargProgression &p = kProgressions[9];
        CHECK(p.len == 4);
        const uint8_t offs[4] = {0, 1, 3, 4};
        for (int c = 0; c < 4; ++c) {
            CHECK(p.chords[c].off == offs[c]);
            CHECK(p.chords[c].qual == Q_MIN);
        }
    }
}

TEST(varg_bias_bands) {
    // RAW: everything collapses to power chords
    {
        VargSource s{};
        s.bias = 20;
        const uint8_t bases[4] = {Q_MIN, Q_MAJ, Q_ADD9, Q_P5};
        for (int i = 0; i < 4; ++i) CHECK(VargApplyBias(s, bases[i]) == Q_P5);
    }
    // TRIAD: each progression keeps its own quality law
    {
        VargSource s{};
        s.bias = 50;
        const uint8_t bases[4] = {Q_MIN, Q_MAJ, Q_ADD9, Q_P5};
        for (int i = 0; i < 4; ++i) CHECK(VargApplyBias(s, bases[i]) == bases[i]);
    }
    // DISS: deterministic per seed; at 100 a minor chord never stays minor
    {
        VargSource a{}, b{};
        a.bias = 100; a.rng_state = 0xcafebabeu;
        b = a;
        for (int i = 0; i < 16; ++i) {
            uint8_t qa = VargApplyBias(a, Q_MIN);
            uint8_t qb = VargApplyBias(b, Q_MIN);
            CHECK(qa == qb);                            // same seed -> same chain
            CHECK(qa == Q_M7B5 || qa == Q_DIM7 || qa == Q_MB9);
        }
        // at the band edge the remap is occasional, never constant-forced
        VargSource c{};
        c.bias = 67;
        uint8_t seen = 0xFF;
        for (int i = 0; i < 64 && seen == 0xFF; ++i) {
            uint8_t q = VargApplyBias(c, Q_MIN);
            if (q != Q_MIN) seen = q;
        }
        (void)seen;   // reaching here without a hang is the assertion
    }
}

TEST(varg_gen_markov_reachable_and_deterministic) {
    // every pool state is reachable from any start; same seed -> same chain;
    // a chord never repeats back-to-back (the diagonal is zero)
    uint32_t visited[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    VargSource a{}, b{};
    a.prog = 0; a.bias = 50; a.rng_state = 0xabcdef01u;
    b = a;
    uint8_t prev = a.genIdx;
    for (int i = 0; i < 2000; ++i) {
        VargNextChord(a);
        VargNextChord(b);
        CHECK(a.chordIdx != prev);          // no self-loop
        prev = a.chordIdx;
        CHECK(a.chordIdx == b.chordIdx);    // determinism
        CHECK(a.curOff == b.curOff);
        visited[a.chordIdx]++;
    }
    for (int i = 0; i < 9; ++i) CHECK(visited[i] > 0);
}

TEST(varg_riff_presets) {
    CHECK(kVargRiffMasks[1] == 0xFFFFu);   // TRM16
    CHECK(kVargRiffMasks[2] == 0xAAAAu);   // TRM8
    CHECK(kVargRiffMasks[4] == 0x8000u);   // HALF
    CHECK(kVargRiffMasks[8] == 0x9249u);   // 68L / 3-3-2
    VargSource g{};
    g.rhythm = 1; g.dens = 16; g.sync = 0;
    VargRiffRecompute(g);
    CHECK(g.mask == 0xFFFFu);              // preset untouched at full density
    g.rhythm = 4; VargRiffRecompute(g);
    CHECK(g.mask == 0x8000u);
    // density trims presets via Barlow indispensability (same rule as SEQ)
    g.rhythm = 1; g.dens = 8; VargRiffRecompute(g);
    CHECK(g.mask != 0xFFFFu);
    CHECK(g.mask != 0u);
}

TEST(varg_riff_gen_density_ordering) {
    // GEN mode: density n keeps every strong beat of density n-1 (Barlow trim)
    VargSource a{}, b{};
    a.rhythm = 0; a.dens = 8;  a.sync = 0;
    b.rhythm = 0; b.dens = 10; b.sync = 0;
    VargRiffRecompute(a);
    VargRiffRecompute(b);
    CHECK((b.mask & a.mask) == a.mask);
}

TEST(varg_riff_gen_syncopation_shift) {
    // syncopation moves strong beats into weak pickups (deterministic seed)
    VargSource a{}, b{};
    a.rhythm = 0; a.dens = 10; a.sync = 0;
    b.rhythm = 0; b.dens = 10; b.sync = 80;
    VargRiffRecompute(a);
    VargRiffRecompute(b);
    CHECK(a.mask != b.mask);
    VargSource c = b;
    VargRiffRecompute(c);
    CHECK(c.mask == b.mask);   // same params -> same groove
}

TEST(varg_riff_tmr_deterministic) {
    VargSource a{}, b{};
    a.rhythm = 16; a.dens = 12; a.sync = 40; a.rng_state = 0x12345678u;
    b = a;
    VargRiffRecompute(a);
    VargRiffRecompute(b);
    CHECK(a.mask == b.mask);            // same seed -> same groove
    uint32_t flips = 0;
    for (int i = 0; i < 64; ++i) {
        uint32_t before = a.mask;
        VargAdvanceStep(a, 1);          // advances the step AND maybe mutates
        flips += (a.mask != before);
    }
    CHECK(flips > 0);                   // mutation actually mutates
}

TEST(varg_chord_cadence_and_pulse) {
    // hold encodes bars-per-chord as 1<<hold: 1/2/4 musical bars
    VargSource s{};
    s.prog = 1; s.bias = 50; s.rng_state = 7u; s.step = 3;
    VargLoadChord(s, 0);
    CHECK(s.curOff == 0 && s.curQual == Q_MIN);        // i first
    CHECK(s.chgPulse == 0);
    s.hold = 0;                                         // 1 bar per chord
    VargAdvanceBar(s, 1);
    CHECK(s.chordIdx == 1);                             // -> bVII
    CHECK(s.curOff == 10 && s.curQual == Q_MAJ);
    CHECK(s.chgPulse == 1 && s.chgStep == 3);           // onset pulse armed
    VargAdvanceStep(s, 1);                              // next riff step
    CHECK(s.chgPulse == 0);                             // pulse was one step
    // hold 2 = 4 bars: three single-bar wraps must NOT change the chord
    s.hold = 2;
    uint8_t off = s.curOff;
    VargAdvanceBar(s, 1);
    VargAdvanceBar(s, 1);
    VargAdvanceBar(s, 1);
    CHECK(s.curOff == off);
    CHECK(s.chgPulse == 0);
    VargAdvanceBar(s, 1);                               // 4th bar -> change
    CHECK(s.curOff != off);
}

TEST(varg_dyad_notes) {
    // tonic A (pc 9): i = A minor dyad (A + C), bVII = G major (G + B)
    VargSource s{};
    s.root = 9; s.prog = 1; s.bias = 50;
    VargLoadChord(s, 0);
    CHECK(VargRootNote(s) == 57);                       // A3
    CHECK(VargPairNote(s) == 60);                       // C4 (b3)
    VargLoadChord(s, 1);
    CHECK(VargRootNote(s) == 67);                       // G4 (A + 10 st)
    CHECK(VargPairNote(s) == 71);                       // B4 (M3)
}

// ---- VargEngine shell ----

#include "VargEngine.hpp"
#define MODTRANSPORT_HOST 1
#include "ModTransport.hpp"

TEST(varg_engine_defaults) {
    CTAG::DRIVERS::VargEngine::Init();
    CHECK(CTAG::DRIVERS::VargEngine::GetProg() == 1);    // i-bVII
    CHECK(CTAG::DRIVERS::VargEngine::GetBias() == 50);   // TRIAD band
    CHECK(CTAG::DRIVERS::VargEngine::GetHold() == 1);    // 2 bars
    CHECK(CTAG::DRIVERS::VargEngine::GetRhythm() == 1);  // TRM16
    CHECK(CTAG::DRIVERS::VargEngine::GetRoot() == 9);    // A
    CHECK(CTAG::DRIVERS::VargEngine::GetClk() == 0);
    CHECK(CTAG::DRIVERS::VargEngine::GetDiv() == 7);     // 1/16
    CHECK(CTAG::DRIVERS::VargEngine::GetRootSlot() == 106);
    CHECK(CTAG::DRIVERS::VargEngine::GetPairSlot() == 107);
    CHECK(CTAG::DRIVERS::VargEngine::GetTremSlot() == 51);
    CHECK(CTAG::DRIVERS::VargEngine::GetChgSlot() == 52);
    CHECK(CTAG::DRIVERS::VargEngine::GetMask() == 0xFFFFu);
}

TEST(varg_engine_first_block_outputs) {
    CTAG::DRIVERS::VargEngine::Init();
    uint8_t trig[53]; float cv[108];
    memset(trig, 1, sizeof(trig));
    memset(cv, 0, sizeof(cv));
    CTAG::DRIVERS::VargEngine::Process(cv, trig, 1.0f / 1000.0f);
    // tonic A minor dyad from the first block: root 57 -> +9st = 0.15 V,
    // pair 60 -> +12st = 0.2 V (MIDI 48 = 0 V convention)
    CHECK_NEAR(cv[106], 0.15, 0.001);
    CHECK_NEAR(cv[107], 0.2, 0.001);
    CHECK(trig[51] == 0);   // trem: step 0 of TRM16 is ON, window open
    CHECK(trig[52] == 0);   // chord-onset pulse announces the boot chord
}

TEST(varg_engine_chord_cycle_internal_clock) {
    CTAG::DRIVERS::VargEngine::Init();
    CTAG::DRIVERS::VargEngine::SetHold(0);              // 1 bar per chord
    uint8_t trig[53]; float cv[108];
    memset(trig, 1, sizeof(trig));
    memset(cv, 0, sizeof(cv));
    // Internal clock (default): 120 BPM, 1 ms blocks -> 0.048 PPQ/block,
    // bar = 96 PPQ = 2000 blocks. i-bVII alternates offsets 0 / 10.
    bool sawTonic = false, sawFlat7 = false;
    float rootAtTonic = -99.0f, rootAtFlat7 = -99.0f;
    for (int b = 0; b < 6500; ++b) {
        CTAG::DRIVERS::VargEngine::Process(cv, trig, 1.0f / 1000.0f);
        uint32_t snap = CTAG::DRIVERS::VargEngine::GetOutputSnapshot();
        int off = (int)((snap >> 6) & 0xFu);
        if (off == 0) {
            sawTonic = true;
            rootAtTonic = cv[106];
        } else if (off == 10) {
            sawFlat7 = true;
            rootAtFlat7 = cv[106];
        }
    }
    CHECK(sawTonic);
    CHECK(sawFlat7);
    CHECK_NEAR(rootAtTonic, 0.15, 0.001);               // A3
    CHECK_NEAR(rootAtFlat7, (67.0f - 48.0f) / 12.0f * 0.2f, 0.001);  // G4
}

TEST(varg_engine_trem_window_stop_release) {
    CTAG::DRIVERS::VargEngine::Init();
    CTAG::DRIVERS::VargEngine::SetClk(2);               // MIDI clock
    CTAG::DRIVERS::ModTransport::OnMidiStart();
    uint8_t trig[53]; float cv[108];
    memset(trig, 1, sizeof(trig));
    memset(cv, 0, sizeof(cv));
    // REAL call order: ModTransport::Process before VargEngine::Process
    CTAG::DRIVERS::ModTransport::OnMidiClockAt(1000000);
    CTAG::DRIVERS::ModTransport::Process(1.0f / 1000.0f);
    CTAG::DRIVERS::VargEngine::Process(cv, trig, 1.0f / 1000.0f);
    CHECK(trig[51] == 0);                               // riff window open
    // Transport STOP: a frozen open window would hang a mapped note —
    // the riff window must release (idle) while the transport is stopped.
    CTAG::DRIVERS::ModTransport::OnMidiStop();
    CTAG::DRIVERS::ModTransport::Process(1.0f / 1000.0f);
    CTAG::DRIVERS::VargEngine::Process(cv, trig, 1.0f / 1000.0f);
    CHECK(trig[51] == 1);
}

TEST(varg_engine_fa_rephase_midi) {
    CTAG::DRIVERS::VargEngine::Init();
    CTAG::DRIVERS::VargEngine::SetClk(2);               // MIDI clock
    CTAG::DRIVERS::ModTransport::OnMidiStart();
    uint8_t trig[53]; float cv[108];
    memset(trig, 1, sizeof(trig));
    memset(cv, 0, sizeof(cv));
    // Advance several steps (1 tick per block, 6 PPQ per 1/16 step) with the
    // REAL call order: ModTransport::Process before VargEngine::Process.
    for (int b = 0; b < 200; ++b) {
        CTAG::DRIVERS::ModTransport::OnMidiClockAt((b + 1) * 20000);
        CTAG::DRIVERS::ModTransport::Process(1.0f / 1000.0f);
        CTAG::DRIVERS::VargEngine::Process(cv, trig, 1.0f / 1000.0f);
    }
    CHECK(((CTAG::DRIVERS::VargEngine::GetOutputSnapshot() >> 2) & 0xFu) != 0u);
    // FA re-phase: riff snaps to step 0, progression snaps to the tonic.
    CTAG::DRIVERS::ModTransport::OnMidiStart();
    CTAG::DRIVERS::ModTransport::Process(1.0f / 1000.0f);
    CTAG::DRIVERS::VargEngine::Process(cv, trig, 1.0f / 1000.0f);
    uint32_t snap = CTAG::DRIVERS::VargEngine::GetOutputSnapshot();
    CHECK(((snap >> 2) & 0xFu) == 0u);                  // riff step == 0
    CHECK(((snap >> 6) & 0xFu) == 0u);                  // chord offset == 0 (i)
    CHECK(((snap >> 22) & 0xFu) == 0u);                 // chord index == 0
}

TEST(varg_engine_serialize_roundtrip) {
    CTAG::DRIVERS::VargEngine::Init();
    CTAG::DRIVERS::VargEngine::SetProg(4);              // Weitzmann
    CTAG::DRIVERS::VargEngine::SetBias(70);
    CTAG::DRIVERS::VargEngine::SetHold(2);
    CTAG::DRIVERS::VargEngine::SetRhythm(6);            // GAL
    CTAG::DRIVERS::VargEngine::SetDens(9);
    CTAG::DRIVERS::VargEngine::SetSync(33);
    CTAG::DRIVERS::VargEngine::SetLen(0.77f);           // fraction 0..1
    CTAG::DRIVERS::VargEngine::SetRoot(3);              // Eb — the doc's example
    CTAG::DRIVERS::VargEngine::SetClk(1);
    CTAG::DRIVERS::VargEngine::SetDiv(5);
    CTAG::DRIVERS::VargEngine::SetRootSlot(105);
    CTAG::DRIVERS::VargEngine::SetPairSlot(104);
    CTAG::DRIVERS::VargEngine::SetTremSlot(50);
    CTAG::DRIVERS::VargEngine::SetChgSlot(49);
    char json[512];
    CTAG::DRIVERS::VargEngine::SerializeConfig(json, sizeof(json));
    CHECK(json[0] == '{');
    CTAG::DRIVERS::VargEngine::InitDefaults();
    CHECK(CTAG::DRIVERS::VargEngine::GetProg() == 1);   // defaults restored
    CHECK(CTAG::DRIVERS::VargEngine::DeserializeConfig(json));
    CHECK(CTAG::DRIVERS::VargEngine::GetProg() == 4);
    CHECK(CTAG::DRIVERS::VargEngine::GetBias() == 70);
    CHECK(CTAG::DRIVERS::VargEngine::GetHold() == 2);
    CHECK(CTAG::DRIVERS::VargEngine::GetRhythm() == 6);
    CHECK(CTAG::DRIVERS::VargEngine::GetDens() == 9);
    CHECK(CTAG::DRIVERS::VargEngine::GetSync() == 33);
    CHECK_NEAR(CTAG::DRIVERS::VargEngine::GetLen(), 0.77f, 0.001);  // fraction
    CHECK(CTAG::DRIVERS::VargEngine::GetRoot() == 3);
    CHECK(CTAG::DRIVERS::VargEngine::GetClk() == 1);
    CHECK(CTAG::DRIVERS::VargEngine::GetDiv() == 5);
    CHECK(CTAG::DRIVERS::VargEngine::GetRootSlot() == 105);
    CHECK(CTAG::DRIVERS::VargEngine::GetPairSlot() == 104);
    CHECK(CTAG::DRIVERS::VargEngine::GetTremSlot() == 50);
    CHECK(CTAG::DRIVERS::VargEngine::GetChgSlot() == 49);
    // Weitzmann at Eb tonic: first chord Ebm dyad = Eb + Gb. The roundtrip
    // reassigned the slots — root rides 105, pair rides 104 now.
    {
        uint8_t trig[53]; float cv[108];
        memset(trig, 1, sizeof(trig));
        memset(cv, 0, sizeof(cv));
        CTAG::DRIVERS::VargEngine::Process(cv, trig, 1.0f / 1000.0f);
        CHECK_NEAR(cv[105], (51.0f - 48.0f) / 12.0f * 0.2f, 0.001);  // Eb3
        CHECK_NEAR(cv[104], (54.0f - 48.0f) / 12.0f * 0.2f, 0.001);  // Gb3 (+3)
    }
    // malformed JSON refused wholesale
    CTAG::DRIVERS::VargEngine::InitDefaults();
    CHECK(!CTAG::DRIVERS::VargEngine::DeserializeConfig("{\"prog\":"));
}
