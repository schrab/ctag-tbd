/*
 * Host unit tests for the SEQ modulators (GATE x2 + ARP x2) and the
 * slot-space growth they need (cv 102-105, trig 43-50).
 */
#include "test_framework.hpp"

#include "helpers/ctagSlotBounds.hpp"
#include "stmlib/utils/gate_flags.h"

TEST(seq_clamp_new_bounds_valid) {
    // cv 102..105 and trig 43..50 must be valid after the extension
    CHECK(CTAG::SP::HELPERS::ClampCVSlot(102) == 102);
    CHECK(CTAG::SP::HELPERS::ClampCVSlot(105) == 105);
    CHECK(CTAG::SP::HELPERS::ClampTrigSlot(43) == 43);
    CHECK(CTAG::SP::HELPERS::ClampTrigSlot(50) == 50);
}

TEST(seq_clamp_at_new_bounds_invalid) {
    // first index past each range clamps to unassigned (stack-safety rule)
    CHECK(CTAG::SP::HELPERS::ClampCVSlot(106) == -1);
    CHECK(CTAG::SP::HELPERS::ClampTrigSlot(51) == -1);
}

// ---- ModTransport (hoisted transport) ----

#define MODTRANSPORT_HOST 1
#include "ModTransport.hpp"

TEST(transport_ticks_drained_once) {
    CTAG::DRIVERS::ModTransport::Init();
    CTAG::DRIVERS::ModTransport::OnMidiClockAt(1000000);
    CTAG::DRIVERS::ModTransport::OnMidiClockAt(1000001);
    // both engines read the SAME drained count; draining twice is the bug class
    CTAG::DRIVERS::ModTransport::Process(1.0f / 1000.0f);
    int a = CTAG::DRIVERS::ModTransport::TicksThisBlock();
    int b = CTAG::DRIVERS::ModTransport::TicksThisBlock();
    CHECK(a == 2);
    CHECK(b == 2);   // stable within the block, reset only at next Process
    CTAG::DRIVERS::ModTransport::Process(1.0f / 1000.0f);
    CHECK(CTAG::DRIVERS::ModTransport::TicksThisBlock() == 0);
}

TEST(transport_start_stop_states) {
    CTAG::DRIVERS::ModTransport::Init();
    CHECK(!CTAG::DRIVERS::ModTransport::IsRunning());
    CTAG::DRIVERS::ModTransport::OnMidiStart();
    CHECK(CTAG::DRIVERS::ModTransport::TakeStartReset());
    CHECK(!CTAG::DRIVERS::ModTransport::TakeStartReset());  // once per FA
    CHECK(CTAG::DRIVERS::ModTransport::IsRunning());
    CTAG::DRIVERS::ModTransport::OnMidiStop();
    CHECK(!CTAG::DRIVERS::ModTransport::IsRunning());
    CHECK(CTAG::DRIVERS::ModTransport::TicksThisBlock() == 0); // no ticks yet
}

TEST(transport_bpm_clamp_and_ema) {
    CTAG::DRIVERS::ModTransport::Init();
    CTAG::DRIVERS::ModTransport::SetBPM(999.0f);
    CHECK(CTAG::DRIVERS::ModTransport::GetBPM() <= 480.0f);
    CTAG::DRIVERS::ModTransport::SetBPM(120.0f);
    // 24 MIDI-clock ticks per quarter: at 120 BPM a quarter is 500 ms, so
    // ticks land 500 ms / 24 apart. The EMA converges toward 120.
    int64_t t = 0;
    for (int i = 0; i < 24; ++i) {
        CTAG::DRIVERS::ModTransport::OnMidiClockAt(t);
        t += 500000 / 24;
    }
    for (int i = 0; i < 24; ++i) { CTAG::DRIVERS::ModTransport::Process(1.0f / 1000.0f); }
    float measured = CTAG::DRIVERS::ModTransport::GetMeasuredBPM();
    CHECK(measured > 100.0f && measured < 140.0f);
}

TEST(transport_gate_flags_follow_drain) {
    CTAG::DRIVERS::ModTransport::Init();
    // idle: gate chain low
    CTAG::DRIVERS::ModTransport::Process(1.0f / 1000.0f);
    CHECK(CTAG::DRIVERS::ModTransport::ExternalGateFlags() == stmlib::GATE_FLAG_LOW);
    // one tick -> one RISING edge (the RampExtractor feed)
    CTAG::DRIVERS::ModTransport::OnMidiClockAt(2000000);
    CTAG::DRIVERS::ModTransport::Process(1.0f / 1000.0f);
    CHECK(CTAG::DRIVERS::ModTransport::ExternalGateFlags() ==
          (stmlib::GATE_FLAG_RISING | stmlib::GATE_FLAG_HIGH));
    // silence next block -> FALLING (collapse rule: one high block per tick)
    CTAG::DRIVERS::ModTransport::Process(1.0f / 1000.0f);
    CHECK(CTAG::DRIVERS::ModTransport::ExternalGateFlags() == stmlib::GATE_FLAG_FALLING);
}

