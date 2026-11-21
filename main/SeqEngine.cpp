#include "SeqEngine.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <mutex>
#include "ModTransport.hpp"
#include "rapidjson/document.h"

namespace CTAG::DRIVERS {

using CTAG::SP::HELPERS::ClampCVSlot;
using CTAG::SP::HELPERS::ClampTrigSlot;


// ---- engine state (static DRAM, ~1.5 KB) ----
// Control-side rebuilds and audio-side advancement share one bounded lock.
// File parsing and persistence run outside it. Companions are published once
// per block so a control edit cannot split a note from its routing metadata.
static std::recursive_mutex stateMutex;
using StateLock = std::lock_guard<std::recursive_mutex>;
static SEQ::GateSource gGate[2];
static SEQ::ArpSource gArp[2];
static SEQ::ArpExt gArpExt[2];
static uint8_t gNeoOp = 0;             // shared Neo-Riemannian op (0..7)

// per-source clocking (spec §4.2 clock bus)
static float intPhase = 0.0f;          // internal PPQ-tick accumulator
static float gPhase[2] = {0.0f, 0.0f}; // gate in-step fraction (0..1)
static float aPhase[2] = {0.0f, 0.0f}; // arp in-step fraction (0..1)
static int gateClk[2] = {0, 0};        // 0 global 1 internal 2 midi
static int gateDiv[2] = {7, 7};        // kDivisions index
static int arpClk[2] = {0, 0};
static int arpDiv[2] = {7, 7};
static int arpGlideMs[2] = {0, 0};
static int arpOct[2] = {0, 0};   // -2..+2 octaves, applied at the pitch CV

// slot assignment (already clamped; -1 = unassigned)
static int gateTrigSlot[2] = {43, 44};
static int gateCvSlot[2] = {102, 103};
static int arpPitchSlot[2] = {104, 105};
static int arpGateSlot[2] = {45, 46};
static int arpAccSlot[2] = {47, 48};
static int arpSlideSlot[2] = {49, 50};
// strum companion latch (per ARP): the stroke/flags of the step whose gate
// just opened — the wrapper sees the trig edge and this in the same block
static int8_t s_vargStroke[2] = {0, 0};
static uint8_t s_vargFlags[2] = {0, 0};
// Guitar string index of the note whose gate just opened (either VARG algo).
static int8_t s_vargString[2] = {-1, -1};

// VARG Chord expands each gate into individual note events. Seven entries
// cover the pedal plus all six notes of either broken-chord order.
struct StrumBurst {
    bool active {false};
    uint8_t count {0};
    uint8_t idx {0};
    uint8_t countdown {0};
    uint8_t spacing {2};
    uint16_t totalSpread {0};
    bool glide {false};
    bool accent {false};
    uint8_t kind {0};
    int8_t slot[7] {};
    int8_t note[7] {};
    uint8_t vel[7] {};
    uint32_t rng {0x1a2b3c4du};
};
static StrumBurst s_strumBurst[2];
static uint8_t s_vargBurst[2] = {0, 0};
static uint8_t s_vargBurstVel[2] = {0, 0};
static uint8_t s_vargGhost[2] = {0, 0};
// Set on the block a VARG progression advances; consumed by the first
// burst so the strings glide to the new chord only at that moment.
static uint8_t s_vargChordChange[2] = {0, 0};
static uint8_t s_vargGlide[2] = {0, 0};
// First gated step of a chord: that step carries the open pedal pitch.
static uint8_t s_vargPedal[2] = {0, 0};

// outputs
static float gateOut[2] = {0.0f, 0.0f};   // slewed gate level
static float arpOut[2] = {0.0f, 0.0f};    // leveled arp out (accent shaped)
static std::atomic<uint32_t> outSnapshot{0};
static float arpPitchOut[2] {};
static uint8_t arpGateOut[2] {1, 1};
static uint8_t arpAccentOut[2] {1, 1};
static uint8_t arpSlideOut[2] {1, 1};
static int previousTrigSlot[8] {43, 44, 45, 46, 47, 48, 49, 50};
static int previousCvSlot[4] {102, 103, 104, 105};
struct VargCompanion {
    int voicing {-1};
    int string {-1};
    int pitchSlot {-1};
    int gateSlot {-1};
    int8_t stroke {0};
    uint8_t flags {0}, burst {0}, velocity {0}, ghost {0}, glide {0};
};
static VargCompanion companion[2];

static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void SeqEngine::InitDefaults() {
    StateLock lock(stateMutex);
    for (int i = 0; i < 2; ++i) {
        s_strumBurst[i] = StrumBurst{};
        companion[i] = VargCompanion{};
        s_vargString[i] = -1;
        s_vargStroke[i] = 0;
        s_vargFlags[i] = s_vargBurst[i] = s_vargBurstVel[i] = 0;
        s_vargGhost[i] = s_vargChordChange[i] = s_vargGlide[i] = s_vargPedal[i] = 0;
        arpPitchOut[i] = 0.f;
        arpGateOut[i] = arpAccentOut[i] = arpSlideOut[i] = 1;
        // gates: spec §4.4 — gate1 preset 01 "Classic Alternating", gate2
        // preset 03 "4-on-the-Floor Ducking Pump"
        previousTrigSlot[i] = 43 + i;
        previousTrigSlot[2 + i] = 45 + i;
        previousTrigSlot[4 + i] = 47 + i;
        previousTrigSlot[6 + i] = 49 + i;
        previousCvSlot[i] = 102 + i;
        previousCvSlot[2 + i] = 104 + i;
        gGate[i] = SEQ::GateSource{};
        gGate[i].mode = (i == 0) ? 1 : 3;
        gGate[i].dens = 8;
        gGate[i].sync = 0;
        gGate[i].glen = 0.5f;
        gGate[i].rng_state = 0x12345678u + (uint32_t)i * 0x9e3779b9u;
        gateClk[i] = 0;
        gateDiv[i] = 7;
        gateTrigSlot[i] = 43 + i;
        gateCvSlot[i] = 102 + i;
        SEQ::GateRecompute(gGate[i]);

        // arps: arp1 Acid 303 range 2, arp2 Phrase Motif range 2
        gArp[i] = SEQ::ArpSource{};
        gArp[i].algo = (i == 0) ? SEQ::kArpAlgoHyp1 : SEQ::kArpAlgoMotif;
        gArp[i].range = 2;
        gArp[i].p[0] = 62; gArp[i].p[1] = 40; gArp[i].p[2] = 50; gArp[i].p[3] = 0;
        gArp[i].p[4] = 0; gArp[i].p[5] = 0; gArp[i].p[6] = 0; gArp[i].p[7] = 0;
        // Seed the current note at the root: cur_note 0 would slam the pitch
        // CV to -0.8 V from boot until the first gated step wraps.
        gArp[i].cur_note = (uint8_t)(48u + gArp[i].root);
        gArp[i].rng = 0x1fff7a10u + (uint32_t)i * 2654435761u;
        arpGlideMs[i] = 0;
        arpOct[i] = 0;
        arpClk[i] = 0;
        arpDiv[i] = 7;
        arpPitchSlot[i] = 104 + i;
        arpGateSlot[i] = 45 + i;
        arpAccSlot[i] = 47 + i;
        arpSlideSlot[i] = 49 + i;
        gArpExt[i] = SEQ::ArpExt{};
        // VARG's "unloaded" marker: the first rebuilt bar plays the
        // progression's tonic chord instead of advancing past it.
        gArpExt[i].st.varg.chordIdx = 0xFF;
    }
    gNeoOp = 0;
    intPhase = 0.0f;
    gPhase[0] = gPhase[1] = 0.0f;
    aPhase[0] = aPhase[1] = 0.0f;
    gateOut[0] = gateOut[1] = 0.0f;
    arpOut[0] = arpOut[1] = 0.0f;
    outSnapshot.store(0, std::memory_order_relaxed);

    for (int i = 0; i < 2; ++i) {
        SEQ::ArpRebuildPattern(gArp[i], gArpExt[i], false);
        // Start at the root note — a pitch CV that starts at 0 would yank a
        // mapped plugin down ~2.4 V on load.
        gArpExt[i].current_pitch_q16 = gArpExt[i].target_pitch_q16 =
            (int32_t)(48u + gArp[i].root) << 16;
    }
}

void SeqEngine::Init() {
    InitDefaults();
    LoadConfig();
}

void SeqEngine::Process(float *cv, uint8_t *trig, float dt) {
    StateLock lock(stateMutex);
    // Release destinations abandoned since the previous block, unless another
    // SEQ output still owns that destination. Then write all current outputs.
    const int trigSlots[8] = {gateTrigSlot[0], gateTrigSlot[1],
        arpGateSlot[0], arpGateSlot[1], arpAccSlot[0], arpAccSlot[1],
        arpSlideSlot[0], arpSlideSlot[1]};
    const int cvSlots[4] = {gateCvSlot[0], gateCvSlot[1], arpPitchSlot[0], arpPitchSlot[1]};
    for (int &old : previousTrigSlot) {
        bool owned = false;
        for (int slot : trigSlots) if (slot == old) owned = true;
        if (old >= 0 && !owned) trig[old] = 1;
    }
    for (int &old : previousCvSlot) {
        bool owned = false;
        for (int slot : cvSlots) if (slot == old) owned = true;
        if (old >= 0 && !owned) cv[old] = 0.f;
    }
    memcpy(previousTrigSlot, trigSlots, sizeof(trigSlots));
    memcpy(previousCvSlot, cvSlots, sizeof(cvSlots));
    // 1) clock bus (rack §2): one fractional PPQ accumulator at the global
    //    BPM; MIDI sources advance by integer ticks. The increment IS the
    //    delta — intPhase only wraps cosmetically so the float never loses
    //    precision against the per-block increments.
    const float bpm = ModTransport::GetBPM();
    const float intDelta = 24.0f * (bpm / 60.0f) * dt;
    intPhase += intDelta;
    if (intPhase >= 65536.0f) intPhase -= 65536.0f;
    const int midiTicks = ModTransport::TicksThisBlock();
    const bool running = ModTransport::IsRunning();
    const bool globalMidi = ModTransport::GetGlobalClockMidi();
    const bool startReset = ModTransport::TakeStartReset();

    for (int i = 0; i < 2; ++i) {
        // ---- GATE ----
        SEQ::GateSource &g = gGate[i];
        const bool gUseMidi = (gateClk[i] == 2) || (gateClk[i] == 0 && globalMidi);
        const bool gActive = gUseMidi ? running : true;   // MIDI sources freeze while stopped
        // FA re-phase: MIDI-clocked sources snap to the downbeat (rack
        // behavior; the broadcast is consumed read-only here). Runs BEFORE
        // this block's tick advance so the downbeat re-phases first.
        if (startReset && gUseMidi && running) {
            g.step = 0;
            g.level = (g.mask >> 15) & 1u;
            gPhase[i] = 0.0f;
        }
        const float gDivPpq = (float)SEQ::kDivisions[gateDiv[i]].ppq;
        const float gAdv = gActive
            ? (gUseMidi ? (float)midiTicks / gDivPpq : intDelta / gDivPpq)
            : 0.0f;
        const float gPrev = gPhase[i];
        gPhase[i] += gAdv;
        if (gPhase[i] >= 1.0f) gPhase[i] -= floorf(gPhase[i]);
        const uint32_t gWraps = (uint32_t)(gPrev + gAdv);
        if (gWraps) SEQ::GateAdvanceStep(g, gWraps);

        // Gate length: the step's level holds only for the first glen
        // fraction of the division cycle. A stopped MIDI source releases
        // the window (a frozen open gate would hang a mapped note).
        const bool gHigh = gActive && (g.level != 0) && (gPhase[i] < g.glen);
        const float gTarget = gHigh ? 1.0f : 0.0f;
        // Asymmetric one-pole slew for click-free continuous gating
        // (~1 ms attack, ~3 ms release — rack values).
        if (gTarget > gateOut[i]) {
            gateOut[i] += 0.4f * (gTarget - gateOut[i]);
            if (gateOut[i] > 0.99f) gateOut[i] = 1.0f;
        } else {
            gateOut[i] += 0.2f * (gTarget - gateOut[i]);
            if (gateOut[i] < 0.01f) gateOut[i] = 0.0f;
        }
        // active-low convention: 0 = gate on, 1 = idle
        if (gateTrigSlot[i] >= 0) trig[gateTrigSlot[i]] = gHigh ? 0 : 1;
        if (gateCvSlot[i] >= 0) cv[gateCvSlot[i]] = gateOut[i];
    }

    for (int i = 0; i < 2; ++i) {
        // ---- ARP ---- (rack §3 MOD_ARP branch)
        SEQ::ArpSource &s = gArp[i];
        SEQ::ArpExt &e = gArpExt[i];
        const bool aUseMidi = (arpClk[i] == 2) || (arpClk[i] == 0 && globalMidi);
        const bool aActive = aUseMidi ? running : true;
        // FA re-phase to the downbeat, before this block's tick advance
        if (startReset && aUseMidi && running) {
            s.step = 0;
            s.level = 0u;
            s.held = 0u;
            s.retrig_edge = 0u;
            aPhase[i] = 0.0f;
        }
        if (!aActive || (startReset && aUseMidi)) {
            s_strumBurst[i].active = false;
            s.level = s.held = s.cur_acc = s.cur_hold = 0;
            s_vargBurst[i] = s_vargGhost[i] = s_vargGlide[i] = 0;
        }
        const float aDivPpq = (float)SEQ::kDivisions[arpDiv[i]].ppq;
        const float aAdv = aActive
            ? (aUseMidi ? (float)midiTicks / aDivPpq : intDelta / aDivPpq)
            : 0.0f;
        const float aPrev = aPhase[i];
        aPhase[i] += aAdv;
        if (aPhase[i] >= 1.0f) aPhase[i] -= floorf(aPhase[i]);
        const uint32_t aWraps = (uint32_t)(aPrev + aAdv);
        if (aWraps) {
            const uint8_t prevSt = s.step;
            s.step = (uint8_t)((s.step + aWraps) & 0x0Fu);
            s.ticks += aWraps;
            const uint8_t st = s.step;
            const bool hadBurst = s_strumBurst[i].active;
            if (hadBurst) {
                StrumBurst &burst = s_strumBurst[i];
                if (aWraps >= (uint32_t)burst.count - burst.idx) {
                    burst.active = false;
                    burst.idx = burst.count;
                    burst.countdown = 0;
                    burst.rng = 0x1a2b3c4du + s.rng;
                }
            }
            if (st < prevSt || (s.ticks & 0x0Fu) == 0u) {
                // bar boundary: chord drift cadence + fill bar + rebuild.
                // VARG advances its own progression inside ArpBuildVarg (one
                // chord per bar) — skip the shared W7/Neo drift for it.
                e.bar_count++;
                const uint8_t driftBars = (s.p[3] >= 60) ? 4 : 8;
                if (e.bar_count >= driftBars) {
                    e.bar_count = 0;
                    const bool isVargAlgo = (s.algo == SEQ::kArpAlgoVarg ||
                                             s.algo == SEQ::kArpAlgoVargChord);
                    if (!isVargAlgo &&
                        (s.p[3] > 15 || s.root_mode >= 1))
                        SEQ::ArpNextChord(s, e, gNeoOp);
                    if (!isVargAlgo) {
                        // rack shared-harmony lock: mirror the drift to the
                        // sibling so both ARPs stay chord-locked
                        SEQ::ArpSource &so = gArp[i ^ 1];
                        SEQ::ArpExt &eo = gArpExt[i ^ 1];
                        so.root = s.root;
                        eo.is_minor_triad = e.is_minor_triad;
                        eo.deg = e.deg;
                        SEQ::ArpRebuildScale(so, eo);
                    }
                    e.dd_fill = 0;
                    e.turn_type = (uint8_t)((s.ticks >> 4) & 3u);
                } else if (e.bar_count == driftBars - 1 && s.p[3] >= 40) {
                    e.dd_fill = 1;
                    e.turn_type = (uint8_t)((s.ticks >> 4) & 3u);
                }
                if (s.algo == SEQ::kArpAlgoVarg ||
                    s.algo == SEQ::kArpAlgoVargChord) {
                    // P5 = chord duration as a 4-way choice: 1/2/4/8 bars.
                    static constexpr uint8_t kBars[4] = {1, 2, 4, 8};
                    const int chordBars =
                        kBars[(s.p[4] * 4) / 101 < 4 ? (s.p[4] * 4) / 101 : 3];
                    bool advance = false;
                    if (e.st.varg.bars_held + 1 >= chordBars) {
                        e.st.varg.bars_held = 0;
                        advance = true;
                        s_vargChordChange[i] = 1;
                    } else {
                        ++e.st.varg.bars_held;
                    }
                    SEQ::ArpRebuildPattern(s, e, advance, false);
                } else if (s.algo == SEQ::kArpAlgoHyp1) {
                    // HYP-1: in-place step mutation on loop wrap
                    const uint8_t mutProb = (uint8_t)((uint16_t)s.p[3] * 50 / 100);
                    for (int k = 0; k < 16; ++k)
                        if (SEQ::ArpRand(s, 100) < mutProb) SEQ::ArpRollHyp1Step(s, e, (uint8_t)k);
                } else {
                    SEQ::ArpRebuildPattern(s, e);
                }
            }
            const bool continuingBurst = hadBurst && s_strumBurst[i].active;
            SEQ::ArpStep cur = e.steps[st];
            const bool isVarg = s.algo == SEQ::kArpAlgoVarg || s.algo == SEQ::kArpAlgoVargChord;
            if (e.dd_fill && !isVarg) SEQ::ArpApplyTurnaround(s, e, st, cur);
            if (continuingBurst) {
                s.level = 0;
            } else if (cur.flags & 1u) {
                const SEQ::ArpStep &prev = e.steps[prevSt];
                const bool prevSlid = ((prev.flags & 5u) == 5u) && s.held;
                s.cur_note = SEQ::ArpCalcNote(s, e, cur);
                s.retrig_edge = prevSlid ? 0u : 1u;
                s.cur_acc = (cur.flags & 2u) ? 1u : 0u;
                s.cur_hold = (cur.flags & 4u) ? 1u : 0u;
                s.level = 1u; s.held = 1u;
                // strum companion: this gate edge carries the step's stroke
                // direction (pendulum, stamped by ArpBuildVarg) and accent
                s_vargStroke[i] = (cur.flags & 8u) ? -1 : +1;
                s_vargFlags[i] = (cur.flags & 2u) ? 1u : 0u;
                s_vargString[i] = (int8_t)(cur.str & 3u);
                s_vargPedal[i] = (cur.flags & 16u) ? 1u : 0u;
                // Burst mode always expands the selected nine-way order.
                StrumBurst &burst = s_strumBurst[i];
                burst.active = false;
                const bool vargChord = s.algo == SEQ::kArpAlgoVargChord;
                if (vargChord && s.p[8] < 51) {
                    static constexpr uint8_t styles[9] = {
                        tbd::STRUM_DOWN, tbd::STRUM_UP, tbd::STRUM_PENDULUM,
                        tbd::STRUM_CONVERGE, tbd::STRUM_DIVERGE, tbd::STRUM_RANDOM,
                        tbd::STRUM_THRU, tbd::STRUM_BROKEN1, tbd::STRUM_BROKEN2};
                        const int style = styles[SEQ::VargOrderFromParam(s.p[5])];
                    const uint8_t order[3] = {0, 1, 2};
                    const tbd::StrumPlan plan = tbd::StrumBuildPlan(
                        order, 3, (uint8_t)style,
                        (cur.flags & 8u) ? -1 : +1, false,
                        (cur.flags & 2u) != 0, 0, burst.rng);
                    burst.count = (uint8_t)ClampInt(plan.n + 1, 1, 7);
                    burst.idx = 0;
                    burst.countdown = 0;
                    const int gaps = burst.count - 1;
                    const uint32_t requestedBlocks = (uint32_t)(s.p[6] * .00045f / dt);
                    const float stepSeconds = aDivPpq / (24.f * bpm / 60.f);
                    const uint32_t availableBlocks = (uint32_t)(stepSeconds * .7f / dt);
                    const uint32_t totalBlocks = requestedBlocks < availableBlocks ? requestedBlocks : availableBlocks;
                    burst.totalSpread = (uint16_t)ClampInt((int)totalBlocks, 0, 65535);
                    burst.spacing = (uint8_t)ClampInt(gaps ? (int)(totalBlocks / gaps) : 2, 2, 60);
                    const int voicing = SEQ::kVargToVoicing[e.st.varg.curQual % SEQ::VQ_COUNT];
                    const int8_t *offs = SEQ::kVargVoicing3[voicing];
                    const int rootNote = 48 + s.root + e.st.varg.curOff;
                    const bool burst16 = tbd::StrumMaskIs16thGrid(SEQ::VargEffectiveMask(s));
                    const float atten = (burst16 && (st & 1u)) ? .8f : 1.f;
                    burst.accent = (cur.flags & 2u) != 0;
                    burst.slot[0] = 3;
                    burst.note[0] = (int8_t)(48 + s.root - 12);
                    burst.vel[0] = (uint8_t)(.85f * atten * 70.f);
                    if (burst.accent) burst.vel[0] = (uint8_t)ClampInt(burst.vel[0] + 10, 1, 100);
                    for (uint8_t k = 0; k < plan.n; ++k) {
                        const int slot = plan.slot[k] % 3u;
                        burst.slot[k + 1] = (int8_t)slot;
                        burst.note[k + 1] = (int8_t)(rootNote + offs[slot]);
                        int velocity = (int)(plan.vel[k] * atten * 100.f);
                        if (burst.accent) velocity += 10;
                        burst.vel[k + 1] = (uint8_t)ClampInt(velocity, 1, 100);
                    }
                    burst.kind = 0;
                    burst.glide = s_vargChordChange[i] != 0;
                    s_vargChordChange[i] = 0;
                    burst.active = plan.n > 0;
                    s.level = 0;
                }
            } else {
                s.level = 0u; s.retrig_edge = 0u; s.held = 0u;
                s_vargPedal[i] = 0;
                // Research §2 rule 5: muted ghost thuds on silent steps and
                // a fret/slide transient just before the bar's chord change.
                StrumBurst &burst = s_strumBurst[i];
                burst.active = false;
                if (s.algo == SEQ::kArpAlgoVargChord && s.p[7] > 0) {
                    uint8_t kind = 0;
                    if (st == 15) kind = 2;
                    else if ((SEQ::ArpU32Rand(s) % 100u) < s.p[7]) kind = 1;
                    if (kind) {
                        burst.count = 1; burst.idx = 0; burst.countdown = 0;
                        burst.spacing = 2; burst.kind = kind;
                        burst.slot[0] = (int8_t)(cur.str % 3u);
                        burst.note[0] = (int8_t)s.cur_note;
                        burst.vel[0] = (kind == 1) ? 25u : 12u;
                        burst.glide = false;
                        burst.active = true;
                    }
                }
            }
        }

        // pitch glide: GLIDE=0 instant (TBD03 does the slide via slide_level),
        // else ms slew; fresh (non-slid) attacks always snap. Between steps
        // cur_note is unchanged, so the slew sits still and the CV holds the
        // last note (spec: "holds last note between gated steps").
        const int32_t targetQ16 = ((int32_t)s.cur_note) << 16;
        if (arpGlideMs[i] <= 0 || s.retrig_edge || !s.held) {
            e.current_pitch_q16 = targetQ16;
            e.target_pitch_q16 = targetQ16;
        } else {
            e.target_pitch_q16 = targetQ16;
            const int32_t diff = e.target_pitch_q16 - e.current_pitch_q16;
            if (diff) {
                int32_t alpha = (int32_t)(((float)dt / ((float)arpGlideMs[i] * 0.001f)) * 65536.0f);
                if (alpha > 65536) alpha = 65536;
                e.current_pitch_q16 += (int32_t)(((int64_t)diff * alpha) >> 16);
            }
        }
        // accent capacitor (rack 1567-1572): charges on accents, bleeds off
        if (s.cur_acc && s.level) {
            e.accent_cap_q16 += 16384;
            if (e.accent_cap_q16 > 65536) e.accent_cap_q16 = 65536;
        }
        e.accent_cap_q16 = (int32_t)(((int64_t)e.accent_cap_q16 * 63000) >> 16);

        // outputs (every owned slot written every block; active-low trigs)
        // Pitch CV is ABSOLUTE: relative to MIDI 48 (C3) — the convention the
        // plugin pitch bases are anchored to. Subtracting the movable arp
        // root here transposed the sounding key with every root change
        // (manual or W7/Neo drift) — the plugins never played the actual
        // notes.
        // Octave transpose lives at the CV level (one octave = 0.2 V) so it
        // applies identically to every generator and the MIDI-note fold in
        // ArpCalcNote cannot swallow it.
        const float pitchCv =
            ((float)((int32_t)(e.current_pitch_q16 >> 16) - 48) / 12.0f
             + (float)arpOct[i]) * 0.2f;
        const bool isVargNote = s.algo == SEQ::kArpAlgoVarg || s.algo == SEQ::kArpAlgoVargChord;
        if (!isVargNote) {
            arpPitchOut[i] = pitchCv;
        } else if (aWraps && s.level) {
            if (s_vargPedal[i]) {
                arpPitchOut[i] = (float)(s.root - 12) / 12.f * .2f + arpOct[i] * .2f;
                s_vargPedal[i] = 0;
            } else {
                arpPitchOut[i] = pitchCv;
            }
            s_vargGlide[i] = s_vargChordChange[i];
            s_vargChordChange[i] = 0;
        }
        // Burst notes and companions are latched on the same block as their
        // one-block pulse. Pitch holds through idle blocks and after the burst.
        StrumBurst &burst = s_strumBurst[i];
        bool burstEmit = false;
        s_vargBurst[i] = 0;
        s_vargBurstVel[i] = 0;
        s_vargGhost[i] = 0;
        if (!aWraps || !s.level) s_vargGlide[i] = 0;
    if (burst.active && aActive && burst.idx < burst.count) {
        if (burst.countdown) {
            --burst.countdown;
        } else {
            burstEmit = true;
            arpPitchOut[i] = ((float)(burst.note[burst.idx] - 48) / 12.f + arpOct[i]) * .2f;
            s_vargString[i] = burst.slot[burst.idx];
            s_vargBurstVel[i] = burst.vel[burst.idx];
            s_vargGhost[i] = burst.kind;
            s_vargGlide[i] = burst.glide ? 1 : 0;
            ++burst.idx;
            if (burst.idx == burst.count) {
                burst.active = false;
                burst.countdown = 0;
            } else {
                const uint32_t elapsed = (uint32_t)(burst.idx - 1) * burst.spacing;
                const uint32_t remaining = burst.totalSpread > elapsed ? burst.totalSpread - elapsed : 0;
                const uint8_t gaps = burst.count - burst.idx;
                burst.countdown = gaps ? (uint8_t)ClampInt((int)(remaining / gaps), 2, 60) - 1 : 0;
            }
        }
        s_vargBurst[i] = 1;
    }
        if (arpPitchSlot[i] >= 0) cv[arpPitchSlot[i]] = arpPitchOut[i];

        const float gWin = s.cur_hold ? 1.f : .55f;
        const bool gateOpen = aActive && s.level && aPhase[i] < gWin;
        const bool burstMode = s.algo == SEQ::kArpAlgoVargChord && s.p[8] < 51;
        const bool gateHigh = aActive && (burstMode ? burstEmit : gateOpen);
        const bool accentHigh = aActive && (burstMode ? (burstEmit && burst.accent)
                                                    : (s.cur_acc && gateOpen));
        const bool slideHigh = aActive && s.cur_hold && gateOpen;
        arpGateOut[i] = gateHigh ? 0 : 1;
        arpAccentOut[i] = accentHigh ? 0 : 1;
        arpSlideOut[i] = slideHigh ? 0 : 1;
        if (arpGateSlot[i] >= 0) trig[arpGateSlot[i]] = arpGateOut[i];
        if (arpAccSlot[i] >= 0) trig[arpAccSlot[i]] = arpAccentOut[i];
        if (arpSlideSlot[i] >= 0) trig[arpSlideSlot[i]] = arpSlideOut[i];
        const float aTarget = gateHigh ? (s.cur_acc ? 1.f : .72f) : 0.f;
        arpOut[i] += (aTarget - arpOut[i]) * .5f;
    companion[i] = VargCompanion{};
    if (isVargNote) {
            companion[i].voicing = (s.algo == SEQ::kArpAlgoVargChord && s.p[8] < 51)
                ? (int)SEQ::kVargToVoicing[e.st.varg.curQual % SEQ::VQ_COUNT] : -1;
            companion[i].string = s_vargString[i];
            companion[i].pitchSlot = arpPitchSlot[i];
            companion[i].gateSlot = arpGateSlot[i];
            companion[i].stroke = s_vargStroke[i];
            companion[i].flags = s_vargFlags[i];
            companion[i].burst = s_vargBurst[i];
            companion[i].velocity = s_vargBurstVel[i];
            companion[i].ghost = s_vargGhost[i];
            companion[i].glide = s_vargGlide[i];
        }
    }
    // snapshot: [g1 g2 a1g a2g a1a a2a a1s a2s | a1step a2step | a1note a2note]
    uint32_t snap = (uint32_t)gGate[0].level | ((uint32_t)gGate[1].level << 1)
        | ((uint32_t)(gArp[0].level) << 2) | ((uint32_t)(gArp[1].level) << 3)
        | ((uint32_t)(gArp[0].cur_acc && gArp[0].level) << 4)
        | ((uint32_t)(gArp[1].cur_acc && gArp[1].level) << 5)
        | ((uint32_t)(gArp[0].cur_hold && gArp[0].level) << 6)
        | ((uint32_t)(gArp[1].cur_hold && gArp[1].level) << 7)
        | ((uint32_t)(gArp[0].step & 0xFu) << 8) | ((uint32_t)(gArp[1].step & 0xFu) << 12)
        | ((uint32_t)(gArp[0].cur_note & 0xFFu) << 16) | ((uint32_t)(gArp[1].cur_note & 0xFFu) << 24);
    outSnapshot.store(snap, std::memory_order_relaxed);
}

uint32_t SeqEngine::GetOutputSnapshot() { return outSnapshot.load(std::memory_order_relaxed); }

SeqEngine::VargState SeqEngine::GetVargState(int i) {
    if (i < 0 || i > 1) return VargState{-1, -1, -1, -1, 0, 0, 0, 0, 0, 0};
    StateLock lock(stateMutex);
    const VargCompanion &v = companion[i];
    return VargState{(int8_t)v.voicing, (int8_t)v.string, (int8_t)v.pitchSlot,
                     (int8_t)v.gateSlot, v.stroke, v.flags, v.burst,
                     v.velocity, v.ghost, v.glide};
}

// ---- gate setters (mask recomputed setter-side, rack discipline) ----
int SeqEngine::GetGateMode(int i) { StateLock lock(stateMutex); return gGate[i].mode; }
void SeqEngine::SetGateMode(int i, int v) { StateLock lock(stateMutex); gGate[i].mode = (uint8_t)ClampInt(v, 0, 18); SEQ::GateRecompute(gGate[i]); }
int SeqEngine::GetGateDens(int i) { StateLock lock(stateMutex); return gGate[i].dens; }
void SeqEngine::SetGateDens(int i, int v) { StateLock lock(stateMutex); gGate[i].dens = (uint8_t)ClampInt(v, 1, 16); SEQ::GateRecompute(gGate[i]); }
int SeqEngine::GetGateSync(int i) { StateLock lock(stateMutex); return gGate[i].sync; }
void SeqEngine::SetGateSync(int i, int v) { StateLock lock(stateMutex); gGate[i].sync = (uint8_t)ClampInt(v, 0, 100); SEQ::GateRecompute(gGate[i]); }
float SeqEngine::GetGateLen(int i) { StateLock lock(stateMutex); return gGate[i].glen; }
void SeqEngine::SetGateLen(int i, float v) { StateLock lock(stateMutex); gGate[i].glen = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
int SeqEngine::GetGateClk(int i) { StateLock lock(stateMutex); return gateClk[i]; }
void SeqEngine::SetGateClk(int i, int v) { StateLock lock(stateMutex); gateClk[i] = ClampInt(v, 0, 2); }
int SeqEngine::GetGateDiv(int i) { StateLock lock(stateMutex); return gateDiv[i]; }
void SeqEngine::SetGateDiv(int i, int v) { StateLock lock(stateMutex); gateDiv[i] = ClampInt(v, 0, SEQ::kDivCount - 1); }
int SeqEngine::GetGateTrigSlot(int i) { StateLock lock(stateMutex); return gateTrigSlot[i]; }
void SeqEngine::SetGateTrigSlot(int i, int s) { StateLock lock(stateMutex); gateTrigSlot[i] = ClampTrigSlot(s); }
int SeqEngine::GetGateCvSlot(int i) { StateLock lock(stateMutex); return gateCvSlot[i]; }
void SeqEngine::SetGateCvSlot(int i, int s) { StateLock lock(stateMutex); gateCvSlot[i] = ClampCVSlot(s); }
uint32_t SeqEngine::GetGateMask(int i) { StateLock lock(stateMutex); return gGate[i].mask; }

uint32_t SeqEngine::GetArpGateMask(int i) {
    StateLock lock(stateMutex);
    uint32_t m = 0;
    for (int st = 0; st < 16; ++st)
        if (gArpExt[i].steps[st].flags & 1u) m |= (1u << (15 - st));
    return m;
}

uint32_t SeqEngine::GetArpAccMask(int i) {
    StateLock lock(stateMutex);
    uint32_t m = 0;
    for (int st = 0; st < 16; ++st)
        if (gArpExt[i].steps[st].flags & 2u) m |= (1u << (15 - st));
    return m;
}

uint32_t SeqEngine::GetArpSlideMask(int i) {
    StateLock lock(stateMutex);
    uint32_t m = 0;
    for (int st = 0; st < 16; ++st)
        if (gArpExt[i].steps[st].flags & 4u) m |= (1u << (15 - st));
    return m;
}

int SeqEngine::GetGateStep(int i) { StateLock lock(stateMutex); return gGate[i].step; }

// ---- arp setters ----
int SeqEngine::GetVargVoicing(int i) {
    StateLock lock(stateMutex);
    if (i < 0 || i > 1) return -1;
    if (gArp[i].algo != SEQ::kArpAlgoVargChord || gArp[i].p[8] >= 51) return -1;
    // curQual is the VARG chord QUALITY (0..8); TBDings' performance_state.chord
    // is a Rings VOICING ROW (0..16). Map through the shared table so the
    // exact progression quality reaches the string synth (e.g. minor -> row 3).
    return (int)SEQ::kVargToVoicing[gArpExt[i].st.varg.curQual % SEQ::VQ_COUNT];
}
int8_t SeqEngine::GetVargStroke(int i) {
    StateLock lock(stateMutex);
    if (i < 0 || i > 1) return 0;
    if (gArp[i].algo != SEQ::kArpAlgoVargChord) return 0;
    return s_vargStroke[i];
}
uint8_t SeqEngine::GetVargFlags(int i) {
    StateLock lock(stateMutex);
    if (i < 0 || i > 1) return 0;
    if (gArp[i].algo != SEQ::kArpAlgoVargChord) return 0;
    return s_vargFlags[i];
}
int8_t SeqEngine::GetVargString(int i) {
    StateLock lock(stateMutex);
    if (i < 0 || i > 1) return -1;
    // Both VARG algorithms emit per-note string indices: the note ostinato
    // (algo 5) and the whole-chord engine (algo 6, where it is the base
    // anchor). Other algos publish nothing.
    if (gArp[i].algo != SEQ::kArpAlgoVarg &&
        gArp[i].algo != SEQ::kArpAlgoVargChord) return -1;
    return s_vargString[i];
}
uint8_t SeqEngine::GetVargBurst(int i) {
    StateLock lock(stateMutex);
    if (i < 0 || i > 1) return 0;
    return s_vargBurst[i];
}
uint8_t SeqEngine::GetVargBurstVel(int i) {
    StateLock lock(stateMutex);
    if (i < 0 || i > 1) return 0;
    return s_vargBurstVel[i];
}
uint8_t SeqEngine::GetVargGhost(int i) {
    StateLock lock(stateMutex);
    if (i < 0 || i > 1) return 0;
    return s_vargGhost[i];
}
uint8_t SeqEngine::GetVargGlide(int i) {
    StateLock lock(stateMutex);
    if (i < 0 || i > 1) return 0;
    return s_vargGlide[i];
}
int SeqEngine::GetArpAlgo(int i) {
    StateLock lock(stateMutex);
    // VARG has one algo; its range is a separate Chord Range control.
    if (gArp[i].algo == SEQ::kArpAlgoVarg ||
        gArp[i].algo == SEQ::kArpAlgoVargChord) return gArp[i].algo * 3;
    return gArp[i].algo * 3 + (gArp[i].range - 1);
}
int SeqEngine::GetArpRange(int i) { StateLock lock(stateMutex); return gArp[i].range; }
void SeqEngine::SetArpRange(int i, int r) {
    StateLock lock(stateMutex);
    gArp[i].range = (uint8_t)ClampInt(r, 1, 3);
    SEQ::ArpRebuildPattern(gArp[i], gArpExt[i], false);
}
void SeqEngine::SetArpAlgo(int i, int v) {
    StateLock lock(stateMutex);
    v = ClampInt(v, 0, SEQ::kArpAlgoCount * 3 - 1);   // 7 algos x 3 ranges
    const uint8_t oldAlgo = gArp[i].algo;
    gArp[i].algo = (uint8_t)(v / 3);
    gArp[i].range = (uint8_t)(1 + v % 3);
    const bool wasVarg = (oldAlgo == SEQ::kArpAlgoVarg || oldAlgo == SEQ::kArpAlgoVargChord);
    const bool isVarg = (gArp[i].algo == SEQ::kArpAlgoVarg || gArp[i].algo == SEQ::kArpAlgoVargChord);
    s_strumBurst[i].active = false;
    if (isVarg && !wasVarg)
        gArpExt[i].st.varg.chordIdx = 0xFF;   // fresh switch: start on the tonic
    SEQ::ArpRebuildPattern(gArp[i], gArpExt[i], false);
}
int SeqEngine::GetArpP(int i, int k) { StateLock lock(stateMutex); return (k < 0 || k > 8) ? 0 : gArp[i].p[k]; }
void SeqEngine::SetArpP(int i, int k, int v) {
    StateLock lock(stateMutex);
    if (k < 0 || k > 8) return;
    const int previous = gArp[i].p[k];
    gArp[i].p[k] = (uint8_t)ClampInt(v, 0, 100);
    // Progression and quality changes explicitly resolve a new voicing; other
    // edits rebuild around the existing chord without rerolling DISS quality.
    const bool revoice = k == 0 || k == 2;
    SEQ::ArpRebuildPattern(gArp[i], gArpExt[i], false, revoice && previous != gArp[i].p[k]);
}
int SeqEngine::GetArpGlide(int i) { StateLock lock(stateMutex); return arpGlideMs[i]; }
void SeqEngine::SetArpGlide(int i, int v) { StateLock lock(stateMutex); arpGlideMs[i] = ClampInt(v, 0, 100); }
int SeqEngine::GetArpOct(int i) { StateLock lock(stateMutex); return arpOct[i]; }
void SeqEngine::SetArpOct(int i, int v) { StateLock lock(stateMutex); arpOct[i] = ClampInt(v, -2, 2); }
int SeqEngine::GetArpClk(int i) { StateLock lock(stateMutex); return arpClk[i]; }
void SeqEngine::SetArpClk(int i, int v) { StateLock lock(stateMutex); arpClk[i] = ClampInt(v, 0, 2); }
int SeqEngine::GetArpDiv(int i) { StateLock lock(stateMutex); return arpDiv[i]; }
void SeqEngine::SetArpDiv(int i, int v) { StateLock lock(stateMutex); arpDiv[i] = ClampInt(v, 0, SEQ::kDivCount - 1); }
int SeqEngine::GetArpPitchSlot(int i) { StateLock lock(stateMutex); return arpPitchSlot[i]; }
void SeqEngine::SetArpPitchSlot(int i, int s) { StateLock lock(stateMutex); arpPitchSlot[i] = ClampCVSlot(s); }
int SeqEngine::GetArpGateSlot(int i) { StateLock lock(stateMutex); return arpGateSlot[i]; }
void SeqEngine::SetArpGateSlot(int i, int s) { StateLock lock(stateMutex); arpGateSlot[i] = ClampTrigSlot(s); }
int SeqEngine::GetArpAccSlot(int i) { StateLock lock(stateMutex); return arpAccSlot[i]; }
void SeqEngine::SetArpAccSlot(int i, int s) { StateLock lock(stateMutex); arpAccSlot[i] = ClampTrigSlot(s); }
int SeqEngine::GetArpSlideSlot(int i) { StateLock lock(stateMutex); return arpSlideSlot[i]; }
void SeqEngine::SetArpSlideSlot(int i, int s) { StateLock lock(stateMutex); arpSlideSlot[i] = ClampTrigSlot(s); }

// ---- shared root/scale/neo-op (rack shared-root behavior) ----
int SeqEngine::GetRoot() { StateLock lock(stateMutex); return gArp[0].root; }
void SeqEngine::SetRoot(int v) {
    StateLock lock(stateMutex);
    const uint8_t pc = (uint8_t)ClampInt(v, 0, 11);
    gArp[0].root = gArp[1].root = pc;   // base note only; the pattern keeps rolling
}
int SeqEngine::GetRootMode() { StateLock lock(stateMutex); return gArp[0].root_mode; }
void SeqEngine::SetRootMode(int v) {
    StateLock lock(stateMutex);
    const uint8_t m = (uint8_t)ClampInt(v, 0, 2);
    gArp[0].root_mode = gArp[1].root_mode = m;
    for (int i = 0; i < 2; ++i) {
        SEQ::ArpRebuildScale(gArp[i], gArpExt[i]);
        SEQ::ArpRebuildPattern(gArp[i], gArpExt[i], false);
    }
}
int SeqEngine::GetNeoOp() { StateLock lock(stateMutex); return gNeoOp; }
void SeqEngine::SetNeoOp(int v) { StateLock lock(stateMutex); gNeoOp = (uint8_t)ClampInt(v, 0, 7); }
int SeqEngine::GetScale() { StateLock lock(stateMutex); return gArp[0].scale_idx & 0x3Fu; }
void SeqEngine::SetScale(int v) {
    StateLock lock(stateMutex);
    const uint8_t sc = (uint8_t)ClampInt(v, 0, SEQ::SCALE_COUNT - 1);
    gArp[0].scale_idx = gArp[1].scale_idx = sc;
    for (int i = 0; i < 2; ++i) {
        SEQ::ArpRebuildScale(gArp[i], gArpExt[i]);
        SEQ::ArpRebuildPattern(gArp[i], gArpExt[i], false);
    }
}

// ---- persistence: flat spec §4.4 JSON in the "seq" section ----
// mod-config.jsn is hand-editable, so every member is type-checked and
// clamped on load; unknown members are ignored.

void SeqEngine::SerializeConfig(char *dst, int cap) {
    StateLock lock(stateMutex);
    std::string j = "{\"gates\":[";
    for (int i = 0; i < 2; ++i) {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "{\"mode\":%d,\"dens\":%d,\"sync\":%d,\"glen\":%.3g,\"clk\":%d,"
                 "\"div\":%d,\"trigSlot\":%d,\"cvSlot\":%d}",
                 gGate[i].mode, gGate[i].dens, gGate[i].sync, (double)gGate[i].glen,
                 gateClk[i], gateDiv[i], gateTrigSlot[i], gateCvSlot[i]);
        if (i) j += ",";
        j += buf;
    }
    j += "],\"arps\":[";
    for (int i = 0; i < 2; ++i) {
        char buf[200];
        snprintf(buf, sizeof(buf),
                 "{\"algo\":%d,\"range\":%d,\"p\":[%d,%d,%d,%d,%d,%d,%d,%d,%d],\"glide\":%d,\"oct\":%d,\"clk\":%d,"
                 "\"div\":%d,\"pitchSlot\":%d,\"gateSlot\":%d,\"accSlot\":%d,\"slideSlot\":%d}",
                 gArp[i].algo, gArp[i].range, gArp[i].p[0], gArp[i].p[1], gArp[i].p[2],
                 gArp[i].p[3], gArp[i].p[4], gArp[i].p[5], gArp[i].p[6], gArp[i].p[7],
                 gArp[i].p[8], arpGlideMs[i], arpOct[i],
                 arpClk[i], arpDiv[i],
                 arpPitchSlot[i], arpGateSlot[i], arpAccSlot[i], arpSlideSlot[i]);
        if (i) j += ",";
        j += buf;
    }
    char sh[96];
    // "root" is the MIDI note (spec §4.4 example: 48 = C3); API uses pitch class.
    snprintf(sh, sizeof(sh),
             "],\"shared\":{\"rootMode\":%d,\"root\":%d,\"neoOp\":%d,\"scale\":%d}}",
             gArp[0].root_mode, 48 + gArp[0].root, gNeoOp, gArp[0].scale_idx & 0x3Fu);
    j += sh;
    if ((int)j.size() < cap) {
        memcpy(dst, j.c_str(), j.size() + 1);
    } else if (cap > 0) {
        dst[0] = 0;
    }
}

bool SeqEngine::DeserializeConfig(const char *json) {
    StateLock lock(stateMutex);
    using namespace rapidjson;
    Document d;
    if (d.Parse(json).HasParseError() || !d.IsObject()) return false;
    bool revoiceVarg[2] = {false, false};

    if (d.HasMember("gates") && d["gates"].IsArray()) {
        const Value &arr = d["gates"];
        for (SizeType i = 0; i < arr.Size() && i < 2; i++) {
            const Value &g = arr[i];
            if (!g.IsObject()) continue;
            int n = 0;
            float f = 0.0f;
            if (g.HasMember("mode") && g["mode"].IsInt()) gGate[i].mode = (uint8_t)ClampInt(g["mode"].GetInt(), 0, 18);
            if (g.HasMember("dens") && g["dens"].IsInt()) gGate[i].dens = (uint8_t)ClampInt(g["dens"].GetInt(), 1, 16);
            if (g.HasMember("sync") && g["sync"].IsInt()) gGate[i].sync = (uint8_t)ClampInt(g["sync"].GetInt(), 0, 100);
            if (g.HasMember("glen") && g["glen"].IsNumber()) {
                f = g["glen"].GetFloat();
                gGate[i].glen = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
            }
            if (g.HasMember("clk") && g["clk"].IsInt()) gateClk[i] = ClampInt(g["clk"].GetInt(), 0, 2);
            if (g.HasMember("div") && g["div"].IsInt()) gateDiv[i] = ClampInt(g["div"].GetInt(), 0, SEQ::kDivCount - 1);
            if (g.HasMember("trigSlot") && g["trigSlot"].IsInt()) gateTrigSlot[i] = ClampTrigSlot(g["trigSlot"].GetInt());
            if (g.HasMember("cvSlot") && g["cvSlot"].IsInt()) gateCvSlot[i] = ClampCVSlot(g["cvSlot"].GetInt());
            (void)n;
        }
    }
    if (d.HasMember("arps") && d["arps"].IsArray()) {
        const Value &arr = d["arps"];
        for (SizeType i = 0; i < arr.Size() && i < 2; i++) {
            const Value &a = arr[i];
            if (!a.IsObject()) continue;
            const uint8_t oldAlgo = gArp[i].algo;
            const uint8_t oldProg = gArp[i].p[0];
            const uint8_t oldQuality = gArp[i].p[2];
            if (a.HasMember("algo") && a["algo"].IsInt()) gArp[i].algo = (uint8_t)ClampInt(a["algo"].GetInt(), 0, SEQ::kArpAlgoCount - 1);
            if (a.HasMember("range") && a["range"].IsInt()) gArp[i].range = (uint8_t)ClampInt(a["range"].GetInt(), 1, 3);
            const bool oldVarg = oldAlgo == SEQ::kArpAlgoVarg || oldAlgo == SEQ::kArpAlgoVargChord;
            const bool newVarg = gArp[i].algo == SEQ::kArpAlgoVarg || gArp[i].algo == SEQ::kArpAlgoVargChord;
            if (newVarg && !oldVarg) gArpExt[i].st.varg.chordIdx = 0xFF;
            if (a.HasMember("p") && a["p"].IsArray()) {
                const Value &p = a["p"];
                for (SizeType k = 0; k < p.Size() && k < 9; k++)
                    if (p[k].IsInt()) gArp[i].p[k] = (uint8_t)ClampInt(p[k].GetInt(), 0, 100);
            }
            const bool revoice = oldVarg && newVarg &&
                (oldProg != gArp[i].p[0] || oldQuality != gArp[i].p[2]);
            revoiceVarg[i] = revoice || (newVarg && !oldVarg);
            if (a.HasMember("glide") && a["glide"].IsInt()) arpGlideMs[i] = ClampInt(a["glide"].GetInt(), 0, 100);
            if (a.HasMember("oct") && a["oct"].IsInt()) arpOct[i] = ClampInt(a["oct"].GetInt(), -2, 2);
            if (a.HasMember("clk") && a["clk"].IsInt()) arpClk[i] = ClampInt(a["clk"].GetInt(), 0, 2);
            if (a.HasMember("div") && a["div"].IsInt()) arpDiv[i] = ClampInt(a["div"].GetInt(), 0, SEQ::kDivCount - 1);
            if (a.HasMember("pitchSlot") && a["pitchSlot"].IsInt()) arpPitchSlot[i] = ClampCVSlot(a["pitchSlot"].GetInt());
            if (a.HasMember("gateSlot") && a["gateSlot"].IsInt()) arpGateSlot[i] = ClampTrigSlot(a["gateSlot"].GetInt());
            if (a.HasMember("accSlot") && a["accSlot"].IsInt()) arpAccSlot[i] = ClampTrigSlot(a["accSlot"].GetInt());
            if (a.HasMember("slideSlot") && a["slideSlot"].IsInt()) arpSlideSlot[i] = ClampTrigSlot(a["slideSlot"].GetInt());
        }
    }
    if (d.HasMember("shared") && d["shared"].IsObject()) {
        const Value &sh = d["shared"];
        int rm = 0, root = -1, op = 0, sc = -1;
        if (sh.HasMember("rootMode") && sh["rootMode"].IsInt()) rm = ClampInt(sh["rootMode"].GetInt(), 0, 2);
        if (sh.HasMember("root") && sh["root"].IsInt()) {
            // MIDI note (48..59) per spec §4.4; pitch classes 0..11 also accepted
            int v = sh["root"].GetInt();
            root = (v >= 48) ? ClampInt(v, 48, 59) - 48 : (v < 24 ? ClampInt(v, 0, 11) : -1);
        }
        if (sh.HasMember("neoOp") && sh["neoOp"].IsInt()) op = ClampInt(sh["neoOp"].GetInt(), 0, 7);
        if (sh.HasMember("scale") && sh["scale"].IsInt()) sc = ClampInt(sh["scale"].GetInt(), 0, SEQ::SCALE_COUNT - 1);
        gArp[0].root_mode = gArp[1].root_mode = (uint8_t)rm;
        if (root >= 0) gArp[0].root = gArp[1].root = (uint8_t)root;
        gNeoOp = (uint8_t)op;
        if (sc >= 0) gArp[0].scale_idx = gArp[1].scale_idx = (uint8_t)sc;
    }

    // derived state: gate masks from (mode, dens, sync), arp scale tables +
    // patterns from (algo, range, p, shared)
    for (int i = 0; i < 2; ++i) SEQ::GateRecompute(gGate[i]);
    for (int i = 0; i < 2; ++i) SEQ::ArpRebuildPattern(gArp[i], gArpExt[i], false, revoiceVarg[i]);
    return true;
}

void SeqEngine::LoadConfig() {
    char buf[1024];
    if (!ModTransport::ReadSectionJSON("seq", buf, sizeof(buf))) return;
    DeserializeConfig(buf);
}

void SeqEngine::SaveConfig() {
    char buf[1024];
    SerializeConfig(buf, sizeof(buf));
    if (buf[0]) ModTransport::WriteSectionJSON("seq", buf);
}

} // namespace CTAG::DRIVERS
