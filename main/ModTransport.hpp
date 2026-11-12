#pragma once
#include <cstdint>

namespace CTAG::DRIVERS {

// Shared modulation transport: global BPM, MIDI-clock funnel, transport
// state. ONE owner of the incoming MIDI clock edges — Marbles (RampExtractor
// feed) and SeqEngine (integer PPQ ticks) both consume the per-block state
// published by Process(), nobody drains the raw counter themselves.
// Host-testable: the core paths use OnMidiClockAt(int64_t) + Process(dt); the
// esp_timer convenience wrapper is guarded so tests compile without IDF.
class ModTransport {
public:
    static void Init();
    // MIDI task entries (link-fed bytes). OnMidiClock uses esp_timer; the
    // _At form is the testable core.
    static void OnMidiClock();
    static void OnMidiClockAt(int64_t now_us);
    static void OnMidiStart();   // 0xFA and 0xFB
    static void OnMidiStop();    // 0xFC

    // Audio task, once per block BEFORE MarblesEngine/SeqEngine Process.
    static void Process(float dt /*seconds*/);
    // Block state, valid after Process until the next Process:
    static int     TicksThisBlock();    // MIDI PPQ ticks landed in this block
    static bool    IsRunning();         // transport running (FA/FB set, FC clears)
    static bool    TakeStartReset();    // true once per FA (pattern re-phase)
    // Marbles RampExtractor feed (one gate-flag stream per block, same
    // collapse rule as the pre-hoist pendingClockEdges drain):
    static uint8_t ExternalGateFlags(); // stmlib::GateFlags value
    static void    SetExternalGateLevel(bool high); // advanced inside Process

    // Global tempo (20..480). Measured EMA adopts while MIDI clock runs.
    static float GetBPM();
    static void  SetBPM(float v);
    static float GetMeasuredBPM();   // -1 = no measurement yet

    // Global clock source for the SEQ "Global" clk option (the MOD CLOCK
    // page's MIDI-vs-internal choice; Marbles' tExternal is its own flag by
    // design). Persisted as "ext" in the "transport" section.
    static bool GetGlobalClockMidi();
    static void SetGlobalClockMidi(bool v);

    // mod-config.jsn ownership: single file, read-merge-write per section so
    // transport/marbles/seq never overwrite each other.
    static void  LoadConfig();       // "transport" + legacy marbles.bpm
    static void  SaveConfig();       // writes "transport"
    static bool  ReadSectionJSON(const char *name, char *dst, int cap);
    static bool  WriteSectionJSON(const char *name, const char *json);
};

}
