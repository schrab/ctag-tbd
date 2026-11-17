/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2024 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

The CTAG TBD hardware design is released under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0).
Details here: https://creativecommons.org/licenses/by-nc-sa/4.0/

CTAG TBD is provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.
***************/

// Pure, host-testable generator core for the VARG modulator (black-metal
// chord/riff sequencer). Same contract as SeqEngineCore.hpp: no ESP-IDF
// types, no hardware, no allocation — VargEngine (the DSP shell) owns the
// clock bus, slot writes and persistence and drives this core per audio
// block.
//
// Harmonic content is derived from the research document
// docs/Black Metal Chords, Scales, Harmonies, and Progressions.md:
//   - chord qualities cover the genre's vocabulary (minor triads, power
//     chords incl. the flattened-fifth tritone variant, dim7/m7b5,
//     augmented, add9, and the open-string b9 friction voicing);
//   - progression modes 1..10 are the documented archetypes (aeolian
//     vamps, the Leviathan Weitzmann major-third cycle, the corpus
//     add9/dim7/#vii ostinato, chromatic mediants, half-step+tritone
//     darkness, the "Freezing Moon" power-chord pool, parallel planing,
//     the augmented oscillation);
//   - progression mode 0 is a generative weighted chord Markov over the
//     corpus-significant triad set (i, #iii, vi, #vii minor triads plus
//     the bVI/bVII major cadences);
//   - the BIAS parameter re-voices every chord between RAW (power chords
//     only), TRIAD (each progression's own quality law) and DISS (altered
//     fifths / dim / aug remapping);
//   - rhythm masks cover the genre's picking textures (straight 1/16 and
//     1/8 tremolo, quarter vamps, sustained chord beds, gallops, the 3-3-2
//     timeline, punctuated stabs) with a Barlow-indispensability
//     generative mode and a TMR per-step mutation mode.
//
// Chord cadence decouples from the riff grid: the shell advances the
// progression on musical bars (96 PPQ) while the riff mask runs on its own
// clock division — "bar-level rhythm decouples from picking speed".

#pragma once

#include <cstdint>
#include <cstring>
#include "SeqEngineCore.hpp"   // SEQ::kDivisions, kBarlow16, kSyncCandidates, Sync255