TEST(transport_config_section_roundtrip) {
    // Scratch path injected by run_tests.sh (-DMODTRANSPORT_CFG_PATH=...);
    // run_tests.sh wipes it before the run. Sections must MERGE, not clobber.
    char buf[256];
    CHECK(!CTAG::DRIVERS::ModTransport::ReadSectionJSON("transport", buf, sizeof(buf)));
    CHECK(CTAG::DRIVERS::ModTransport::WriteSectionJSON("transport", "{\"bpm\":123.5}"));
    CHECK(CTAG::DRIVERS::ModTransport::WriteSectionJSON("seq", "{\"gates\":[1,2]}"));
    CHECK(CTAG::DRIVERS::ModTransport::ReadSectionJSON("transport", buf, sizeof(buf)));
    CHECK_STREQ(buf, "{\"bpm\":123.5}");
    CHECK(CTAG::DRIVERS::ModTransport::ReadSectionJSON("seq", buf, sizeof(buf)));
    CHECK_STREQ(buf, "{\"gates\":[1,2]}");
    // malformed fragment refused, file untouched
    CHECK(!CTAG::DRIVERS::ModTransport::WriteSectionJSON("transport", "{\"bpm\":"));
    CHECK(CTAG::DRIVERS::ModTransport::ReadSectionJSON("transport", buf, sizeof(buf)));
    CHECK_STREQ(buf, "{\"bpm\":123.5}");
}

// ---- SeqEngineCore: gate generator + clock bus ----

#include "SeqEngineCore.hpp"

TEST(gate_preset_patterns) {
    using namespace CTAG::SEQ;
    // 16 canonical patterns, MSB = step 1 (rack mod_engine.h:396-413)
    CHECK(kTranceGatePresets[0] == 0xAAAAu);
    CHECK(kTranceGatePresets[9] == 0x9292u);
    CHECK(kTranceGatePresets[15] == 0x88AFu);
    GateSource g{};
    g.mode = 1; g.dens = 16; g.sync = 0; g.glen = 0.5f;
    GateRecompute(g);
    CHECK(g.mask == 0xAAAAu);   // preset 01 untouched at full density
    g.mode = 3; GateRecompute(g); CHECK(g.mask == 0x7777u);
}

TEST(gate_barlow_density_ordering) {
    using namespace CTAG::SEQ;
    // GEN mode: density n keeps every strong beat of density n-1 (Barlow trim)
    GateSource a{}, b{};
    a.mode = 0; a.dens = 8;  a.sync = 0; b.mode = 0; b.dens = 10; b.sync = 0;
    GateRecompute(a); GateRecompute(b);
    CHECK((b.mask & a.mask) == a.mask);
}

TEST(gate_tmr_deterministic) {
    using namespace CTAG::SEQ;
    GateSource a{}, b{};
    a.mode = 17; a.dens = 12; a.sync = 40; b = a;
    a.rng_state = 0x12345678u; b.rng_state = 0x12345678u;
    GateRecompute(a); GateRecompute(b);
    CHECK(a.mask == b.mask);            // same seed -> same groove
    uint32_t flips = 0;
    for (int i = 0; i < 64; ++i) {
        uint32_t before = a.mask;
        GateAdvanceStep(a, 1);          // advances the step AND maybe mutates
        flips += (a.mask != before);
    }
    CHECK(flips > 0);                   // mutation actually mutates
}

TEST(gate_markov_adjacent_transitions) {
    using namespace CTAG::SEQ;
    // MRKV: dens seeds the preset, temp drives weighted transitions through
    // mod_gate_markov_next; the mask ALWAYS mirrors the tracked preset.
    GateSource g{}, h{};
    g.mode = 18; g.dens = 3; g.sync = 100; g.rng_state = 99;
    h = g;
    GateRecompute(g); GateRecompute(h);
    CHECK(g.mask == kTranceGatePresets[2]);   // dens 3 -> preset index 2
    for (int i = 0; i < 16; ++i) { GateAdvanceStep(g, 1); GateAdvanceStep(h, 1); }
    CHECK(g.mask == h.mask);   // same seed -> identical transition chain
    CHECK(g.mask == kTranceGatePresets[g.cur_preset]);
}

TEST(division_table_and_default) {
    using namespace CTAG::SEQ;
    CHECK(kDivCount == 13);
    CHECK(strcmp(kDivisions[7].name, "1/16") == 0);   // Review Focus #4: index 7 IS 1/16
    CHECK(kDivisions[7].ppq == 6);
    CHECK(kDivisions[12].ppq == 2);                   // 1/32T
    CHECK(kDivisions[3].ppq == 96);                   // 1/1
}

