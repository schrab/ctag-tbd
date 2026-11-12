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

