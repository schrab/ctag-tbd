#include "SeqEngine.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "ModTransport.hpp"
#include "rapidjson/document.h"

namespace CTAG::DRIVERS {

using CTAG::SP::HELPERS::ClampCVSlot;
using CTAG::SP::HELPERS::ClampTrigSlot;


// ---- engine state (static DRAM, ~1.5 KB) ----
// Setters run on the UIMenu/link task, Process on the audio task; shared
// scalars are the same benign-race class as MarblesEngine's (a torn value
// applies one block late, nothing dereferences).
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

// VARG Chord strum burst: when P5 (style) != 0, a chord gate is expanded
// into a sequence of individual note events (one per string) with P6
// (spread) controlling the gap. Each sub-note is a one-block active-low
// pulse on the ARP's gate slot plus a pitch CV write and string companion.
struct StrumBurst {
    bool active {false};
    uint8_t count {0};
    uint8_t idx {0};
    uint8_t countdown {0};
    uint8_t spacing {2};
    bool pulse {false};
    uint8_t kind {0};   // 0 normal, 1 ghost hit, 2 pre-change transient
    bool glide {false}; // glide to the new chord only on a chord advance
    int8_t slot[6] {-1, -1, -1, -1, -1, -1};
    int8_t note[6] {0, 0, 0, 0, 0, 0};
    uint8_t vel[6] {100, 100, 100, 100, 100, 100};
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

static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void SeqEngine::InitDefaults() {
    for (int i = 0; i < 2; ++i) {
        // gates: spec §4.4 — gate1 preset 01 "Classic Alternating", gate2
        // preset 03 "4-on-the-Floor Ducking Pump"
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
                    SEQ::ArpRebuildPattern(s, e, advance);
                } else if (s.algo == SEQ::kArpAlgoHyp1) {
                    // HYP-1: in-place step mutation on loop wrap
                    const uint8_t mutProb = (uint8_t)((uint16_t)s.p[3] * 50 / 100);
                    for (int k = 0; k < 16; ++k)
                        if (SEQ::ArpRand(s, 100) < mutProb) SEQ::ArpRollHyp1Step(s, e, (uint8_t)k);
                } else {
                    SEQ::ArpRebuildPattern(s, e);
                }
            }
            SEQ::ArpStep cur = e.steps[st];
            if (e.dd_fill) SEQ::ArpApplyTurnaround(s, e, st, cur);
            if (cur.flags & 1u) {
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
                s_vargString[i] = (int8_t)(cur.str % 3u);
                s_vargPedal[i] = (cur.flags & 16u) ? 1u : 0u;
                // VARG Chord strum: expand this chord gate into a burst of
                // one-note events, ordered by the shared strum plan. 0 =
                // Block = legacy whole-chord behaviour.
                StrumBurst &burst = s_strumBurst[i];
                burst.active = false;
                if (s.algo == SEQ::kArpAlgoVargChord && s.p[8] == 0) {
                    int style = (s.p[5] * 10) / 101;
                    if (style > 9) style = 9;
                    if (style > 0) {
                        const uint8_t order[3] = {0, 1, 2};
                        const uint32_t spreadMs = (uint32_t)s.p[6] * 45u / 100u;
                        const uint32_t spreadBlocks =
                            (uint32_t)(spreadMs * 0.001f / dt + 0.5f);
                        const tbd::StrumPlan plan = tbd::StrumBuildPlan(
                            order, 3, (uint8_t)style,
                            (cur.flags & 8u) ? -1 : +1, false,
                            (cur.flags & 2u) != 0,
                            (uint16_t)spreadBlocks, burst.rng);
                        burst.count = plan.n;
                        burst.idx = 0;
                        burst.countdown = 0;
                        burst.spacing = (uint8_t)ClampInt(
                            (int)(spreadBlocks / (plan.n > 1 ? plan.n - 1 : 1)),
                            2, 60);
                        // Voice from the SAME 18-row voicing table the string
                        // synth uses (not the raw interval set), so RAW power
                        // chords really are 1-5-1 and Bryan shapes survive.
                        const int voicing =
                            SEQ::kVargToVoicing[e.st.varg.curQual % SEQ::VQ_COUNT];
                        const int8_t *offs = SEQ::kVargVoicing3[voicing];
                        const int rootNote = 48 + s.root + e.st.varg.curOff;
                        // Blast-beat coupling: even 16ths are slightly softer.
                        const bool burst16 =
                            tbd::StrumMaskIs16thGrid(SEQ::VargEffectiveMask(s));
                        const float atten = (burst16 && (st & 1u)) ? 0.8f : 1.0f;
                        // The open pedal is part of the VARG chord: a fixed
                        // drone one octave below the ARP root, on string 3.
                        // It stays put while the fretted shape moves above.
                        burst.slot[0] = 3;
                        burst.note[0] = (int8_t)(48 + s.root - 12);
                        burst.vel[0] = (uint8_t)ClampInt(
                            (int)(0.85f * atten * 70.0f), 0, 100);
                        for (uint8_t k = 0; k < plan.n && k + 1 < 6; ++k) {
                            const int slot = plan.slot[k] % 3u;
                            burst.slot[k + 1] = (int8_t)((cur.str + k) % 3u);
                            burst.note[k + 1] = (int8_t)(rootNote + offs[slot]);
                            burst.vel[k + 1] = (uint8_t)ClampInt(
                                (int)(plan.vel[k] * atten * 100.0f), 0, 100);
                        }
                        burst.count = plan.n + 1;
                        burst.kind = 0;
                        burst.glide = (s_vargChordChange[i] != 0);
                        s_vargChordChange[i] = 0;
                        burst.active = plan.n > 0;
                        // Suppress the normal long gate window for this step.
                        if (burst.active) s.level = 0;
                    }
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
        if (arpPitchSlot[i] >= 0) {
            if (s_vargPedal[i]) {
                // Open pedal: fixed drone at the ARP root, one octave down.
                cv[arpPitchSlot[i]] =
                    (float)(s.root - 12) / 12.0f * 0.2f
                    + (float)arpOct[i] * 0.2f;
            } else if (s.algo == SEQ::kArpAlgoVargChord && s.p[8] == 0) {
                // Burst mode: VARG Chord sends the chord ROOT as a normal
                // pitch CV; the plugin builds the chord locally. In
                // Arpeggio mode the per-step note CV is used instead.
                // a normal 0.2 V/oct pitch on the ARP's pitch CV (slot 104),
                // consumed by TBDings' `frequency`. The chord QUALITY travels
                // as an internal companion (GetVargVoicing -> pd.vargVoicing),
                // not as a user CV slot. Root = shared root + chord offset.
                const float rootCv =
                    (float)(s.root + e.st.varg.curOff) / 12.0f * 0.2f
                    + (float)arpOct[i] * 0.2f;
                cv[arpPitchSlot[i]] = rootCv;
                // The chord quality is NOT written to a CV slot — it travels
                // as an internal companion in ProcessData (GetVargVoicing),
                // so no user modulation source is consumed.
            } else {
                cv[arpPitchSlot[i]] = pitchCv;
            }
        }
        // Strum burst output: each sub-note gets its own pitch CV, string
        // companion and a ONE-BLOCK low gate pulse (TBStrings edge-detects).
        StrumBurst &burst = s_strumBurst[i];
        bool burstEmit = false;
        if (burst.active) {
            if (burst.countdown == 0) {
                burstEmit = true;
                if (arpPitchSlot[i] >= 0 && burst.idx < burst.count) {
                    cv[arpPitchSlot[i]] =
                        ((float)(burst.note[burst.idx] - 48) / 12.0f
                         + (float)arpOct[i]) * 0.2f;
                }
                if (burst.idx < burst.count) {
                    s_vargString[i] = burst.slot[burst.idx];
                    s_vargBurstVel[i] = burst.vel[burst.idx];
                    s_vargGhost[i] = burst.kind;
                    burst.pulse = true;
                }
                ++burst.idx;
                if (burst.idx >= burst.count) {
                    burst.active = false;
                    burst.pulse = false;
                } else {
                    burst.countdown = burst.spacing;
                }
            } else {
                --burst.countdown;
                burst.pulse = false;
            }
            s_vargBurst[i] = 1;
            s_vargGlide[i] = burst.glide ? 1 : 0;
        } else {
            s_vargBurst[i] = 0;
            s_vargBurstVel[i] = 0;
            s_vargGhost[i] = 0;
            s_vargGlide[i] = 0;
        }

        const float gWin = s.cur_hold ? 1.0f : 0.55f;
        const bool gateOpen = (s.level != 0) && (aPhase[i] < gWin);
        if (arpGateSlot[i] >= 0) {
            if (burst.active || burstEmit)
                trig[arpGateSlot[i]] = (burstEmit || burst.pulse) ? 0 : 1;
            else
                trig[arpGateSlot[i]] = gateOpen ? 0 : 1;
        }
        if (arpAccSlot[i] >= 0) trig[arpAccSlot[i]] = (s.cur_acc && s.level) ? 0 : 1;
        if (arpSlideSlot[i] >= 0) trig[arpSlideSlot[i]] = (s.cur_hold && s.level) ? 0 : 1;
        const float aTarget = gateOpen ? (s.cur_acc ? 1.0f : 0.72f) : 0.0f;
        arpOut[i] += (aTarget - arpOut[i]) * 0.5f;
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

// ---- gate setters (mask recomputed setter-side, rack discipline) ----
int SeqEngine::GetGateMode(int i) { return gGate[i].mode; }
void SeqEngine::SetGateMode(int i, int v) { gGate[i].mode = (uint8_t)ClampInt(v, 0, 18); SEQ::GateRecompute(gGate[i]); }
int SeqEngine::GetGateDens(int i) { return gGate[i].dens; }
void SeqEngine::SetGateDens(int i, int v) { gGate[i].dens = (uint8_t)ClampInt(v, 1, 16); SEQ::GateRecompute(gGate[i]); }
int SeqEngine::GetGateSync(int i) { return gGate[i].sync; }
void SeqEngine::SetGateSync(int i, int v) { gGate[i].sync = (uint8_t)ClampInt(v, 0, 100); SEQ::GateRecompute(gGate[i]); }
float SeqEngine::GetGateLen(int i) { return gGate[i].glen; }
void SeqEngine::SetGateLen(int i, float v) { gGate[i].glen = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
int SeqEngine::GetGateClk(int i) { return gateClk[i]; }
void SeqEngine::SetGateClk(int i, int v) { gateClk[i] = ClampInt(v, 0, 2); }
int SeqEngine::GetGateDiv(int i) { return gateDiv[i]; }
void SeqEngine::SetGateDiv(int i, int v) { gateDiv[i] = ClampInt(v, 0, SEQ::kDivCount - 1); }
int SeqEngine::GetGateTrigSlot(int i) { return gateTrigSlot[i]; }
void SeqEngine::SetGateTrigSlot(int i, int s) { gateTrigSlot[i] = ClampTrigSlot(s); }
int SeqEngine::GetGateCvSlot(int i) { return gateCvSlot[i]; }
void SeqEngine::SetGateCvSlot(int i, int s) { gateCvSlot[i] = ClampCVSlot(s); }
uint32_t SeqEngine::GetGateMask(int i) { return gGate[i].mask; }

uint32_t SeqEngine::GetArpGateMask(int i) {
    uint32_t m = 0;
    for (int st = 0; st < 16; ++st)
        if (gArpExt[i].steps[st].flags & 1u) m |= (1u << (15 - st));
    return m;
}

uint32_t SeqEngine::GetArpAccMask(int i) {
    uint32_t m = 0;
    for (int st = 0; st < 16; ++st)
        if (gArpExt[i].steps[st].flags & 2u) m |= (1u << (15 - st));
    return m;
}

uint32_t SeqEngine::GetArpSlideMask(int i) {
    uint32_t m = 0;
    for (int st = 0; st < 16; ++st)
        if (gArpExt[i].steps[st].flags & 4u) m |= (1u << (15 - st));
    return m;
}

int SeqEngine::GetGateStep(int i) { return gGate[i].step; }

// ---- arp setters ----
int SeqEngine::GetVargVoicing(int i) {
    if (i < 0 || i > 1) return -1;
    if (gArp[i].algo != SEQ::kArpAlgoVargChord) return -1;
    // curQual is the VARG chord QUALITY (0..8); TBDings' performance_state.chord
    // is a Rings VOICING ROW (0..16). Map through the shared table so the
    // exact progression quality reaches the string synth (e.g. minor -> row 3).
    return (int)SEQ::kVargToVoicing[gArpExt[i].st.varg.curQual % SEQ::VQ_COUNT];
}
int8_t SeqEngine::GetVargStroke(int i) {
    if (i < 0 || i > 1) return 0;
    if (gArp[i].algo != SEQ::kArpAlgoVargChord) return 0;
    return s_vargStroke[i];
}
uint8_t SeqEngine::GetVargFlags(int i) {
    if (i < 0 || i > 1) return 0;
    if (gArp[i].algo != SEQ::kArpAlgoVargChord) return 0;
    return s_vargFlags[i];
}
int8_t SeqEngine::GetVargString(int i) {
    if (i < 0 || i > 1) return -1;
    // Both VARG algorithms emit per-note string indices: the note ostinato
    // (algo 5) and the whole-chord engine (algo 6, where it is the base
    // anchor). Other algos publish nothing.
    if (gArp[i].algo != SEQ::kArpAlgoVarg &&
        gArp[i].algo != SEQ::kArpAlgoVargChord) return -1;
    return s_vargString[i];
}
uint8_t SeqEngine::GetVargBurst(int i) {
    if (i < 0 || i > 1) return 0;
    return s_vargBurst[i];
}
uint8_t SeqEngine::GetVargBurstVel(int i) {
    if (i < 0 || i > 1) return 0;
    return s_vargBurstVel[i];
}
uint8_t SeqEngine::GetVargGhost(int i) {
    if (i < 0 || i > 1) return 0;
    return s_vargGhost[i];
}
uint8_t SeqEngine::GetVargGlide(int i) {
    if (i < 0 || i > 1) return 0;
    return s_vargGlide[i];
}
int SeqEngine::GetArpAlgo(int i) { return gArp[i].algo * 3 + (gArp[i].range - 1); }
void SeqEngine::SetArpAlgo(int i, int v) {
    v = ClampInt(v, 0, SEQ::kArpAlgoCount * 3 - 1);   // 7 algos x 3 ranges
    const uint8_t oldAlgo = gArp[i].algo;
    gArp[i].algo = (uint8_t)(v / 3);
    // VARG has no separate 1/2/3-OCT variants: range is always 1 and the
    // Octave row handles transposition. Collapse all three encodings.
    gArp[i].range = (gArp[i].algo == SEQ::kArpAlgoVarg ||
                     gArp[i].algo == SEQ::kArpAlgoVargChord)
                        ? 1u : (uint8_t)(1 + v % 3);
    const bool wasVarg = (oldAlgo == SEQ::kArpAlgoVarg || oldAlgo == SEQ::kArpAlgoVargChord);
    const bool isVarg = (gArp[i].algo == SEQ::kArpAlgoVarg || gArp[i].algo == SEQ::kArpAlgoVargChord);
    if (isVarg && !wasVarg)
        gArpExt[i].st.varg.chordIdx = 0xFF;   // fresh switch: start on the tonic
    SEQ::ArpRebuildPattern(gArp[i], gArpExt[i], false);
}
int SeqEngine::GetArpP(int i, int k) { return (k < 0 || k > 8) ? 0 : gArp[i].p[k]; }
void SeqEngine::SetArpP(int i, int k, int v) {
    if (k < 0 || k > 8) return;
    gArp[i].p[k] = (uint8_t)ClampInt(v, 0, 100);
    // setter-side rebuild: re-voice the CURRENT chord, never step the
    // progression (a knob turn must not jump the chord sequence)
    SEQ::ArpRebuildPattern(gArp[i], gArpExt[i], false);
}
int SeqEngine::GetArpGlide(int i) { return arpGlideMs[i]; }
void SeqEngine::SetArpGlide(int i, int v) { arpGlideMs[i] = ClampInt(v, 0, 100); }
int SeqEngine::GetArpOct(int i) { return arpOct[i]; }
void SeqEngine::SetArpOct(int i, int v) { arpOct[i] = ClampInt(v, -2, 2); }
int SeqEngine::GetArpClk(int i) { return arpClk[i]; }
void SeqEngine::SetArpClk(int i, int v) { arpClk[i] = ClampInt(v, 0, 2); }
int SeqEngine::GetArpDiv(int i) { return arpDiv[i]; }
void SeqEngine::SetArpDiv(int i, int v) { arpDiv[i] = ClampInt(v, 0, SEQ::kDivCount - 1); }
int SeqEngine::GetArpPitchSlot(int i) { return arpPitchSlot[i]; }
void SeqEngine::SetArpPitchSlot(int i, int s) { arpPitchSlot[i] = ClampCVSlot(s); }
int SeqEngine::GetArpGateSlot(int i) { return arpGateSlot[i]; }
void SeqEngine::SetArpGateSlot(int i, int s) { arpGateSlot[i] = ClampTrigSlot(s); }
int SeqEngine::GetArpAccSlot(int i) { return arpAccSlot[i]; }
void SeqEngine::SetArpAccSlot(int i, int s) { arpAccSlot[i] = ClampTrigSlot(s); }
int SeqEngine::GetArpSlideSlot(int i) { return arpSlideSlot[i]; }
void SeqEngine::SetArpSlideSlot(int i, int s) { arpSlideSlot[i] = ClampTrigSlot(s); }

// ---- shared root/scale/neo-op (rack shared-root behavior) ----
int SeqEngine::GetRoot() { return gArp[0].root; }
void SeqEngine::SetRoot(int v) {
    const uint8_t pc = (uint8_t)ClampInt(v, 0, 11);
    gArp[0].root = gArp[1].root = pc;   // base note only; the pattern keeps rolling
}
int SeqEngine::GetRootMode() { return gArp[0].root_mode; }
void SeqEngine::SetRootMode(int v) {
    const uint8_t m = (uint8_t)ClampInt(v, 0, 2);
    gArp[0].root_mode = gArp[1].root_mode = m;
    for (int i = 0; i < 2; ++i) {
        SEQ::ArpRebuildScale(gArp[i], gArpExt[i]);
        SEQ::ArpRebuildPattern(gArp[i], gArpExt[i], false);
    }
}
int SeqEngine::GetNeoOp() { return gNeoOp; }
void SeqEngine::SetNeoOp(int v) { gNeoOp = (uint8_t)ClampInt(v, 0, 7); }
int SeqEngine::GetScale() { return gArp[0].scale_idx & 0x3Fu; }
void SeqEngine::SetScale(int v) {
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
    using namespace rapidjson;
    Document d;
    if (d.Parse(json).HasParseError() || !d.IsObject()) return false;

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
            if (a.HasMember("algo") && a["algo"].IsInt()) gArp[i].algo = (uint8_t)ClampInt(a["algo"].GetInt(), 0, SEQ::kArpAlgoCount - 1);
            if (a.HasMember("range") && a["range"].IsInt()) gArp[i].range = (uint8_t)ClampInt(a["range"].GetInt(), 1, 3);
            // VARG has no range variants; the Octave row is the only
            // transposition control (normalise stored 2/3-OCT configs).
            if (gArp[i].algo == SEQ::kArpAlgoVarg ||
                gArp[i].algo == SEQ::kArpAlgoVargChord) gArp[i].range = 1;
            if (a.HasMember("p") && a["p"].IsArray()) {
                const Value &p = a["p"];
                for (SizeType k = 0; k < p.Size() && k < 9; k++)
                    if (p[k].IsInt()) gArp[i].p[k] = (uint8_t)ClampInt(p[k].GetInt(), 0, 100);
            }
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
    for (int i = 0; i < 2; ++i) SEQ::ArpRebuildPattern(gArp[i], gArpExt[i], false);
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
