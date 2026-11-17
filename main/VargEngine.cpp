#include "VargEngine.hpp"
#include "VargEngineCore.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "SeqEngineCore.hpp"   // kDivisions
#include "ModTransport.hpp"
#include "rapidjson/document.h"

namespace CTAG::DRIVERS {

using CTAG::SP::HELPERS::ClampCVSlot;
using CTAG::SP::HELPERS::ClampTrigSlot;


// ---- engine state (static DRAM, ~200 B) ----
// Setters run on the UIMenu/link task, Process on the audio task; shared
// scalars are the same benign-race class as MarblesEngine/SeqEngine (a torn
// value applies one block late, nothing dereferences).
static VARG::VargSource gVarg = {};

// per-clock state: riff phase on the chosen division, bar phase at 96 PPQ
static float rPhase = 0.0f;            // riff in-step fraction (0..1)
static float barPhase = 0.0f;          // musical-bar fraction (0..1)
static int vClk = 0;                   // 0 global 1 internal 2 midi
static int vDiv = 7;                   // kDivisions index (default 1/16)

// slot assignment (already clamped; -1 = unassigned)
static int vRootSlot = 106;
static int vPairSlot = 107;
static int vTremSlot = 51;
static int vChgSlot = 52;

static std::atomic<uint32_t> outSnapshot{0};

static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void VargEngine::InitDefaults() {
    gVarg = VARG::VargSource{};
    // Boot into the classic bed: i-bVII aeolian vamp, straight 1/16 tremolo,
    // full minor triads, 2 bars per chord, tonic A.
    gVarg.prog = 1;
    gVarg.bias = 50;      // TRIAD band: each progression keeps its own law
    gVarg.hold = 1;       // 2 bars per chord
    gVarg.rhythm = 1;     // TRM16
    gVarg.dens = 16;      // full density — the straight tremolo bed must not trim
    gVarg.sync = 0;
    gVarg.glen = 0.9f;
    gVarg.root = 9;       // A — the doc's example tonality
    gVarg.rng_state = 0x1a2b3c4du;
    gVarg.step = 0;
    gVarg.chordIdx = 0;
    gVarg.genIdx = 0;
    gVarg.chgPulse = 1;   // announce the boot chord
    gVarg.chgStep = 0;
    vClk = 0;
    vDiv = 7;
    vRootSlot = 106;
    vPairSlot = 107;
    vTremSlot = 51;
    vChgSlot = 52;
    rPhase = 0.0f;
    barPhase = 0.0f;
    outSnapshot.store(0, std::memory_order_relaxed);

    VARG::VargRiffRecompute(gVarg);
    VARG::VargLoadChord(gVarg, 0);   // tonic first
}

void VargEngine::Init() {
    InitDefaults();
    LoadConfig();
}

void VargEngine::Process(float *cv, uint8_t *trig, float dt) {
    // 1) clock bus: one fractional PPQ accumulator shared shape with
    //    SeqEngine — 24 PPQ per quarter, bar = 96 PPQ. The increment IS the
    //    delta; phases only wrap cosmetically.
    const float bpm = ModTransport::GetBPM();
    const float intDelta = 24.0f * (bpm / 60.0f) * dt;
    const int midiTicks = ModTransport::TicksThisBlock();
    const bool running = ModTransport::IsRunning();
    const bool globalMidi = ModTransport::GetGlobalClockMidi();
    const bool startReset = ModTransport::TakeStartReset();

    const bool useMidi = (vClk == 2) || (vClk == 0 && globalMidi);
    const bool active = useMidi ? running : true;   // MIDI sources freeze while stopped

    // FA re-phase: snap to the downbeat, tonic chord, fresh onset pulse.
    // Runs BEFORE this block's tick advance (rack/SeqEngine behavior).
    if (startReset && useMidi && running) {
        gVarg.step = 0;
        gVarg.level = (uint8_t)((gVarg.mask >> 15) & 1u);
        rPhase = 0.0f;
        barPhase = 0.0f;
        gVarg.barCount = 0;
        gVarg.chordIdx = 0;
        gVarg.genIdx = 0;
        VARG::VargLoadChord(gVarg, 0);
        gVarg.chgPulse = 1;
        gVarg.chgStep = 0;
    }

    // 2) musical-bar clock -> chord cadence (96 PPQ per bar)
    const float barAdv = active ? (useMidi ? (float)midiTicks : intDelta) / 96.0f : 0.0f;
    {
        const float prev = barPhase;
        barPhase += barAdv;
        if (barPhase >= 1.0f) barPhase -= floorf(barPhase);
        const uint32_t wraps = (uint32_t)(prev + barAdv);
        if (wraps) VARG::VargAdvanceBar(gVarg, wraps);
    }

    // 3) riff clock -> mask step (per-source division, SeqEngine gate shape)
    const float divPpq = (float)SEQ::kDivisions[vDiv].ppq;
    const float rAdv = active ? (useMidi ? (float)midiTicks / divPpq : intDelta / divPpq) : 0.0f;
    {
        const float prev = rPhase;
        rPhase += rAdv;
        if (rPhase >= 1.0f) rPhase -= floorf(rPhase);
        const uint32_t wraps = (uint32_t)(prev + rAdv);
        if (wraps) VARG::VargAdvanceStep(gVarg, wraps);
    }

    // 4) outputs (both owned slots written every block; active-low trigs).
    // Pitch CVs are ABSOLUTE like the SEQ arp note CVs (MIDI 48 = 0 V) —
    // the plugin pitch bases are anchored to that convention.
    const float rootCv =
        (float)((int32_t)VARG::VargRootNote(gVarg) - 48) / 12.0f * 0.2f;
    const float pairCv =
        (float)((int32_t)VARG::VargPairNote(gVarg) - 48) / 12.0f * 0.2f;
    if (vRootSlot >= 0) cv[vRootSlot] = rootCv;
    if (vPairSlot >= 0) cv[vPairSlot] = pairCv;
    // riff gate: the step's level holds only for the first glen fraction of
    // the division cycle; a stopped MIDI source releases the window.
    const bool tremHigh = active && (gVarg.level != 0) && (rPhase < gVarg.glen);
    if (vTremSlot >= 0) trig[vTremSlot] = tremHigh ? 0 : 1;
    if (vChgSlot >= 0) trig[vChgSlot] = (active && gVarg.chgPulse) ? 0 : 1;

    // snapshot: [trem chg | step | off | qual | root | pair | chordIdx]
    const uint32_t snap =
        (uint32_t)(tremHigh ? 1u : 0u)
        | ((uint32_t)(gVarg.chgPulse ? 1u : 0u) << 1)
        | ((uint32_t)(gVarg.step & 0xFu) << 2)
        | ((uint32_t)(gVarg.curOff & 0xFu) << 6)
        | ((uint32_t)(gVarg.curQual & 0xFu) << 10)
        | ((uint32_t)(gVarg.root & 0xFu) << 14)
        | ((uint32_t)(VARG::kQualityPair[gVarg.curQual % VARG::Q_COUNT] & 0xFu) << 18)
        | ((uint32_t)(gVarg.chordIdx & 0xFu) << 22);
    outSnapshot.store(snap, std::memory_order_relaxed);
}

uint32_t VargEngine::GetOutputSnapshot() { return outSnapshot.load(std::memory_order_relaxed); }

// ---- setters (riff mask recomputed setter-side, rack discipline) ----
int VargEngine::GetProg() { return gVarg.prog; }
void VargEngine::SetProg(int v) {
    gVarg.prog = (uint8_t)ClampInt(v, 0, VARG::kProgCount - 1);
    gVarg.chordIdx = 0;
    gVarg.genIdx = 0;
    gVarg.barCount = 0;
    VARG::VargLoadChord(gVarg, 0);   // hear the new progression immediately
    gVarg.chgPulse = 1;
    gVarg.chgStep = gVarg.step;
}
int VargEngine::GetBias() { return gVarg.bias; }
void VargEngine::SetBias(int v) {
    gVarg.bias = (uint8_t)ClampInt(v, 0, 100);
    // re-voice the CURRENT chord so the band change is heard right away
    gVarg.curQual = VARG::VargApplyBias(gVarg, gVarg.curBase);
}
int VargEngine::GetHold() { return gVarg.hold; }
void VargEngine::SetHold(int v) { gVarg.hold = (uint8_t)ClampInt(v, 0, 2); }
int VargEngine::GetRhythm() { return gVarg.rhythm; }
void VargEngine::SetRhythm(int v) {
    gVarg.rhythm = (uint8_t)ClampInt(v, 0, VARG::kRiffModeTMR);
    VARG::VargRiffRecompute(gVarg);
}
int VargEngine::GetDens() { return gVarg.dens; }
void VargEngine::SetDens(int v) {
    gVarg.dens = (uint8_t)ClampInt(v, 1, 16);
    VARG::VargRiffRecompute(gVarg);
}
int VargEngine::GetSync() { return gVarg.sync; }
void VargEngine::SetSync(int v) {
    gVarg.sync = (uint8_t)ClampInt(v, 0, 100);
    VARG::VargRiffRecompute(gVarg);
}
float VargEngine::GetLen() { return gVarg.glen; }
void VargEngine::SetLen(float v) { gVarg.glen = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
int VargEngine::GetRoot() { return gVarg.root; }
void VargEngine::SetRoot(int v) {
    gVarg.root = (uint8_t)ClampInt(v, 0, 11);   // CVs derive per block, no rebuild
}
int VargEngine::GetClk() { return vClk; }
void VargEngine::SetClk(int v) { vClk = ClampInt(v, 0, 2); }
int VargEngine::GetDiv() { return vDiv; }
void VargEngine::SetDiv(int v) { vDiv = ClampInt(v, 0, SEQ::kDivCount - 1); }
int VargEngine::GetRootSlot() { return vRootSlot; }
void VargEngine::SetRootSlot(int s) { vRootSlot = ClampCVSlot(s); }
int VargEngine::GetPairSlot() { return vPairSlot; }
void VargEngine::SetPairSlot(int s) { vPairSlot = ClampCVSlot(s); }
int VargEngine::GetTremSlot() { return vTremSlot; }
void VargEngine::SetTremSlot(int s) { vTremSlot = ClampTrigSlot(s); }
int VargEngine::GetChgSlot() { return vChgSlot; }
void VargEngine::SetChgSlot(int s) { vChgSlot = ClampTrigSlot(s); }
uint32_t VargEngine::GetMask() { return gVarg.mask; }

// ---- persistence: flat JSON in the "varg" section ----
// mod-config.jsn is hand-editable, so every member is type-checked and
// clamped on load; unknown members are ignored.

void VargEngine::SerializeConfig(char *dst, int cap) {
    char buf[288];
    snprintf(buf, sizeof(buf),
             "{\"prog\":%d,\"bias\":%d,\"hold\":%d,\"rhythm\":%d,\"dens\":%d,"
             "\"sync\":%d,\"glen\":%.3g,\"root\":%d,\"clk\":%d,\"div\":%d,"
             "\"rootSlot\":%d,\"pairSlot\":%d,\"tremSlot\":%d,\"chgSlot\":%d}",
             gVarg.prog, gVarg.bias, gVarg.hold, gVarg.rhythm, gVarg.dens,
             gVarg.sync, (double)gVarg.glen, gVarg.root, vClk, vDiv,
             vRootSlot, vPairSlot, vTremSlot, vChgSlot);
    if ((int)strlen(buf) < cap) {
        memcpy(dst, buf, strlen(buf) + 1);
    } else if (cap > 0) {
        dst[0] = 0;
    }
}

bool VargEngine::DeserializeConfig(const char *json) {
    using namespace rapidjson;
    Document d;
    if (d.Parse(json).HasParseError() || !d.IsObject()) return false;

    if (d.HasMember("prog") && d["prog"].IsInt())
        gVarg.prog = (uint8_t)ClampInt(d["prog"].GetInt(), 0, VARG::kProgCount - 1);
    if (d.HasMember("bias") && d["bias"].IsInt())
        gVarg.bias = (uint8_t)ClampInt(d["bias"].GetInt(), 0, 100);
    if (d.HasMember("hold") && d["hold"].IsInt())
        gVarg.hold = (uint8_t)ClampInt(d["hold"].GetInt(), 0, 2);
    if (d.HasMember("rhythm") && d["rhythm"].IsInt())
        gVarg.rhythm = (uint8_t)ClampInt(d["rhythm"].GetInt(), 0, VARG::kRiffModeTMR);
    if (d.HasMember("dens") && d["dens"].IsInt())
        gVarg.dens = (uint8_t)ClampInt(d["dens"].GetInt(), 1, 16);
    if (d.HasMember("sync") && d["sync"].IsInt())
        gVarg.sync = (uint8_t)ClampInt(d["sync"].GetInt(), 0, 100);
    if (d.HasMember("glen") && d["glen"].IsNumber()) {
        float f = d["glen"].GetFloat();
        gVarg.glen = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    }
    if (d.HasMember("root") && d["root"].IsInt())
        gVarg.root = (uint8_t)ClampInt(d["root"].GetInt(), 0, 11);
    if (d.HasMember("clk") && d["clk"].IsInt())
        vClk = ClampInt(d["clk"].GetInt(), 0, 2);
    if (d.HasMember("div") && d["div"].IsInt())
        vDiv = ClampInt(d["div"].GetInt(), 0, SEQ::kDivCount - 1);
    if (d.HasMember("rootSlot") && d["rootSlot"].IsInt())
        vRootSlot = ClampCVSlot(d["rootSlot"].GetInt());
    if (d.HasMember("pairSlot") && d["pairSlot"].IsInt())
        vPairSlot = ClampCVSlot(d["pairSlot"].GetInt());
    if (d.HasMember("tremSlot") && d["tremSlot"].IsInt())
        vTremSlot = ClampTrigSlot(d["tremSlot"].GetInt());
    if (d.HasMember("chgSlot") && d["chgSlot"].IsInt())
        vChgSlot = ClampTrigSlot(d["chgSlot"].GetInt());

    // derived state: riff mask from (rhythm, dens, sync), chord from
    // (prog, bias) — tonic first, like a fresh Init
    gVarg.chordIdx = 0;
    gVarg.genIdx = 0;
    gVarg.barCount = 0;
    VARG::VargRiffRecompute(gVarg);
    VARG::VargLoadChord(gVarg, 0);
    return true;
}

void VargEngine::LoadConfig() {
    char buf[512];
    if (!ModTransport::ReadSectionJSON("varg", buf, sizeof(buf))) return;
    DeserializeConfig(buf);
}

void VargEngine::SaveConfig() {
    char buf[512];
    SerializeConfig(buf, sizeof(buf));
    if (buf[0]) ModTransport::WriteSectionJSON("varg", buf);
}

} // namespace CTAG::DRIVERS
