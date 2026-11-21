#include "test_framework.hpp"
#include "SeqEngine.hpp"
#include "ModTransport.hpp"
#include <array>
#include <cstring>

using CTAG::DRIVERS::SeqEngine;
using CTAG::DRIVERS::ModTransport;

namespace {
struct Bench {
    float cv[108] {};
    uint8_t trig[53];
    Bench() {
        memset(trig, 1, sizeof(trig));
        ModTransport::Init();
        SeqEngine::InitDefaults();
        ModTransport::SetBPM(120.f);
        SeqEngine::SetArpAlgo(0, 18);
        SeqEngine::SetArpP(0, 0, 10);
        SeqEngine::SetArpP(0, 1, 0);
        SeqEngine::SetArpP(0, 2, 50);
        SeqEngine::SetArpP(0, 3, 0);
        SeqEngine::SetArpP(0, 5, 0);
    }
    void Tick(float dt = .001f) {
        ModTransport::Process(dt);
        SeqEngine::Process(cv, trig, dt);
    }
};
}

TEST(varg_saved_range_survives_reload) {
    Bench b;
    for (int algo : {15, 18}) {
        SeqEngine::SetArpAlgo(0, algo);
        SeqEngine::SetArpRange(0, 3);
        char json[1024];
        SeqEngine::SerializeConfig(json, sizeof(json));
        CHECK(SeqEngine::DeserializeConfig(json));
        CHECK_EQ(SeqEngine::GetArpRange(0), 3);
    }
}

TEST(varg_arpeggio_companion_routes_single_notes) {
    Bench b;
    SeqEngine::SetArpP(0, 8, 51);
    bool sawGate = false;
    for (int k = 0; k < 500; ++k) {
        b.Tick();
        if (b.trig[45] == 0) {
            sawGate = true;
            CHECK_EQ(SeqEngine::GetVargVoicing(0), -1);
            CHECK(SeqEngine::GetVargString(0) >= 0);
        }
    }
    CHECK(sawGate);
}

TEST(varg_all_burst_orders_emit_complete_bounded_events) {
    for (int order = 0; order < 9; ++order) {
        Bench b;
        SeqEngine::SetArpP(0, 5, order == 0 ? 0 : order * 101 / 9 + 1);
        SeqEngine::SetArpP(0, 6, 100);
        bool lastLow = false;
        int start = -1, end = -1;
        for (int k = 0; k < 245; ++k) {
            b.Tick();
            bool low = b.trig[45] == 0;
            if (low && !lastLow) {
                if (start < 0) start = k;
                end = k;
                CHECK(SeqEngine::GetVargBurst(0));
                CHECK(SeqEngine::GetVargString(0) >= 0);
                CHECK(SeqEngine::GetVargString(0) < 4);
                CHECK(SeqEngine::GetVargBurstVel(0) > 0);
                CHECK(SeqEngine::GetVargBurstVel(0) <= 100);
                CHECK(b.cv[104] >= -.2f && b.cv[104] <= .8f);
            }
            CHECK(!(low && lastLow));
            lastLow = low;
        }
        CHECK(end - start <= 45);
        CHECK(end - start <= 45);
    }
}

TEST(varg_fast_bursts_finish_before_next_step) {
    Bench b;
    ModTransport::SetBPM(480.f);
    SeqEngine::SetArpDiv(0, 8); // 1/32: 15.625 ms
    SeqEngine::SetArpP(0, 6, 100);
    int count = 0;
    bool lastLow = false;
    for (int k = 0; k < 46; ++k) {
        b.Tick();
        bool low = b.trig[45] == 0;
        if (low && !lastLow) ++count;
        lastLow = low;
    }
    CHECK_EQ(count, 8); // Two complete four-note strums.
}

TEST(varg_burst_pitch_holds_between_events) {
    Bench b;
    SeqEngine::SetArpP(0, 5, 23); // Pendulum
    SeqEngine::SetArpP(0, 6, 100);
    bool seen = false;
    float lastPitch = 0.f;
    for (int k = 0; k < 500; ++k) {
        b.Tick();
        if (b.trig[45] == 0) {
            seen = true;
            lastPitch = b.cv[104];
        } else if (seen) {
            CHECK_NEAR(b.cv[104], lastPitch, 1e-6f);
        }
    }
    CHECK(seen);
}

TEST(varg_burst_publishes_accent_pulses) {
    Bench b;
    SeqEngine::SetArpP(0, 5, 23);
    bool sawAccent = false;
    for (int k = 0; k < 800; ++k) {
        b.Tick();
        if (b.trig[47] == 0) {
            sawAccent = true;
            CHECK_EQ(b.trig[45], 0);
        }
    }
    CHECK(sawAccent);
}

TEST(midi_stop_releases_arp_gate_and_cancels_burst) {
    for (bool arpeggio : {false, true}) {
        Bench b;
        SeqEngine::SetArpP(0, 5, 23);
        SeqEngine::SetArpP(0, 6, 100);
        SeqEngine::SetArpP(0, 8, arpeggio ? 51 : 0);
        SeqEngine::SetArpClk(0, 2);
        ModTransport::OnMidiStart();
        bool found = false;
        for (int k = 0; k < 40; ++k) {
            ModTransport::OnMidiClockAt(1000000 + k * 20833);
            b.Tick();
            if (b.trig[45] == 0) { found = true; break; }
        }
        CHECK(found);
        ModTransport::OnMidiStop();
        for (int k = 0; k < 80; ++k) {
            b.Tick();
            CHECK_EQ(b.trig[45], 1);
            CHECK_EQ(b.trig[47], 1);
            CHECK_EQ(b.trig[49], 1);
            CHECK_EQ(SeqEngine::GetVargBurst(0), 0);
        }
    }
}

TEST(seq_remap_releases_previous_unowned_outputs) {
    Bench b;
    SeqEngine::SetArpP(0, 8, 51);
    for (int k = 0; k < 125; ++k) b.Tick();
    CHECK_EQ(b.trig[45], 0);
    SeqEngine::SetArpGateSlot(0, 26);
    SeqEngine::SetArpPitchSlot(0, 26);
    b.Tick();
    CHECK_EQ(b.trig[45], 1);
    CHECK_EQ(b.trig[26], 0);
    CHECK_NEAR(b.cv[104], 0.f, 1e-6f);
}