namespace CTAG {
namespace VARG {

// ===================== Chord qualities =====================
// Each quality carries its dyad "pair" interval above the root: the single
// second tone that defines the chord's character. VRG.Pair carries it as a
// pitch CV (root + pair), so two pitch-mapped voices play the actual dyad:
// root+b3 = the minor identity, root+6 = the tritone chord, root+7 = the
// power chord, root+1 = the open-string b9 clash (Burzum "Dunkelheit"),
// root+2 = the added ninth of the corpus ostinato.
enum ChordQuality : uint8_t {
    Q_P5 = 0,    // power chord  1-5              pair = +7
    Q_MAJ,       // major triad  1-3-5            pair = +4 (bVI/bVII/V cadences)
    Q_MIN,       // minor triad  1-b3-5           pair = +3 (the signature dyad)
    Q_5B5,       // tritone power 1-b5            pair = +6
    Q_DIM7,      // dim7        1-b3-b5-bb7       pair = +6
    Q_M7B5,      // half-dim    1-b3-b5-b7        pair = +6
    Q_AUG,       // augmented   1-3-#5            pair = +8
    Q_ADD9,      // minor add9  1-b3-5-9          pair = +2
    Q_MB9,       // minor + b9 pedal friction     pair = +1
    Q_COUNT
};

inline constexpr uint8_t kQualityPair[Q_COUNT] = { 7, 4, 3, 6, 6, 6, 8, 2, 1 };

// ===================== Progressions =====================
// off = semitones above the tonic pitch class, qual = the archetype's own
// base quality (BIAS re-voices it, see VargApplyBias).
struct VargChord {
    uint8_t off;
    uint8_t qual;
};

struct VargProgression {
    const char *name;
    uint8_t len;
    VargChord chords[6];
};

inline constexpr VargProgression kProgressions[11] = {
    /* 0  */ { "Generative", 0, {} },   // weighted Markov over kGenChordPool
    /* 1  */ { "i-bVII", 2, { { 0, Q_MIN }, { 10, Q_MAJ } } },                        // aeolian vamp (Burzum/Mortiis)
    /* 2  */ { "i-bVI-bVII", 3, { { 0, Q_MIN }, { 8, Q_MAJ }, { 10, Q_MAJ } } },
    /* 3  */ { "i-bIII-bVI-bVII", 4, { { 0, Q_MIN }, { 3, Q_MAJ }, { 8, Q_MAJ }, { 10, Q_MAJ } } },
    /* 4  */ { "Weitzmann", 4, { { 0, Q_MIN }, { 8, Q_MIN }, { 3, Q_MIN }, { 7, Q_MAJ } } },  // Ebm-Bm-Gm-Bb (Leviathan)
    /* 5  */ { "add9-dim7", 4, { { 0, Q_ADD9 }, { 6, Q_DIM7 }, { 11, Q_MIN }, { 0, Q_MAJ } } }, // Am(add9)-D#dim7-G#m-A (corpus)
    /* 6  */ { "Mediant", 2, { { 0, Q_MIN }, { 8, Q_MIN } } },                        // Bm-Gm chromatic mediant (Shining)
    /* 7  */ { "i-bII-v", 3, { { 0, Q_MIN }, { 1, Q_MIN }, { 7, Q_MIN } } },          // Ebm-Em-Bbm half-step darkness
    /* 8  */ { "Pool 5", 6, { { 0, Q_P5 }, { 5, Q_P5 }, { 6, Q_P5 }, { 3, Q_P5 }, { 1, Q_P5 }, { 11, Q_P5 } } }, // E5 A5 A#5 G5 F5 D#5 (Freezing Moon)
    /* 9  */ { "Planing", 4, { { 0, Q_MIN }, { 1, Q_MIN }, { 3, Q_MIN }, { 4, Q_MIN } } }, // Gm-G#m-A#m-Bm parallel planing
    /* 10 */ { "i-I+", 2, { { 0, Q_MIN }, { 0, Q_AUG } } },                           // F#m-F#+ augmented oscillation (Shining)
};
inline constexpr uint8_t kProgCount = 11;

// Generative chord pool (progression 0): the corpus-significant black-metal
// chord set. Boddington Jordan & Herbst found i, iii/#iii (minor), the minor
// mediant vi, and #vii minor triads as the genre's statistical signatures;
// the aeolian cadence pair bVI/bVII and a dim7 passing garnish complete it.
inline constexpr VargChord kGenChordPool[9] = {
    {  0, Q_MIN  },   // i      — the anchor
    {  4, Q_MIN  },   // #iii   — in 25% of corpus tracks (5x any other subgenre)
    {  8, Q_MIN  },   // vi     — minor triad a major third below the tonic
    { 11, Q_MIN  },   // #vii   — raised leading-tone minor
    { 10, Q_MAJ  },   // bVII   — aeolian cadence
    {  8, Q_MAJ  },   // bVI    — aeolian cadence (same root as vi, major law)
    {  3, Q_MAJ  },   // bIII
    {  7, Q_MIN  },   // v      — minor dominant
    {  6, Q_DIM7 },   // #iv    — dim7 passing chord, semitone pull back home
};
inline constexpr uint8_t kGenChordCount = 9;

// First-order transitions over the pool (rows = from, cols = to), tuned to
// the documented motions: aeolian vamps return through bVII/bVI, Weitzmann
// region members chain toward the major-third-relative triads, dim7 leads
// home. Diagonal is zero — a repeated chord is what HOLD is for.
inline constexpr uint8_t kGenChordMarkov[9][9] = {
    /*         to:  i #iii  vi #vii bVII  bVI bIII   v dim7 */
    /* i    */ {   0,  30,  25,  15,  45,  35,  15,  10,   5 },
    /* #iii */ {  35,   0,  20,  10,  15,  10,  10,  40,   5 },
    /* vi   */ {  30,  10,   0,  15,  25,  20,  10,  10,   5 },
    /* #vii */ {  60,  10,  10,   0,  15,  10,   5,  10,   5 },
    /* bVII */ {  80,  10,  15,  10,   0,  20,  10,   5,   5 },
    /* bVI  */ {  25,  10,  10,  10,  45,   0,  10,   5,   5 },
    /* bIII */ {  30,  15,  20,  10,  20,  20,   0,  10,   5 },
    /* v    */ {  55,  10,  10,  10,  15,  10,   5,   0,   5 },
    /* dim7 */ {  45,  15,  10,  10,  10,  10,   5,   5,   0 },
};

// ===================== Rhythm masks =====================
// 16-step riff masks, MSB = step 1 (same bit convention as the SEQ gate).
// The tremolo masks are the genre's picking beds; gallop/3-3-2/stab rows
// cover the riff figures, DIRGE sustains through the glen window.
// 0 = generative (Barlow + Sioros-Guedes), 16 = TMR mutation.
inline constexpr uint16_t kVargRiffMasks[16] = {
    0x0000, // 0 GEN (computed, placeholder)
    0xFFFF, // 1  TRM16  straight 1/16 tremolo
    0xAAAA, // 2  TRM8   straight 1/8 tremolo
    0x8888, // 3  QRT    quarter-note vamp
    0x8000, // 4  HALF   sustained chord bed (glen = 1 holds the bar)
    0xFF00, // 5  BURST  half-bar tremolo burst + rest
    0xBBBB, // 6  GAL    gallop x.xx
    0xDDDD, // 7  RGAL   reverse gallop xx.x
    0x9249, // 8  68L    6/8 lilt / 3-3-2 timeline
    0xC0C2, // 9  STAB   punctuated tritone stabs
    0x9999, // 10 SKIP   dotted-8th skip x..x
    0xF7F7, // 11 CHAR   charge x.xxx (8ths with a 16th pickup)
    0xCC66, // 12 RESP   call & response blocks
    0xFFFE, // 13 TURN   relentless + one-step breath at the turn
    0xC000, // 14 DIRGE  half-bar dirge stab (glen = 1 sustains)
    0xF555, // 15 CHOP   16th run into offbeat chops
};
inline constexpr uint8_t kRiffPresetCount = 16;   // 0 = GEN slot, 1..15 presets
inline constexpr uint8_t kRiffModeTMR = 16;       // mutation mode id

inline const char *kRiffNames[17] = {
    "Generative", "TRM16", "TRM8", "QRT", "HALF", "BURST", "GAL", "RGAL",
    "68L", "STAB", "SKIP", "CHAR", "RESP", "TURN", "DIRGE", "CHOP", "TMR"
};

// One VARG instance. Setters (UIMenu/link task) own the parameter block;
// Process (audio task) advances the clocks, chords and mask.
struct VargSource {
    // --- parameters ---
    uint8_t prog;       // 0..10, kProgressions index (0 = generative)
    uint8_t bias;       // 0..100 chord-quality bias (RAW / TRIAD / DISS bands)
    uint8_t hold;       // 0..2 -> bars per chord = 1<<hold (1/2/4)
    uint8_t rhythm;     // 0 GEN, 1..15 presets, 16 TMR
    uint8_t dens;       // 1..16 (GEN/TMR density, preset Barlow trim)
    uint8_t sync;       // 0..100 (GEN syncopation shift, TMR mutation prob)
    float glen;         // riff gate window, fraction of the division cycle
    uint8_t root;       // tonic pitch class 0..11 (base note = 48 + root)
    // --- runtime ---
    uint32_t mask;      // 16-bit riff mask, MSB = step 1
    uint8_t step;       // 0..15
    uint8_t level;      // current step's mask bit
    uint32_t ticks;     // riff step odometer
    uint32_t barCount;  // musical bars since the last chord change
    uint8_t cycle;      // reserved (chord cadence rides barCount)
    uint8_t chordIdx;   // index into the active progression / pool
    uint8_t curOff;     // current chord root offset above the tonic
    uint8_t curBase;    // current chord's base quality (pre-bias)
    uint8_t curQual;    // current chord's sounding quality (post-bias)
    uint8_t chgPulse;   // chord-onset window (cleared at the next riff step)
    uint8_t chgStep;    // riff step the onset pulse started on
    uint8_t genIdx;     // generative mode: last pool index (markov row)
    uint32_t rng_state; // chord roll + mutation LCG
};

// Deterministic LCG (same family as the rack / SEQ generators).
inline uint32_t VargU32Rand(VargSource &s) {
    s.rng_state = s.rng_state * 1664525u + 1013904223u;
    return s.rng_state;
}

// BIAS re-voicing of one chord. Bands:
//   0..33  RAW   — everything collapses to power chords (2nd-wave baseline)
//   34..66 TRIAD — each progression keeps its own quality law
//   67..100 DISS — altered fifths / dim / aug remapping, probability
//                  scaling with the bias so 67 is occasional, 100 constant
inline uint8_t VargApplyBias(VargSource &s, uint8_t base) {
    if (s.bias <= 33) return Q_P5;
    if (s.bias <= 66) return base;
    uint32_t p = (uint32_t)(s.bias - 66) * 3u;
    if (p > 100u) p = 100u;
    if ((VargU32Rand(s) % 100u) >= p) return base;
    switch (base) {
        case Q_P5:  return (VargU32Rand(s) & 1u) ? Q_5B5 : Q_AUG;
        case Q_MAJ: return Q_AUG;
        case Q_MIN: {   // m7b5 / dim7 / m+b9 (not contiguous — map explicitly)
            const uint8_t r = (uint8_t)(VargU32Rand(s) % 3u);
            return (r == 0) ? Q_M7B5 : (r == 1) ? Q_DIM7 : Q_MB9;
        }
        default:    return base;   // already altered — nothing to push further
    }
}

// Load progression/pool chord idx into the runtime state (bias applied).
inline void VargLoadChord(VargSource &s, uint8_t idx) {
    if (s.prog == 0) {
        idx %= kGenChordCount;
        s.chordIdx = idx;
        s.curOff = kGenChordPool[idx].off;
        s.curBase = kGenChordPool[idx].qual;
    } else {
        const VargProgression &p = kProgressions[s.prog];
        idx %= p.len;
        s.chordIdx = idx;
        s.curOff = p.chords[idx].off;
        s.curBase = p.chords[idx].qual;
    }
    s.curQual = VargApplyBias(s, s.curBase);
}

// Advance to the next chord: progression mode steps its table, generative
// mode walks the weighted markov (never repeating the current chord).
inline void VargNextChord(VargSource &s) {
    if (s.prog == 0) {
        uint32_t sum = 0;
        const uint8_t cur = (s.genIdx < kGenChordCount) ? s.genIdx : 0;
        for (uint8_t j = 0; j < kGenChordCount; ++j) sum += kGenChordMarkov[cur][j];
        uint32_t r = VargU32Rand(s) % (sum ? sum : 1u);
        uint8_t next = cur;
        for (uint8_t j = 0; j < kGenChordCount; ++j) {
            if (r < kGenChordMarkov[cur][j] && kGenChordMarkov[cur][j] > 0) { next = j; break; }
            r -= kGenChordMarkov[cur][j];
        }
        if (next == cur) next = (uint8_t)((cur + 1) % kGenChordCount);
        VargLoadChord(s, next);
        s.genIdx = s.chordIdx;
        return;
    }
    const VargProgression &p = kProgressions[s.prog];
    VargLoadChord(s, (uint8_t)((s.chordIdx + 1) % p.len));
}

// Musical-bar advance: chord cadence. Called by the shell with the number
// of 1/1-note bars landed in this block. Chord changes always land on a
// bar boundary and open the onset pulse window.
inline void VargAdvanceBar(VargSource &s, uint32_t wraps) {
    if (!wraps) return;
    s.barCount += wraps;
    const uint32_t holdBars = 1u << (s.hold & 3u);   // hold clamped 0..2
    if (s.barCount >= holdBars) {
        s.barCount = 0;
        VargNextChord(s);
        s.chgPulse = 1;
        s.chgStep = s.step;
    }
}

// TMR per-step bit mutation (port of the SEQ gate's GateStepMutation):
// weak (low-Barlow) steps flip with probability scaled by sync.
inline void VargStepMutation(VargSource &s) {
    const uint8_t w = (uint8_t)(15 - SEQ::kBarlow16[s.step]);   // inverse indispensability
    const uint32_t prob = ((uint32_t)SEQ::Sync255(s.sync) * (uint32_t)w) / 15u;
    s.rng_state = s.rng_state * 1664525u + 1013904223u;
    const uint8_t r = (uint8_t)((s.rng_state >> 24) & 0xFFu);
    if (r < prob) s.mask ^= (1u << (15 - s.step));
}

// Recompute the riff mask (setter-side, rack discipline — same structure as
// SEQ GateRecompute): generative Barlow threshold + Sioros-Guedes left-shift
// anticipation with a deterministic per-(prog,dens,sync) seed, preset masks
// with the Barlow density trim, TMR seeds the Barlow threshold mask.
inline void VargRiffRecompute(VargSource &s) {
    uint8_t step_on[16];
    const uint8_t sync255 = SEQ::Sync255(s.sync);
    const uint8_t mode = (s.rhythm > kRiffModeTMR) ? kRiffModeTMR : s.rhythm;

    if (mode == kRiffModeTMR) {
        const uint8_t thresh = (s.dens >= 16) ? 0 : (uint8_t)(16 - s.dens);
        for (int i = 0; i < 16; ++i) step_on[i] = (SEQ::kBarlow16[i] >= thresh) ? 1 : 0;
    } else if (mode > 0) {
        const uint16_t preset = kVargRiffMasks[mode];
        for (int i = 0; i < 16; ++i) step_on[i] = (preset >> (15 - i)) & 1u;
        if (s.dens < 16) {
            const uint8_t thresh = (uint8_t)(16 - s.dens);
            for (int i = 0; i < 16; ++i) {
                if (SEQ::kBarlow16[i] < thresh) step_on[i] = 0;
            }
        }
    } else {
        // Generative: Barlow metric indispensability threshold
        const uint8_t thresh = (s.dens >= 16) ? 0 : (uint8_t)(16 - s.dens);
        for (int i = 0; i < 16; ++i) step_on[i] = (SEQ::kBarlow16[i] >= thresh) ? 1 : 0;
    }

    // Sioros-Guedes left-shift anticipation (GEN mode only), deterministic
    // per (prog, dens, sync) so every identical setting reproduces its groove
    if (mode == 0 && sync255 > 0) {
        uint32_t rng = 0x5eedcafeu ^ ((uint32_t)s.dens * 1013904223u) ^
                       ((uint32_t)sync255 * 1664525u) ^
                       ((uint32_t)s.prog * 2654435761u);
        bool prev_was_shifted = false;
        for (int c = 0; c < 8; ++c) {
            const uint8_t i = SEQ::kSyncCandidates[c];
            const uint8_t prev_i = (i + 15) & 0x0Fu;
            if (step_on[i] == 1 && step_on[prev_i] == 0 && !prev_was_shifted) {
                rng = rng * 1664525u + 1013904223u;
                const uint8_t rand_sync = (uint8_t)((rng >> 24) & 0xFFu);
                if (rand_sync < sync255) {
                    step_on[prev_i] = 1;
                    step_on[i] = 0;
                    prev_was_shifted = true;
                } else {
                    prev_was_shifted = false;
                }
            } else {
                prev_was_shifted = false;
            }
        }
    }

    uint16_t mask = 0;
    for (int i = 0; i < 16; ++i) {
        if (step_on[i]) mask |= (1u << (15 - i));
    }
    s.mask = mask;
    s.step %= 16;
    s.level = (uint8_t)((s.mask >> (15 - s.step)) & 1u);
}

// Riff-step advance (shell calls once per block with the landed step wraps):
// step odometer, TMR mutation at its cadence, onset-pulse bookkeeping.
inline void VargAdvanceStep(VargSource &s, uint32_t wraps) {
    if (!wraps) return;
    s.ticks += wraps;
    s.step = (uint8_t)((s.step + wraps) & 0x0Fu);
    if (s.rhythm == kRiffModeTMR && s.sync > 0) VargStepMutation(s);
    s.level = (uint8_t)((s.mask >> (15 - s.step)) & 1u);
    if (s.chgPulse && s.step != s.chgStep) s.chgPulse = 0;
}

// Dyad notes (MIDI). Base 48 + tonic pc, chord offset ≤ 11, pair ≤ 8 — the
// range 48..78 needs no folding.
inline uint8_t VargRootNote(const VargSource &s) {
    return (uint8_t)(48u + s.root + s.curOff);
}
inline uint8_t VargPairNote(const VargSource &s) {
    return (uint8_t)(48u + s.root + s.curOff +
                     (s.curQual < Q_COUNT ? kQualityPair[s.curQual] : 7u));
}

} // namespace VARG
} // namespace CTAG