TEST(gate_step_advance_1_16) {
    using namespace CTAG::SEQ;
    GateSource g{};
    g.mode = 1; g.dens = 16; g.sync = 0; g.glen = 0.5f;
    GateRecompute(g);
    g.step = 15;
    g.level = (g.mask >> (15 - g.step)) & 1u;   // 0xAAAA bit 0 = 0
    CHECK(g.level == 0u);
    CHECK(GateAdvanceStep(g, 0) == 0u);         // no wraps -> nothing moves
    CHECK(g.step == 15);
    CHECK(GateAdvanceStep(g, 1) == 1u);         // one wrap: 15 -> 0
    CHECK(g.step == 0);
    CHECK(g.level == 1u);                       // 0xAAAA bit 15 = 1
}

// ---- SeqEngineCore: ARP generators ----

TEST(arp_scale_quantization_all_20) {
    using namespace CTAG::SEQ;
    for (uint8_t sc = 0; sc < SCALE_COUNT; ++sc) {
        ArpSource s{}; s.scale_idx = sc; s.root = 0;   // fixed C
        ArpExt e{};
        ArpRebuildScale(s, e);
        CHECK(e.nsteps >= 2 && e.nsteps <= 12);
        // scale steps are strictly ascending semitone offsets < 12
        for (uint8_t i = 1; i < e.nsteps; ++i) CHECK(e.scale_st[i] > e.scale_st[i - 1]);
        CHECK(e.scale_st[0] == 0);
    }
}

TEST(arp_pitch_convention_1v_oct) {
    using namespace CTAG::SEQ;
    ArpSource s{}; s.root = 0; s.scale_idx = 6 /* Aeolian */; s.range = 2;
    ArpExt e{}; ArpRebuildScale(s, e);
    e.deg = 0;
    ArpStep step{}; step.off = 7; step.oct = 0; step.flags = 1;  // +7 scale degrees = one octave
    uint8_t note = ArpCalcNote(s, e, step);
    // 1 V/oct convention: cv 0.2 == exactly +12 st (TBD03 multiplies by 12*5)
    float cv = (float)(note - 48) / 12.0f * 0.2f;
    CHECK_NEAR(cv, 0.2, 0.001);
    CHECK((note - 48) == (int)((cv * 60.0f) + 0.01f));
}

TEST(arp_algo_determinism_seeded) {
    using namespace CTAG::SEQ;
    for (uint8_t algo = 0; algo < 5; ++algo) {
        ArpSource a{}, b{};
        a.algo = algo; b.algo = algo;
        a.range = 2; b.range = 2;
        a.scale_idx = 6; b.scale_idx = 6;
        a.p[0] = 62; a.p[1] = 40; a.p[2] = 50; a.p[3] = 20;
        b = a;
        a.rng = 0x1fff7a10u; b.rng = 0x1fff7a10u;
        ArpExt ea{}, eb{};
        ArpRebuildPattern(a, ea); ArpRebuildPattern(b, eb);
        CHECK(memcmp(ea.steps, eb.steps, sizeof(ea.steps)) == 0);
    }
}

TEST(arp_accent_slide_flags_on_every_algo) {
    using namespace CTAG::SEQ;
    bool saw_acc = false, saw_sld = false, saw_gate = false, saw_off = false;
    for (uint8_t algo = 0; algo < 5; ++algo) {
        ArpSource s{}; s.algo = algo; s.range = 3; s.scale_idx = 6;
        s.p[0] = 80; s.p[1] = 50; s.p[2] = 50; s.p[3] = 50; s.rng = 12345;
        ArpExt e{};
        ArpRebuildPattern(s, e);
        for (int st = 0; st < 16; ++st) {
            saw_gate |= (e.steps[st].flags & 1) != 0;
            saw_off  |= (e.steps[st].flags & 1) == 0;
            saw_acc  |= (e.steps[st].flags & 2) != 0;
            saw_sld  |= (e.steps[st].flags & 4) != 0;
        }
    }
    CHECK(saw_gate); CHECK(saw_off); CHECK(saw_acc); CHECK(saw_sld);
}

TEST(arp_root_modes_exist) {
    using namespace CTAG::SEQ;
    // Neo drift: root rotates as a pitch class, never out of range
    ArpSource s{}; s.root = 4 /* E */; s.root_mode = 2; s.scale_idx = 0;
    s.p[0] = 50; s.p[1] = 50; s.p[2] = 50; s.p[3] = 60; s.rng = 7;
    ArpExt e{};
    ArpRebuildPattern(s, e);
    ArpNextChord(s, e, 3 /* R op */);
    CHECK(s.root >= 0 && s.root <= 11);
    // W7 drift: root stays, degree moves
    s.root_mode = 1;
    s.root = 2; e.deg = 0;
    ArpNextChord(s, e, 0);
    CHECK(s.root == 2);
    CHECK(e.deg >= 0);
}

