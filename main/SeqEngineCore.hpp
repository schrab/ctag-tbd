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

The gate and arp generator cores are a port of the rack-rp2350 T3
modulation engine (mod_engine.h / gen_tables.h), used with permission of
the upstream project.
***************/

// Pure, host-testable generator core for the SEQ modulators (GATE x2 +
// ARP x2). No ESP-IDF types, no hardware, no allocation: SeqEngine (the DSP
// shell) owns the clock bus, slot writes and persistence and drives these
// generators per audio block.
//
// Ported verbatim from ~/mi-rp2350/rack-rp2350/src/mod_engine.h with the
// rename map mod_* -> SEQ::*, s.p.gate.X -> s.X, s.p.arp.X -> s.X; the
// gate's fixed length 16 (rack s.p.gate.len) is a literal, and syncopation
// is stored on the 0..100 UI scale and converted to the rack's 0..255
// probability scale at use (see Sync255 below).

#pragma once

#include <cstdint>
#include <cstring>
#include "helpers/varg_chord.h"
#include "helpers/strum_plan.h"

namespace CTAG {
namespace SEQ {

// Clock divisions: PPQ ticks per division cycle (24 PPQ = quarter note).
// All divisors are integers in 24 PPQ (triplets included).
struct ModDivision {
    const char *name;
    uint16_t ppq;
};
inline constexpr ModDivision kDivisions[13] = {
    {"8/1", 768}, {"4/1", 384}, {"2/1", 192},
    {"1/1",  96}, {"1/2",  48}, {"1/4",  24}, {"1/8", 12}, {"1/16", 6},
    {"1/32",  3}, {"1/4T", 16}, {"1/8T",  8}, {"1/16T", 4}, {"1/32T", 2},
};
inline constexpr uint8_t kDivCount = 13;

// 16 Canonical 16-Step Trance Gate Presets (MSB = Step 1, 1 = 'o', 0 = 'x')
inline constexpr uint16_t kTranceGatePresets[16] = {
    0xAAAA, // 01: o x o x o x o x o x o x o x o x (Classic 1/8th Alternate)
    0x5555, // 02: x o x o x o x o x o x o x o x o (Classic Trance Off-Beat)
    0x7777, // 03: x o o o x o o o x o o o x o o o (4-on-the-Floor Ducking Pump)
    0x3333, // 04: x x o o x x o o x x o o x x o o (Deep / Prog Late Pump)
    0xEEEE, // 05: o o o x o o o x o o o x o o o x (Forward Gallop 3+1)
    0xBBBB, // 06: o x o o o x o o o x o o o x o o (Reverse Gallop Pickup)
    0x5657, // 07: x o x o x o o x x o x o x o o o (Uplifting Rolling Gate)
    0xB5B5, // 08: o x o o x o x o o x o o x o x o (Vanguard Anthem Gate)
    0x9249, // 09: o x x o x x o x x o x x o x x o (Dotted 1/8th Cross-Grid)
    0x9292, // 10: o x x o x x o x o x x o x x o x (Double Tresillo 3-3-2)
    0x9228, // 11: o x x o x x o x x x o x o x x x (Son Clave 3-2 Timeline)
    0x9129, // 12: o x x o x x x o x x o x o x x o (Rumba / Funk Syncopator)
    0xC0C2, // 13: o o x x x x x x o o x x x x o x (Punctuated Chord Stabs)
    0xB6B5, // 14: o x o o x o o x o x o o x o x o (Prog Call & Response)
    0xA55A, // 15: o x o x x o x o x o x o o x o x (Breakbeat / Electro Chop)
    0x88AF  // 16: o x x x o x x x o x o x o o o o (Accelerating Riser Build)
};

// Clarence Barlow's 4/4 16-step metric indispensability table (15 = strongest, 0 = weakest)
inline constexpr uint8_t kBarlow16[16] = {
    15, 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7
};

// Sioros-Guedes syncopation candidate order (strong quarter notes first, then eighths)
inline constexpr uint8_t kSyncCandidates[8] = { 0, 4, 8, 12, 2, 6, 10, 14 };

// One gate generator instance (rack ModSource.p.gate; len is always 16 in
// this port so it is a literal). sync rides the 0..100 UI scale — the rack's
// 0..255 probability scale is derived at use.
struct GateSource {
    uint8_t mode;      // 0 GEN, 1..16 presets, 17 TMR, 18 MRKV
    uint8_t dens;      // 1..16
    uint8_t sync;      // 0..100 UI scale (rack syncopation 0..255)
    float glen;        // gate window, fraction of the division cycle
    uint32_t mask;     // 16-bit pattern, MSB = step 1
    uint8_t step;      // 0..15
    uint8_t level;     // current step's mask bit
    uint32_t rng_state;
    uint8_t cur_preset;   // MRKV: tracked preset index
};

// The rack consumed syncopation as a 0..255 probability byte; the shell
// stores 0..100 so every UI/config roundtrip is exact. Convert at use.
inline uint8_t Sync255(uint8_t sync) {
    uint32_t v = (uint32_t)sync * 255u / 100u;
    return (uint8_t)(v > 255u ? 255u : v);
}

// Normalized Hamming distance between two 16-bit patterns
inline uint8_t mod_hamming_dist(uint16_t a, uint16_t b) {
    return (uint8_t)__builtin_popcount((uint32_t)(a ^ b));
}

// Architecture 4: Hamming-Distance Markov Transition across the 16 Presets
inline uint8_t mod_gate_markov_next(uint8_t current_preset, uint8_t temp, uint32_t &rng) {
    uint8_t d_min = 16;
    uint8_t dists[16];
    for (uint8_t j = 0; j < 16; ++j) {
        if (j == current_preset) {
            dists[j] = 0;
            continue;
        }
        dists[j] = mod_hamming_dist(kTranceGatePresets[current_preset], kTranceGatePresets[j]);
        if (dists[j] < d_min) d_min = dists[j];
    }

    uint16_t weights[16];
    uint16_t total_weight = 0;
    for (uint8_t j = 0; j < 16; ++j) {
        if (j == current_preset) {
            weights[j] = 0;
            continue;
        }
        uint8_t delta = dists[j] - d_min;
        // Scale delta attenuation: at temp=0, strong drop; at temp=255, uniform (shift=0)
        uint32_t shift = ((uint32_t)delta * (255u - (uint32_t)temp) * 3u) / 255u;
        uint16_t w = (shift >= 12) ? 0 : (uint16_t)(512u >> shift);
        if (w == 0 && temp > 128) w = 1;
        weights[j] = w;
        total_weight += w;
    }

    if (total_weight == 0) {
        for (uint8_t j = 0; j < 16; ++j) {
            if (j != current_preset && dists[j] == d_min) return j;
        }
        return (uint8_t)((current_preset + 1) % 16);
    }

    rng = rng * 1664525UL + 1013904223UL;
    uint16_t pick = (uint16_t)(((rng >> 16) * total_weight) >> 16);
    uint16_t running = 0;
    for (uint8_t j = 0; j < 16; ++j) {
        running += weights[j];
        if (pick < running && weights[j] > 0) return j;
    }
    return (uint8_t)((current_preset + 1) % 16);
}

// Recompute the rhythmic trance gate mask (rack mod_gate_recompute). Called
// setter-side on any mode/dens/sync change and once at init.
inline void GateRecompute(GateSource &s) {
    uint8_t step_on[16];
    const uint8_t sync255 = Sync255(s.sync);

    if (s.mode == 17) {
        // Mode 17: TMR (Architecture 4 Shift-Register / Turing Machine Mutation)
        // Seed register based on density threshold
        const uint8_t thresh = (s.dens >= 16) ? 0 : (uint8_t)(16 - s.dens);
        for (int i = 0; i < 16; ++i) {
            step_on[i] = (kBarlow16[i] >= thresh) ? 1 : 0;
        }
        uint16_t mask = 0;
        for (int i = 0; i < 16; ++i) {
            if (step_on[i]) mask |= (1u << (15 - i));
        }
        s.mask = mask;
        s.step %= 16;
        s.level = (s.mask >> (15 - s.step)) & 1u;
        return;
    }

    if (s.mode == 18) {
        // Mode 18: MRKV (Architecture 4 Hamming-Distance Markov Preset Transitions)
        // DENS selects starting seed preset (1..16 -> index 0..15)
        uint8_t p_idx = (s.dens >= 1 && s.dens <= 16)
                            ? (uint8_t)(s.dens - 1) : 0;
        s.cur_preset = p_idx;
        s.mask = kTranceGatePresets[p_idx];
        s.step %= 16;
        s.level = (s.mask >> (15 - s.step)) & 1u;
        return;
    }

    if (s.mode > 0 && s.mode <= 16) {
        // Canonical preset mode (1..16)
        const uint16_t preset = kTranceGatePresets[s.mode - 1];
        for (int i = 0; i < 16; ++i) {
            step_on[i] = (preset >> (15 - i)) & 1u;
        }
        // Density trim if density < 16
        if (s.dens < 16) {
            const uint8_t thresh = (uint8_t)(16 - s.dens);
            for (int i = 0; i < 16; ++i) {
                if (kBarlow16[i] < thresh) step_on[i] = 0;
            }
        }
    } else {
        // Mode 0: Barlow metric indispensability generative engine
        const uint8_t thresh = (s.dens >= 16) ? 0 : (uint8_t)(16 - s.dens);
        for (int i = 0; i < 16; ++i) {
            step_on[i] = (kBarlow16[i] >= thresh) ? 1 : 0;
        }
    }

    // Sioros-Guedes Left-Shift Anticipation with Anti-Offset Protection
    if (sync255 > 0) {
        // Deterministic PRNG seed per (mode, density, syncopation) for 100% reproducible grooves
        uint32_t rng = 0x1337cafeu ^ ((uint32_t)s.dens * 1013904223u) ^
                       ((uint32_t)sync255 * 1664525u) ^
                       ((uint32_t)s.mode * 2654435761u);
        bool prev_was_shifted = false;
        for (int c = 0; c < 8; ++c) {
            const uint8_t i = kSyncCandidates[c];
            const uint8_t prev_i = (i + 15) & 0x0Fu;
            if (step_on[i] == 1 && step_on[prev_i] == 0 && !prev_was_shifted) {
                rng = rng * 1664525u + 1013904223u;
                const uint8_t rand_sync = (uint8_t)((rng >> 24) & 0xFFu);
                if (rand_sync < sync255) {
                    step_on[prev_i] = 1; // Anticipate into weaker pickup step
                    step_on[i] = 0;      // True syncopation (mute the strong beat)
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
    s.level = (s.mask >> (15 - s.step)) & 1u;
}

// Architecture 4: Mode 17 TMR per-step bit mutation (rack
// mod_engine_process_block GATE branch, extracted). Advances rng_state,
// flips one mask bit when the seeded draw hits.
inline void GateStepMutation(GateSource &s) {
    const uint8_t s_idx = s.step;
    const uint8_t w = (uint8_t)(15 - kBarlow16[s_idx]); // inverse Barlow indispensability
    const uint32_t prob = ((uint32_t)Sync255(s.sync) * (uint32_t)w) / 15u;
    s.rng_state = s.rng_state * 1664525UL + 1013904223UL;
    const uint8_t r = (uint8_t)((s.rng_state >> 24) & 0xFFu);
    if (r < prob) {
        s.mask ^= (1u << (15 - s_idx)); // flip bit
    }
}

// Architecture 4: Mode 18 whole-bar Hamming-Markov preset transition (rack
// GATE branch, extracted). Caller guards mode == 18 && step == 0 && sync > 0.
inline void GateBarMutation(GateSource &s) {
    s.rng_state = s.rng_state * 1664525UL + 1013904223UL;
    const uint8_t r = (uint8_t)((s.rng_state >> 24) & 0xFFu);
    if (r < Sync255(s.sync)) {
        s.cur_preset = mod_gate_markov_next(s.cur_preset, Sync255(s.sync), s.rng_state);
        s.mask = kTranceGatePresets[s.cur_preset];
    }
}

// Advance the gate one division-cycle wrap (rack mod_engine_process_block
// MOD_GATE branch): step odometer, TMR/MRKV mutations at their cadences,
// then the level re-read. Returns the wrap count it was told about.
inline uint32_t GateAdvanceStep(GateSource &s, uint32_t wraps) {
    if (wraps) {
        const uint8_t prev_step = s.step;
        s.step = (uint8_t)((s.step + wraps) % 16);

        if (s.mode == 17 && s.sync > 0) GateStepMutation(s);

        if (s.mode == 18 && s.step == 0 && prev_step > 0 && s.sync > 0) GateBarMutation(s);

        s.level = (s.mask >> (15 - s.step)) & 1u;
    }
    return wraps;
}

// ===================== ARP: 5-Algo Generative Melody & Acid Engine ===========
// Verbatim port of rack mod_engine.h MOD_ARP + gen_tables.h. The rack's
// g_mod_engine globals are gone: every function takes the ArpExt (pattern +
// harmony state) and the ArpSource (parameters + runtime) explicitly, and
// the shared root/scale/neo-op sync across the two instances is the shell's
// job (rack mod_arp_set_shared_* / the in-function sibling loops).

// 20 canonical 12-bit pitch-class scale masks (bit s = semitone s above root)
enum scale_id_t {
    SCALE_CHROMATIC = 0,
    SCALE_IONIAN_MAJOR,
    SCALE_DORIAN,
    SCALE_PHRYGIAN,
    SCALE_LYDIAN,
    SCALE_MIXOLYDIAN,
    SCALE_AEOLIAN_MINOR,
    SCALE_LOCRIAN,
    SCALE_PENTATONIC_MAJ,
    SCALE_PENTATONIC_MIN,
    SCALE_BLUES,
    SCALE_HARMONIC_MINOR,
    SCALE_MELODIC_MINOR,
    SCALE_PHRYGIAN_DOM,
    SCALE_WHOLE_TONE,
    SCALE_OCTATONIC_HW,
    SCALE_HIRAJOSHI,
    SCALE_INSEN,
    SCALE_DOUBLE_HARMONIC,
    SCALE_ACID_DOM7_POOL,
    SCALE_COUNT
};

inline constexpr uint16_t SCALE_BITMASKS[SCALE_COUNT] = {
    0x0FFFu, /*  0: Chromatic          [0,1,2,3,4,5,6,7,8,9,10,11] */
    0x0AB5u, /*  1: Ionian (Major)     [0,2,4,5,7,9,11]            */
    0x06ADu, /*  2: Dorian             [0,2,3,5,7,9,10]            */
    0x05ABu, /*  3: Phrygian           [0,1,3,5,7,8,10]            */
    0x0AD5u, /*  4: Lydian             [0,2,4,6,7,9,11]            */
    0x06B5u, /*  5: Mixolydian         [0,2,4,5,7,9,10]            */
    0x05ADu, /*  6: Aeolian (Nat Min)  [0,2,3,5,7,8,10]            */
    0x056Bu, /*  7: Locrian            [0,1,3,5,6,8,10]            */
    0x0295u, /*  8: Major Pentatonic   [0,2,4,7,9]                 */
    0x04A9u, /*  9: Minor Pentatonic   [0,3,5,7,10]                */
    0x04E9u, /* 10: Blues Scale        [0,3,5,6,7,10]              */
    0x09ADu, /* 11: Harmonic Minor     [0,2,3,5,7,8,11]            */
    0x0AADu, /* 12: Melodic Minor      [0,2,3,5,7,9,11]            */
    0x05B3u, /* 13: Phrygian Dominant  [0,1,4,5,7,8,10]            */
    0x0555u, /* 14: Whole Tone         [0,2,4,6,8,10]              */
    0x06DBu, /* 15: Octatonic (H-W)    [0,1,3,4,6,7,9,10]          */
    0x018Du, /* 16: Hirajoshi          [0,2,3,7,8]                 */
    0x04A3u, /* 17: In-Sen             [0,1,5,7,10]                */
    0x09B3u, /* 18: Double Harmonic    [0,1,4,5,7,8,11]            */
    0x0499u  /* 19: Acid Dom7 Pool     [0,3,4,7,10]                */
};

// Metrical Beat-Hierarchy Scale-Degree Gravity Weights (for 7-note scales)
inline constexpr uint8_t DEGREE_METRICAL_GRAVITY[3][7] = {
    /* Deg 0(1st)  Deg 1(2nd)  Deg 2(3rd)  Deg 3(4th)  Deg 4(5th)  Deg 5(6th)  Deg 6(7th) */
    {    255,         15,        180,         20,        210,         15,         40 }, /* Strong */
    {    180,         60,        170,         70,        190,         85,        120 }, /* Medium */
    {     60,        165,         95,        155,         90,        150,        175 }  /* Weak   */
};

// First-Order Scale-Degree Voice-Leading Markov Matrix (7x7)
inline constexpr uint8_t DEGREE_MARKOV_7X7[7][7] = {
    /* To:  d0   d1   d2   d3   d4   d5   d6  */
    /* d0 */ { 30,  70,  65,  35,  60,  20,  40 },
    /* d1 */ { 95,  10,  85,  25,  40,  10,  25 },
    /* d2 */ { 55,  65,  20,  60,  55,  25,  15 },
    /* d3 */ { 35,  30, 110,  10,  65,  20,  15 },
    /* d4 */ { 80,  25,  45,  50,  25,  60,  45 },
    /* d5 */ { 30,  20,  30,  40, 110,  10,  55 },
    /* d6 */ {140,  20,  25,  15,  50,  25,  10 }
};

// HYP-1 Empirical Diatonic Chord Drift Weights (Degrees I..VII)
inline constexpr uint8_t DIATONIC_W7_WEIGHTS[7] = { 40, 20, 10, 30, 30, 30, 4 };

// Functional Diatonic Chord Progression Markov Matrix (I..VII -> I..VII)
inline constexpr uint8_t CHORD_MARKOV_7X7[7][7] = {
    /* From \ To:  I   II  III   IV    V   VI  VII */
    /* I   */ {   0,  35,  20,  65,  65,  55,  10 },
    /* II  */ {  15,   0,  10,  20, 110,  15,  25 },
    /* III */ {  15,  35,   0,  65,  20,  80,  10 },
    /* IV  */ {  55,  45,  10,   0,  85,  25,  20 },
    /* V   */ { 120,  15,  10,  35,   0,  65,  10 },
    /* VI  */ {  25,  65,  25,  70,  45,   0,  10 },
    /* VII */ { 130,  10,  20,  15,  40,  35,   0 }
};

// Neo-Riemannian PLR & NSH Triad Transformations
typedef enum {
    NR_TRANSFORM_P = 0, /* Parallel:          C Maj <-> C Min  */
    NR_TRANSFORM_L,     /* Leittonwechsel:    C Maj <-> E Min  */
    NR_TRANSFORM_R,     /* Relative:          C Maj <-> A Min  */
    NR_TRANSFORM_N,     /* Nebenverwandt:     C Maj <-> F Min  */
    NR_TRANSFORM_S,     /* Slide (shared 3rd):C Maj <-> C# Min */
    NR_TRANSFORM_H,     /* Hexatonic Pole:    C Maj <-> Ab Min */
    NR_TRANSFORM_COUNT
} neo_riemannian_id_t;

// Col 0: Semitone delta when current triad is Major (0 -> 1)
// Col 1: Semitone delta when current triad is Minor (1 -> 0)
inline constexpr uint8_t NEO_RIEMANNIAN_DELTAS[NR_TRANSFORM_COUNT][2] = {
    { 0,  0 }, /* P: Maj+0 -> Min,  Min+0  -> Maj */
    { 4,  8 }, /* L: Maj+4 -> Min,  Min+8  -> Maj */
    { 9,  3 }, /* R: Maj+9 -> Min,  Min+3  -> Maj */
    { 5,  7 }, /* N: Maj+5 -> Min,  Min+7  -> Maj */
    { 1, 11 }, /* S: Maj+1 -> Min,  Min+11 -> Maj */
    { 8,  4 }  /* H: Maj+8 -> Min,  Min+4  -> Maj */
};

inline void apply_neo_riemannian(uint8_t *root_pc, uint8_t *is_minor, neo_riemannian_id_t op) {
    uint8_t q = (*is_minor) ? 1u : 0u;
    uint8_t delta = NEO_RIEMANNIAN_DELTAS[op % NR_TRANSFORM_COUNT][q];
    *root_pc = (uint8_t)((*root_pc + delta) % 12u);
    *is_minor = (uint8_t)(1u - q);
}

// 16 canonical 1-bar melodic contour & motif archetypes (int8_t [16][16]),
// signed scale-degree offsets relative to the active chord root
inline constexpr int8_t MELODIC_CONTOURS_DEG[16][16] = {
    /*  0: Arch (Smooth diatonic swell peaking at octave on step 7) */
    {  0,  1,  2,  3,  4,  5,  6,  7,  6,  5,  4,  3,  2,  1,  0,  0 },
    /*  1: Inverted Arch (Valley descent into lower register, rising back home) */
    {  0, -1, -2, -3, -4, -5, -6, -7, -6, -5, -4, -3, -2, -1,  0,  0 },
    /*  2: Pendulum (Converging oscillation between low root and high octave) */
    {  0,  7,  1,  6,  2,  5,  3,  4,  3,  5,  2,  6,  1,  7, -1,  0 },
    /*  3: Pedal-Point Ostinato (Grounded root on even steps, rising melody on odd steps) */
    {  0,  2,  0,  3,  0,  4,  0,  5,  0,  6,  0,  7,  0,  4,  0, -1 },
    /*  4: Acid Octave-Bounce (Syncopated octave pops and b7/3rd inflections) */
    {  0,  0,  7,  0,  0,  7,  0,  2,  0,  0,  7,  0,  7,  0,  4, -1 },
    /*  5: Rising Cadence (Two-octave diatonic climb from -7 to +7) */
    { -7, -6, -5, -4, -3, -2, -1,  0,  1,  2,  3,  4,  5,  6,  7,  7 },
    /*  6: Falling Resolution (Cascading descent from high octave to low root) */
    {  7,  6,  5,  4,  3,  2,  1,  0, -1, -2, -3, -4, -5, -6, -7, -7 },
    /*  7: Double Wave Hump (Two symmetrical phrase peaks on beats 1.4 and 3.4) */
    {  0,  2,  4,  7,  4,  2,  1,  0,  1,  3,  5,  7,  5,  3,  1,  0 },
    /*  8: High Plateau (Rapid arpeggiated ascent to sustained octave, late drop) */
    {  0,  4,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  4,  2,  0 },
    /*  9: Terraced Sequence Ascending (Four 4-step rising diatonic sequences) */
    {  0,  1,  2,  4,  1,  2,  3,  5,  2,  3,  4,  6,  3,  4,  5,  7 },
    /* 10: Terraced Sequence Descending (Four 4-step tumbling diatonic sequences) */
    {  7,  5,  4,  2,  6,  4,  3,  1,  5,  3,  2,  0,  4,  2,  1,  0 },
    /* 11: Triad + 7th Arpeggio Wave (Up-down 1-3-5-7-8 voice-led arpeggiation) */
    {  0,  2,  4,  6,  7,  6,  4,  2,  0,  2,  4,  6,  7,  6,  4,  1 },
    /* 12: Cresting Tension Wave (Slow chromatic/diatonic creep to step 12, fast drop) */
    {  0,  0,  1,  1,  2,  2,  3,  4,  5,  6,  7,  8,  9,  4,  2, -1 },
    /* 13: Grounded Techno Sub-Bass (Root anchor with syncopated 7th/octave stabs) */
    {  0,  0,  0,  6,  0,  0,  7,  0,  0,  0,  0,  6,  0,  7,  0,  4 },
    /* 14: Turnaround Fill Cascade (Root-fifth alternation followed by steps 10..15 run) */
    {  0,  4,  0,  4,  0,  4,  0,  4,  0,  4,  7,  6,  5,  4,  2,  1 },
    /* 15: Syncopated Call & Response (Question leap on beat 1-2, step answer on 3-4) */
    {  0,  4,  7,  0,  0,  0,  5,  4,  2,  0,  0,  0,  4,  3,  1,  0 }
};

// Metrical Mutation Sensitivity (0..255) for Algo 4 (Deja-Vu)
inline constexpr uint8_t METRIC_MUTATE_SENSITIVITY[16] = {
     35, 240, 140, 255,  85, 230, 150, 255,
     60, 240, 140, 255,  95, 235, 160, 250
};

// Fast integer scale & degree conversion utilities — negative-safe Euclidean
// floor division and modulo (C99 '%' truncates toward 0!)
inline int16_t floor_div_i16(int16_t a, int16_t b) {
    int16_t q = a / b;
    int16_t r = a % b;
    if ((r != 0) && ((r < 0) != (b < 0))) {
        q--;
    }
    return q;
}

inline int16_t floor_mod_i16(int16_t a, int16_t b) {
    int16_t r = a % b;
    if ((r != 0) && ((r < 0) != (b < 0))) {
        r += b;
    }
    return r;
}

// Expands a 12-bit scale mask into ascending semitone offsets (0..11), returns note count M
inline uint8_t expand_scale_mask(uint16_t mask, uint8_t steps_out[12]) {
    uint8_t count = 0;
    mask |= 0x0001u; /* Always force root bit 0 active */
    for (uint8_t s = 0; s < 12u; s++) {
        if ((mask >> s) & 0x01u) {
            steps_out[count++] = s;
        }
    }
    return count;
}

// Converts a chord-relative scale degree offset into a folded MIDI note [28..96]
inline uint8_t degree_to_midi_folded(uint8_t root_pc,
                                     int8_t chord_deg,
                                     int16_t deg_offset,
                                     int8_t oct_offset,
                                     const uint8_t scale_steps[12],
                                     uint8_t num_steps) {
    int16_t m = (num_steps > 0) ? (int16_t)num_steps : 7;
    int16_t total_deg = (int16_t)chord_deg + deg_offset;
    int16_t oct_shift = floor_div_i16(total_deg, m) + (int16_t)oct_offset;
    int16_t scale_idx = floor_mod_i16(total_deg, m);

    int16_t base = (root_pc >= 24) ? (int16_t)root_pc : (int16_t)(36 + (root_pc % 12u));
    int16_t midi_note = base
                      + (int16_t)scale_steps[scale_idx]
                      + (12 * oct_shift);

    while (midi_note > 96) midi_note -= 12;
    while (midi_note < 28) midi_note += 12;
    return (uint8_t)midi_note;
}

// Arp algorithm ids (rack ModArpAlgo + the VARG black-metal algorithm)
enum {
    kArpAlgoHyp1   = 0,   // HYP-1 Acid / 303 W7 Chord-Drift + Mutate + Turnarounds
    kArpAlgoMotif  = 1,   // Phrase-Grammar Motif Transformer (ABAC/AABC)
    kArpAlgoMarkov = 2,   // Tonal-Gravity Markov Walker + 16 Melodic Contours
    kArpAlgoMetro  = 3,   // Metropolix 8-Stage Accumulator & Ratchets
    kArpAlgoDejavu = 4,   // Marbles 3-Zone Deja-Vu Shift Register + Salience
    kArpAlgoVarg   = 5,   // VARG black-metal chord ostinato + tremolo engine
    kArpAlgoVargChord = 6,// VARG chord engine: full chord per gated strum
    kArpAlgoCount  = 7,
};

// Compact 16-step pattern + harmony state (~112 B per instance)
struct ArpStep {
    uint8_t flags;   // bit 0: gate, bit 1: accent, bit 2: slide, bit 3: upstroke
    int8_t  off;     // scale steps above current chord degree
    int8_t  oct;     // octave offset (0..range-1)
    uint8_t str;     // VARG: guitar string index (gated-step ordinal & 3)
};

struct ArpExt {
    ArpStep steps[16];
    uint8_t scale_st[12];
    uint8_t nsteps;
    uint8_t chord_nt;
    int8_t  deg;
    uint8_t is_minor_triad;
    uint8_t bar_count;
    uint8_t dd_fill;
    uint8_t turn_type;

    // Algorithm-specific persistent generator state
    union {
        struct {
            int8_t  seed_deg[4];
            int8_t  seed_oct[4];
            uint8_t seed_flags[4];
            uint8_t form;
        } motif;
        struct {
            uint8_t prev_deg;
            int8_t  prev_oct;
            uint8_t contour_id;
        } markov;
        struct {
            int8_t  accum_val[8];
            int8_t  accum_dir[8];
            uint8_t cur_stage;
            uint8_t cur_pulse;
        } metro;
        struct {
            uint8_t u_pitch[16];
            uint8_t u_gate[16];
        } dejavu;
        struct {
            uint8_t chordIdx;   // index into the active progression / pool
            uint8_t curOff;     // current chord root offset above the tonic
            uint8_t curQual;    // current chord's sounding quality (post-bias)
            uint8_t genIdx;     // generative mode: last pool index
        } varg;
    } st;

    // 303 DSP Articulator state (Q16.16)
    int32_t current_pitch_q16;
    int32_t target_pitch_q16;
    int32_t accent_cap_q16;
};

// One arp instance (rack ModSource.p.arp + the arp runtime fields on
// ModSource). root is a pitch class 0..11; root_mode selects the bar-line
// chord drift (rack root_param: 3 -> mode 2 Neo, 2 -> mode 1 W7). The
// MIDI-note root follow is deferred (spec non-goal).
struct ArpSource {
    uint8_t algo;        // 0..4, kArpAlgo*
    uint8_t range;       // 1..3 octaves
    uint8_t root_mode;   // 0 fixed, 1 W7 drift, 2 Neo-Riemannian
    uint8_t root;        // pitch class 0..11 (base note = 48 + root)
    uint8_t scale_idx;   // 0..19 (+64 flag bit = extended chord tones)
    uint8_t p[4];        // per-algo params, 0..100
    uint8_t step;        // 0..15
    uint8_t cur_note;    // last computed MIDI note
    uint8_t level;       // gate open on the current step
    uint8_t cur_acc;     // accent on the current step
    uint8_t cur_hold;    // slide/hold on the current step
    uint8_t retrig_edge; // 1 on a fresh (non-slid) gate start
    uint8_t held;        // gate held from the previous step
    uint32_t ticks;      // step odometer (bar boundary detection)
    uint32_t rng;        // LCG state (rack mod_u32_rand)
};

// Deterministic LCG rand (rack mod_u32_rand / mod_irand)
inline uint32_t ArpU32Rand(ArpSource &s) {
    s.rng = s.rng * 1664525u + 1013904223u;
    return s.rng;
}

inline uint8_t ArpRand(ArpSource &s, uint8_t n) {
    return n > 0 ? (uint8_t)(((ArpU32Rand(s) >> 16) * n) >> 16) : 0;
}

inline void ArpRebuildScale(ArpSource &s, ArpExt &ext) {
    uint8_t sc_id = (uint8_t)((s.scale_idx & 0x3Fu) % SCALE_COUNT);
    if (s.root_mode == 2) {
        if (sc_id == SCALE_IONIAN_MAJOR || sc_id == SCALE_AEOLIAN_MINOR) {
            sc_id = ext.is_minor_triad ? SCALE_AEOLIAN_MINOR : SCALE_IONIAN_MAJOR;
        } else if (sc_id == SCALE_PENTATONIC_MAJ || sc_id == SCALE_PENTATONIC_MIN) {
            sc_id = ext.is_minor_triad ? SCALE_PENTATONIC_MIN : SCALE_PENTATONIC_MAJ;
        }
    }
    const uint16_t pool = SCALE_BITMASKS[sc_id];
    ext.nsteps = expand_scale_mask(pool, ext.scale_st);
    if (ext.deg >= (int8_t)ext.nsteps) ext.deg = 0;
    ext.chord_nt = (ext.nsteps >= 6) ? 4 : 3;
}

// Bar-line chord progression (Neo-Riemannian, Diatonic Markov, or W7 drift).
// neo_op is the rack's neo_sel: 0 RAND, 1..6 P/L/R/N/S/H, 7 PLR (random P/L/R).
// Mutates only THIS instance — the shell mirrors root/is_minor/deg to the
// sibling and rebuilds its scale (rack did it inline via globals).
inline void ArpNextChord(ArpSource &s, ArpExt &ext, uint8_t neo_op) {
    if (s.root_mode == 2) {
        // Neo-Riemannian Triad drift (semitone shift + major/minor quality flip)
        neo_riemannian_id_t op;
        const uint8_t neo_sel = neo_op & 7u;
        if (neo_sel == 0) {
            op = (neo_riemannian_id_t)ArpRand(s, NR_TRANSFORM_COUNT);
        } else if (neo_sel >= 1 && neo_sel <= 6) {
            op = (neo_riemannian_id_t)(neo_sel - 1);
        } else {
            op = (neo_riemannian_id_t)ArpRand(s, 3);
        }

        uint8_t pc = s.root % 12u;
        apply_neo_riemannian(&pc, &ext.is_minor_triad, op);
        s.root = pc;
        ext.deg = 0;
        return;
    }
    // Diatonic Functional Markov or W7 drift
    uint32_t w[12], sum = 0;
    const uint8_t cur_d = (uint8_t)((ext.deg >= 0 && ext.deg < (int8_t)ext.nsteps) ? ext.deg : 0);
    for (uint8_t i = 0; i < ext.nsteps; ++i) {
        if (i == cur_d) {
            w[i] = 0u;
        } else if (ext.nsteps == 7) {
            w[i] = (s.root_mode == 1) ? DIATONIC_W7_WEIGHTS[i] : CHORD_MARKOV_7X7[cur_d][i];
        } else {
            w[i] = (i == 0) ? 30u : 15u;
        }
        sum += w[i];
    }
    uint32_t r = ArpRand(s, (uint8_t)(sum ? sum : 1));
    uint8_t next_deg = 0;
    for (uint8_t i = 0; i < ext.nsteps; ++i) {
        if (r < w[i] && w[i] > 0) { next_deg = i; break; }
        r -= w[i];
    }
    if (next_deg == cur_d) {
        next_deg = (cur_d == 0 && ext.nsteps > 1) ? 1 : 0;
    }
    ext.deg = (int8_t)next_deg;
}

// Pick chord-tone offset (even scale steps) or passing tone
inline int8_t ArpPickOffset(ArpSource &s, const ArpExt &ext, bool strong_beat) {
    const bool extended_chord = (s.scale_idx >= 64) || ((s.scale_idx & 0x08u) != 0);
    if (strong_beat || ArpRand(s, 100) < 80) {
        uint8_t idx = ArpRand(s, ext.chord_nt);
        int8_t off = (int8_t)(2 * idx);
        if (extended_chord && idx == 2) off = 6; // 9th/Sus flavor skips 5th for 7th/9th
        if (ArpRand(s, 100) < 15) off = (int8_t)(off + ext.nsteps);
        return off;
    }
    return (int8_t)ArpRand(s, ext.nsteps);
}

// Roll a single step for Algo 0 (HYP-1 Acid ac_roll)
// p[0]=DENS (0..100), p[1]=SLIDE (0..100), p[2]=ACCENT (0..100), p[3]=MUTATE (0..100)
inline void ArpRollHyp1Step(ArpSource &s, ArpExt &ext, uint8_t st) {
    ArpStep &step = ext.steps[st];
    const uint8_t dens = s.p[0];
    const uint8_t eff_dens = (st == 0) ? (uint8_t)((dens + 30 > 100) ? 100 : dens + 30) : dens;
    const bool gate = ArpRand(s, 100) < eff_dens;
    const bool acc  = ArpRand(s, 100) < s.p[2];
    const bool sld  = ArpRand(s, 100) < s.p[1];
    step.flags = (uint8_t)((gate ? 1u : 0u) | (acc ? 2u : 0u) | (sld ? 4u : 0u));
    step.off = ArpPickOffset(s, ext, (st & 3u) == 0u);
    const uint8_t rng_oct = s.range ? s.range : 1;
    step.oct = (ArpRand(s, 100) < 55) ? 0 : (int8_t)ArpRand(s, rng_oct);
}

// Algo 1: Phrase-Grammar Motif Transformer (ABAC / AABC / ABBC / AABA)
// p[0]=DENS (0..100), p[1]=FORM (0..3), p[2]=XFORM (0..3), p[3]=MUTATE (0..100)
inline void ArpBuildMotif(ArpSource &s, ArpExt &ext) {
    const uint8_t rng_oct = s.range ? s.range : 1;
    const uint8_t form = (uint8_t)((s.p[1] * 4) / 101); // 0=ABAC, 1=AABC, 2=ABBC, 3=AABA
    const uint8_t xform = (uint8_t)((s.p[2] * 4) / 101); // 0=INV, 1=RETRO, 2=TRANS, 3=DISP
    const uint8_t mutate = s.p[3];
    ext.st.motif.form = form;

    // Generate / refresh Seed A (4 steps = 1 beat)
    for (uint8_t i = 0; i < 4; ++i) {
        if (ext.st.motif.seed_flags[i] == 0 || (mutate > 0 && ArpRand(s, 100) < (mutate >> 1))) {
            uint8_t gate_thresh = (i == 0) ? (uint8_t)((s.p[0] + 25 > 100) ? 100 : s.p[0] + 25) : s.p[0];
            bool gate = ArpRand(s, 100) < gate_thresh;
            int8_t deg;
            if (i == 0) {
                deg = (ArpRand(s, 2) == 0) ? 0 : 4; // Tonic or 5th
            } else if (ArpRand(s, 100) < 80) {
                deg = (int8_t)(2 * ArpRand(s, ext.chord_nt)); // Chord tone
            } else {
                deg = (int8_t)ArpRand(s, ext.nsteps);          // Passing tone
            }
            ext.st.motif.seed_deg[i] = deg;
            ext.st.motif.seed_oct[i] = (ArpRand(s, 100) < 80) ? 0 : (int8_t)ArpRand(s, rng_oct);
            bool acc = ArpRand(s, 100) < 30;
            bool sld = ArpRand(s, 100) < 25;
            ext.st.motif.seed_flags[i] = (uint8_t)((gate ? 1u : 0u) | (acc ? 2u : 0u) | (sld ? 4u : 0u));
        }
    }
    // Guarantee step 0 active tonic anchor
    ext.st.motif.seed_flags[0] |= 1u;
    ext.st.motif.seed_deg[0] = 0;

    // Transform B according to xform
    int8_t b_deg[4], b_oct[4];
    uint8_t b_flags[4];
    int8_t pivot = (int8_t)(ext.st.motif.seed_deg[0] + 2);
    for (uint8_t i = 0; i < 4; ++i) {
        if (xform == 0) { // Inversion
            b_deg[i] = (int8_t)((2 * pivot) - ext.st.motif.seed_deg[i]);
            b_oct[i] = ext.st.motif.seed_oct[i];
            b_flags[i] = ext.st.motif.seed_flags[i];
        } else if (xform == 1) { // Retrograde
            b_deg[i] = ext.st.motif.seed_deg[3 - i];
            b_oct[i] = ext.st.motif.seed_oct[3 - i];
            b_flags[i] = ext.st.motif.seed_flags[3 - i];
        } else if (xform == 2) { // Transpose +5th
            b_deg[i] = (int8_t)(ext.st.motif.seed_deg[i] + 4);
            b_oct[i] = ext.st.motif.seed_oct[i];
            b_flags[i] = ext.st.motif.seed_flags[i];
        } else { // Rhythmic displacement (rotate right by 1)
            uint8_t prev_i = (i + 3) & 3u;
            b_deg[i] = ext.st.motif.seed_deg[prev_i];
            b_oct[i] = ext.st.motif.seed_oct[prev_i];
            b_flags[i] = ext.st.motif.seed_flags[prev_i];
        }
    }

    // Transform C: Retrograde with Rhythmic Displacement and Cadential Pickup on step 3 (bar step 15)
    int8_t c_deg[4], c_oct[4];
    uint8_t c_flags[4];
    for (uint8_t i = 0; i < 4; ++i) {
        c_deg[i] = ext.st.motif.seed_deg[3 - i];
        c_oct[i] = ext.st.motif.seed_oct[3 - i];
        c_flags[i] = ext.st.motif.seed_flags[3 - i];
    }
    // Enforce cadential pickup on step 15 (slurs into beat 1)
    c_flags[3] = 1u | 2u | 4u; // Gate, accent, slide
    c_deg[3] = -1;             // Leading tone resolving to tonic
    c_oct[3] = 0;

    // Compile 16 steps into bar
    const int8_t* deg_ptrs[4];
    const int8_t* oct_ptrs[4];
    const uint8_t* flag_ptrs[4];
    if (form == 0) { // ABAC
        deg_ptrs[0] = ext.st.motif.seed_deg; deg_ptrs[1] = b_deg;
        deg_ptrs[2] = ext.st.motif.seed_deg; deg_ptrs[3] = c_deg;
        oct_ptrs[0] = ext.st.motif.seed_oct; oct_ptrs[1] = b_oct;
        oct_ptrs[2] = ext.st.motif.seed_oct; oct_ptrs[3] = c_oct;
        flag_ptrs[0] = ext.st.motif.seed_flags; flag_ptrs[1] = b_flags;
        flag_ptrs[2] = ext.st.motif.seed_flags; flag_ptrs[3] = c_flags;
    } else if (form == 1) { // AABC
        deg_ptrs[0] = ext.st.motif.seed_deg; deg_ptrs[1] = ext.st.motif.seed_deg;
        deg_ptrs[2] = b_deg;                 deg_ptrs[3] = c_deg;
        oct_ptrs[0] = ext.st.motif.seed_oct; oct_ptrs[1] = ext.st.motif.seed_oct;
        oct_ptrs[2] = b_oct;                 oct_ptrs[3] = c_oct;
        flag_ptrs[0] = ext.st.motif.seed_flags; flag_ptrs[1] = ext.st.motif.seed_flags;
        flag_ptrs[2] = b_flags;                 flag_ptrs[3] = c_flags;
    } else if (form == 2) { // ABBC
        deg_ptrs[0] = ext.st.motif.seed_deg; deg_ptrs[1] = b_deg;
        deg_ptrs[2] = b_deg;                 deg_ptrs[3] = c_deg;
        oct_ptrs[0] = ext.st.motif.seed_oct; oct_ptrs[1] = b_oct;
        oct_ptrs[2] = b_oct;                 oct_ptrs[3] = c_oct;
        flag_ptrs[0] = ext.st.motif.seed_flags; flag_ptrs[1] = b_flags;
        flag_ptrs[2] = b_flags;                 flag_ptrs[3] = c_flags;
    } else { // AABA
        deg_ptrs[0] = ext.st.motif.seed_deg; deg_ptrs[1] = ext.st.motif.seed_deg;
        deg_ptrs[2] = b_deg;                 deg_ptrs[3] = ext.st.motif.seed_deg;
        oct_ptrs[0] = ext.st.motif.seed_oct; oct_ptrs[1] = ext.st.motif.seed_oct;
        oct_ptrs[2] = b_oct;                 oct_ptrs[3] = ext.st.motif.seed_oct;
        flag_ptrs[0] = ext.st.motif.seed_flags; flag_ptrs[1] = ext.st.motif.seed_flags;
        flag_ptrs[2] = b_flags;                 flag_ptrs[3] = ext.st.motif.seed_flags;
    }

    for (uint8_t b = 0; b < 4; ++b) {
        for (uint8_t sub = 0; sub < 4; ++sub) {
            uint8_t st = (uint8_t)((b << 2) | sub);
            ext.steps[st].off   = deg_ptrs[b][sub];
            ext.steps[st].oct   = oct_ptrs[b][sub];
            ext.steps[st].flags = flag_ptrs[b][sub];
        }
    }
}

// Algo 2: Tonal-Gravity Markov Walker & 16 Melodic Contours
// p[0]=DENS (0..100), p[1]=CONTOUR (0..15), p[2]=PEDAL (0..3), p[3]=GRAVTY (0..100)
inline void ArpBuildMarkov(ArpSource &s, ArpExt &ext) {
    const uint8_t rng_oct = s.range ? s.range : 1;
    const uint8_t cid = (uint8_t)((s.p[1] * 16) / 101);
    const uint8_t pedal_mode = (uint8_t)((s.p[2] * 4) / 101); // 0=OFF, 1=ROOT, 2=5TH, 3=BOUNCE
    const uint8_t grav_weight = s.p[3]; // 0..100
    ext.st.markov.contour_id = cid;
    uint8_t cur_d = ext.st.markov.prev_deg % 7u;

    for (uint8_t st = 0; st < 16; ++st) {
        uint8_t beat_tier = ((st & 3u) == 0) ? 0 : (((st & 1u) == 0) ? 1 : 2);
        uint16_t eff_dens = s.p[0];
        if (beat_tier == 0) eff_dens += 20;
        else if (beat_tier == 1) eff_dens += 10;
        if (eff_dens > 100) eff_dens = 100;

        bool gate = ArpRand(s, 100) < eff_dens;
        bool is_pedal = false;
        int8_t deg = 0, oct = 0;
        bool acc = false, sld = false;

        if (pedal_mode == 1 && beat_tier == 0) { // ROOT on quarters
            is_pedal = true; deg = 0;
        } else if (pedal_mode == 2 && beat_tier == 0) { // 5TH on quarters
            is_pedal = true; deg = 4;
        } else if (pedal_mode == 3 && (st == 0 || st == 8)) { // BOUNCE on half notes
            is_pedal = true; deg = (st == 0) ? 0 : 4;
        }

        if (is_pedal) {
            acc = (st == 0);
        } else {
            // Evaluate Markov + Metrical Gravity + Contour Proximity
            int8_t target_deg = MELODIC_CONTOURS_DEG[cid][st];
            int8_t target_mod = floor_mod_i16(target_deg, 7);

            uint32_t cdf[7];
            uint32_t total_w = 0;
            for (uint8_t d = 0; d < 7; ++d) {
                uint32_t w_markov = DEGREE_MARKOV_7X7[cur_d][d];
                uint32_t w_metric = DEGREE_METRICAL_GRAVITY[beat_tier][d];
                uint32_t w = (w_markov * (100u - grav_weight) + w_metric * grav_weight) >> 4;
                int8_t dist = (int8_t)(d - target_mod);
                if (dist < 0) dist = (int8_t)(-dist);
                if (dist > 3) dist = (int8_t)(7 - dist);
                w += (uint32_t)(4 - dist) * ((grav_weight * 15u) / 100u);
                if (w == 0) w = 1;
                total_w += w;
                cdf[d] = total_w;
            }
            uint32_t pick = ArpRand(s, (uint8_t)(total_w > 255 ? 255 : total_w));
            for (uint8_t d = 0; d < 7; ++d) {
                if (pick < cdf[d]) { cur_d = d; break; }
            }
            deg = (int8_t)cur_d;
            oct = (target_deg >= 7 && rng_oct > 1) ? 1 : ((target_deg < 0) ? -1 : 0);
            acc = (beat_tier == 2 && ArpRand(s, 100) < 35);
            sld = (beat_tier == 2 && ArpRand(s, 100) < 30);
        }

        ext.steps[st].off   = deg;
        ext.steps[st].oct   = oct;
        ext.steps[st].flags = (uint8_t)((gate ? 1u : 0u) | (acc ? 2u : 0u) | (sld ? 4u : 0u));
    }
    ext.st.markov.prev_deg = cur_d;
}

// Algo 3: Metropolix Multi-Stage Accumulator
// p[0]=PROB (0..100), p[1]=PULSES (1..8), p[2]=RATCHET (0..3), p[3]=ACCUM (0..3)
inline void ArpBuildMetro(ArpSource &s, ArpExt &ext) {
    const uint8_t rng_oct = s.range ? s.range : 1;
    static const int8_t kInitDegs[8]   = { 0, 2, 4, 6, 4, 2, 1, 4 };
    const uint8_t max_pulses = (uint8_t)(1 + (s.p[1] * 7) / 100); // 1..8
    const uint8_t ratchet_mode = (uint8_t)((s.p[2] * 4) / 101); // 0=OFF, 1=2X, 2=3X, 3=4X
    const uint8_t accum_mode = (uint8_t)((s.p[3] * 4) / 101);   // 0=PENDLM, 1=WRAP, 2=RANDOM, 3=HOLD
    static const int8_t kInitAccum[8]  = { 0, 1, 0, 2, 0, -1, 1, 2 };

    uint8_t st = 0;
    for (uint8_t stage = 0; stage < 8; ++stage) {
        uint8_t pulses = (max_pulses <= 2) ? max_pulses : (uint8_t)(1 + (stage % max_pulses));
        int8_t base_d = kInitDegs[stage];
        int8_t acc = ext.st.metro.accum_val[stage];
        bool gate_stage = ArpRand(s, 100) < s.p[0];

        for (uint8_t p = 0; p < pulses && st < 16; ++p, ++st) {
            bool gate = gate_stage && (stage == 0 || p == 0 || ArpRand(s, 100) < 70);
            bool is_hold = (stage == 0 && p < pulses - 1);
            bool is_acc = (stage == 0 || stage == 3 || stage == 6);
            bool is_sld = is_hold || (stage == 2 || stage == 5);
            if (ratchet_mode > 0 && is_acc) is_sld = true; // Ratchet triggers slide/hold legato
            ext.steps[st].off   = (int8_t)(base_d + acc);
            ext.steps[st].oct   = (int8_t)(((stage == 3 || stage == 7) && (rng_oct > 1)) ? 1 : 0);
            ext.steps[st].flags = (uint8_t)((gate ? 1u : 0u) | (is_acc ? 2u : 0u) | (is_sld ? 4u : 0u));
        }

        // Accumulation advance according to accum_mode
        if (accum_mode == 0) { // PENDULUM
            int8_t step_amt = kInitAccum[stage];
            if (ext.st.metro.accum_dir[stage] == 0) ext.st.metro.accum_dir[stage] = 1;
            int16_t next = (int16_t)(ext.st.metro.accum_val[stage] + (int16_t)(step_amt * ext.st.metro.accum_dir[stage]));
            if (next > 7) { next = (int16_t)(7 - (next - 7)); ext.st.metro.accum_dir[stage] = -1; }
            if (next < -7) { next = (int16_t)(-7 + (-7 - next)); ext.st.metro.accum_dir[stage] = 1; }
            ext.st.metro.accum_val[stage] = (int8_t)next;
        } else if (accum_mode == 1) { // WRAP (0..7)
            int8_t step_amt = kInitAccum[stage];
            int16_t next = (int16_t)(ext.st.metro.accum_val[stage] + step_amt);
            if (next > 7) next = 0;
            if (next < 0) next = 7;
            ext.st.metro.accum_val[stage] = (int8_t)next;
        } else if (accum_mode == 2) { // RANDOM step drift
            if (ArpRand(s, 100) < 30) {
                ext.st.metro.accum_val[stage] = (int8_t)(ArpRand(s, 7) - 3);
            }
        } // accum_mode == 3: HOLD (no advance)
    }
}

// Algo 4: Marbles 3-Zone Deja-Vu Shift Register
// p[0]=DENS (0..100), p[1]=DEJAVU (0..100), p[2]=STEPS (0..3), p[3]=SPREAD (0..100)
inline void ArpBuildDejavu(ArpSource &s, ArpExt &ext) {
    const uint8_t rng_oct = s.range ? s.range : 1;
    const uint8_t dj = s.p[1]; // 0..100 -> Zone 1 (0..45), Zone 2 (46..55), Zone 3 (56..100)
    const uint8_t steps_mode = (uint8_t)((s.p[2] * 4) / 101); // 0=ALL, 1=PENT, 2=TRIAD, 3=ROOT
    const uint8_t spread = s.p[3]; // 0..100

    // 1. Zone 1: Metrically biased mutation (weak beats mutate first)
    if (dj < 46) {
        uint16_t mut_drive = (uint16_t)((46u - dj) * 5u); // 0..230
        for (uint8_t i = 0; i < 16; ++i) {
            uint16_t sens = METRIC_MUTATE_SENSITIVITY[i];
            uint16_t prob = (uint16_t)((mut_drive * sens) >> 8);
            if (ArpRand(s, 255) < prob) {
                ext.st.dejavu.u_pitch[i] = (uint8_t)ArpRand(s, 255);
                ext.st.dejavu.u_gate[i]  = (uint8_t)ArpRand(s, 255);
            }
        }
    }

    // 2. Progressive STEPS Salience Filtering:
    for (uint8_t st = 0; st < 16; ++st) {
        uint8_t r_ix = st;
        // Zone 3: Permutation / jump read pointer
        if (dj > 55) {
            uint8_t jump_prob = (uint8_t)((dj - 55u) * 2u);
            if (ArpRand(s, 100) < jump_prob) {
                r_ix = (uint8_t)ArpRand(s, 16);
            }
        }

        uint8_t raw_p = ext.st.dejavu.u_pitch[r_ix];
        uint8_t raw_g = ext.st.dejavu.u_gate[r_ix];
        uint16_t g_thresh = (uint16_t)(255u - (uint16_t)s.p[0] * 255u / 100u);
        if (st == 0 && g_thresh > 40) g_thresh -= 40;

        bool gate = (raw_g >= g_thresh);
        int16_t deg = (int16_t)(((int32_t)raw_p - 128) * 10 / 256);
        int16_t base_d = floor_mod_i16(deg, 7);
        int8_t oct = (int8_t)floor_div_i16(deg, 7);
        if (spread > 0 && ArpRand(s, 100) < spread) {
            oct = (int8_t)ArpRand(s, rng_oct);
        }
        if (oct < 0) oct = 0;
        if (oct >= rng_oct) oct = (int8_t)(rng_oct - 1);

        // STEPS salience filtering
        if (steps_mode == 3) {
            // ROOT lock
            base_d = 0;
        } else if (steps_mode == 2) {
            // Triad only
            static const int8_t kTriadSnap[7] = { 0, 0, 2, 2, 4, 4, 0 };
            base_d = kTriadSnap[base_d];
        } else if (steps_mode == 1) {
            // Pentatonic subset (strip 4th and 7th)
            if (base_d == 3) base_d = 2;
            if (base_d == 6) base_d = 4;
        } // steps_mode == 0: ALL

        bool acc = (raw_g > 200);
        bool sld = ((raw_g & 0x07u) == 0x07u);
        ext.steps[st].off   = (int8_t)base_d;
        ext.steps[st].oct   = oct;
        ext.steps[st].flags = (uint8_t)((gate ? 1u : 0u) | (acc ? 2u : 0u) | (sld ? 4u : 0u));
    }
}

// ===================== VARG: black-metal chord algorithm =====================
// Algorithm 5. Harmonic content from docs/Black Metal Chords, Scales,
// Harmonies, and Progressions.md. Runs inside the ARP modulators (no extra
// slots/page): the ARP's pitch CV carries the arpeggiated chord tones, the
// gate/accent trigs carry the riff rhythm. Per-algo params:
//   p[0] PROG    — 11 progression modes (10 documented archetypes +
//                  generative chord Markov)
//   p[1] RHYTHM  — 16 picking/riff masks (tremolo 1/16, 1/8, gallop, 6/8,
//                  stabs, ...)
//   p[2] QUALITY — bias band: RAW (power chords) / TRIAD / DISS (altered)
//   p[3] MUTATE  — per-bar mask variation probability (0..100)
// One chord per bar (ArpBuildVarg runs at every bar boundary); the chord
// root is semitone-exact above the arp's shared root, the tones are the
// quality's chord tones (step.off carries SEMITONES for this algo — see
// ArpCalcNote).

// VargQuality, kVargTones and the chord-index codec live in the shared
// header (helpers/varg_chord.h), included above — the TBDings wrapper uses
// the same vocabulary to decode the block-chord CV.

struct VargChord {
    uint8_t off;
    uint8_t qual;
};
struct VargProgression {
    const char *name;
    uint8_t len;
    VargChord chords[6];
};
// Tonic-relative offsets + the archetype's own quality law (BIAS re-voices)
inline constexpr VargProgression kVargProgressions[11] = {
    /* 0  */ { "Generative", 0, {} },
    /* 1  */ { "i-bVII", 2, { { 0, VQ_MIN }, { 10, VQ_MAJ } } },
    /* 2  */ { "i-bVI-bVII", 3, { { 0, VQ_MIN }, { 8, VQ_MAJ }, { 10, VQ_MAJ } } },
    /* 3  */ { "i-bIII-bVI-bVII", 4, { { 0, VQ_MIN }, { 3, VQ_MAJ }, { 8, VQ_MAJ }, { 10, VQ_MAJ } } },
    /* 4  */ { "Weitzmann", 4, { { 0, VQ_MIN }, { 8, VQ_MIN }, { 3, VQ_MIN }, { 7, VQ_MAJ } } },
    /* 5  */ { "add9-dim7", 4, { { 0, VQ_ADD9 }, { 6, VQ_DIM7 }, { 11, VQ_MIN }, { 0, VQ_MAJ } } },
    /* 6  */ { "Mediant", 2, { { 0, VQ_MIN }, { 8, VQ_MIN } } },
    /* 7  */ { "i-bII-v", 3, { { 0, VQ_MIN }, { 1, VQ_MIN }, { 7, VQ_MIN } } },
    /* 8  */ { "Pool 5", 6, { { 0, VQ_P5 }, { 5, VQ_P5 }, { 6, VQ_P5 }, { 3, VQ_P5 }, { 1, VQ_P5 }, { 11, VQ_P5 } } },
    /* 9  */ { "Planing", 4, { { 0, VQ_MIN }, { 1, VQ_MIN }, { 3, VQ_MIN }, { 4, VQ_MIN } } },
    /* 10 */ { "i-I+", 2, { { 0, VQ_MIN }, { 0, VQ_AUG } } },
};
inline constexpr uint8_t kVargProgCount = 11;

// Generative chord pool (progression 0): the corpus-significant BM chord set
inline constexpr VargChord kVargGenPool[9] = {
    {  0, VQ_MIN  },   // i
    {  4, VQ_MIN  },   // #iii
    {  8, VQ_MIN  },   // vi (minor M3 below)
    { 11, VQ_MIN  },   // #vii
    { 10, VQ_MAJ  },   // bVII
    {  8, VQ_MAJ  },   // bVI
    {  3, VQ_MAJ  },   // bIII
    {  7, VQ_MIN  },   // v
    {  6, VQ_DIM7 },   // #iv dim7
};
inline constexpr uint8_t kVargGenCount = 9;
inline constexpr uint8_t kVargGenMarkov[9][9] = {
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

// 20 riff masks (MSB = step 1): the genre's picking beds and figures.
// 16-19 are the strum cells from docs/guitar-strumming-voicing-research.md
// §3 (campfire / backbeat push / d-beat / crust); parity direction for the
// strum engine is derived per mask by tbd::StrumStepIsUp.
inline constexpr uint16_t kVargMasks[20] = {
    0xFFFF, // 0  TRM16  straight 1/16 tremolo
    0xAAAA, // 1  TRM8   straight 1/8 tremolo
    0x8888, // 2  QRT    quarter-note vamp
    0x8000, // 3  HALF   one hit per bar (sustained bed)
    0xFF00, // 4  BURST  half-bar tremolo burst + rest
    0xBBBB, // 5  GAL    gallop x.xx
    0xDDDD, // 6  RGAL   reverse gallop xx.x
    0x9249, // 7  68L    6/8 lilt / 3-3-2 timeline
    0xC0C2, // 8  STAB   punctuated stabs
    0x9999, // 9  SKIP   dotted-8th skip x..x
    0xF7F7, // 10 CHAR   charge x.xxx (8ths with a 16th pickup)
    0xCC66, // 11 RESP   call & response blocks
    0xFFFE, // 12 TURN   relentless + one-step breath
    0xC000, // 13 DIRGE  half-bar dirge stab
    0xF555, // 14 CHOP   16th run into offbeat chops
    0xF000, // 15 DBL    four-hit double stop
    0x8A2A, // 16 CAMP   campfire D . D U . U D U (8th grid)
    0x8AA8, // 17 BACK   backbeat push D . D U D U D .
    0x9090, // 18 DBEA   d-beat 1 . . a . . 2 . . a
    0x8A28, // 19 CRST   crust D - D U - D U (pendulum-read)
};

// BIAS re-voicing: 0..33 RAW (power chords), 34..66 TRIAD (own law),
// 67..100 DISS (probabilistic altered remap; intensity scales with bias)
inline uint8_t VargBiasQuality(ArpSource &s, uint8_t base, uint8_t bias) {
    if (bias <= 33) return VQ_P5;
    if (bias <= 66) return base;
    uint32_t p = (uint32_t)(bias - 66) * 3u;
    if (p > 100u) p = 100u;
    if ((ArpU32Rand(s) % 100u) >= p) return base;
    switch (base) {
        case VQ_P5:  return (ArpU32Rand(s) & 1u) ? VQ_5B5 : VQ_AUG;
        case VQ_MAJ: return VQ_AUG;
        case VQ_MIN: {
            const uint8_t r = (uint8_t)(ArpU32Rand(s) % 3u);
            return (r == 0) ? VQ_M7B5 : (r == 1) ? VQ_DIM7 : VQ_MB9;
        }
        default:     return base;   // already altered
    }
}

// Advance to the next chord of the active progression (generative mode walks
// the weighted markov; never repeats the current pool chord). chordIdx 0xFF
// is the "unloaded" marker (set at InitDefaults): the first bar plays the
// progression's tonic instead of advancing past it.
inline void VargNextChord(ArpSource &s, ArpExt &ext) {
    auto &v = ext.st.varg;
    const uint8_t prog = (uint8_t)((s.p[0] * 11) / 101);
    if (prog == 0) {
        uint32_t sum = 0;
        const uint8_t cur = (v.genIdx < kVargGenCount) ? v.genIdx : 0;
        for (uint8_t j = 0; j < kVargGenCount; ++j) sum += kVargGenMarkov[cur][j];
        uint32_t r = ArpU32Rand(s) % (sum ? sum : 1u);
        uint8_t next = cur;
        for (uint8_t j = 0; j < kVargGenCount; ++j) {
            if (r < kVargGenMarkov[cur][j] && kVargGenMarkov[cur][j] > 0) { next = j; break; }
            r -= kVargGenMarkov[cur][j];
        }
        if (next == cur) next = (uint8_t)((cur + 1) % kVargGenCount);
        v.genIdx = next;
        v.chordIdx = next;
        v.curOff = kVargGenPool[next].off;
        v.curQual = VargBiasQuality(s, kVargGenPool[next].qual, s.p[2]);
        return;
    }
    const VargProgression &pr = kVargProgressions[prog];
    if (v.chordIdx == 0xFF) v.chordIdx = 0;                      // first bar: tonic
    else v.chordIdx = (uint8_t)((v.chordIdx + 1) % pr.len);
    v.curOff = pr.chords[v.chordIdx].off;
    v.curQual = VargBiasQuality(s, pr.chords[v.chordIdx].qual, s.p[2]);
}

// Load the chord at idx of the active progression WITHOUT advancing (used
// by setter-side rebuilds: knob turns re-voice the current chord, they must
// not step the progression).
inline void VargLoadChord(ArpSource &s, ArpExt &ext, uint8_t idx) {
    auto &v = ext.st.varg;
    const uint8_t prog = (uint8_t)((s.p[0] * 11) / 101);
    if (prog == 0) {
        idx %= kVargGenCount;
        v.genIdx = idx;
        v.chordIdx = idx;
        v.curOff = kVargGenPool[idx].off;
        v.curQual = VargBiasQuality(s, kVargGenPool[idx].qual, s.p[2]);
        return;
    }
    const VargProgression &pr = kVargProgressions[prog];
    idx %= pr.len;
    v.chordIdx = idx;
    v.curOff = pr.chords[idx].off;
    v.curQual = VargBiasQuality(s, pr.chords[idx].qual, s.p[2]);
}

// Build one bar: advance the chord (bar boundary) or re-voice the current
// one (setter-side, advance=false), then compile the riff mask into gate /
// accent flags with the quality's chord tones spread over the gated steps
// (octave climbs through the ARP's range on repeated tone cycles).
inline void ArpBuildVarg(ArpSource &s, ArpExt &ext, bool advance = true) {
    auto &v = ext.st.varg;
    if (advance) VargNextChord(s, ext);
    else VargLoadChord(s, ext, (v.chordIdx == 0xFF) ? 0 : v.chordIdx);
    const VargToneSet &ts = kVargTones[v.curQual % VQ_COUNT];
    uint16_t mask = kVargMasks[(s.p[1] * 20) / 101];

    // MUTATE: flip the weak (low-Barlow) steps of the mask per bar
    if (s.p[3] > 0) {
        const uint32_t prob = (uint32_t)s.p[3] * 255u / 100u;
        for (uint8_t st = 0; st < 16; ++st) {
            const uint16_t sens = METRIC_MUTATE_SENSITIVITY[st];
            const uint32_t bit_prob = (prob * sens) >> 8;
            if (ArpRand(s, 255) < bit_prob) mask ^= (1u << (15 - st));
        }
    }

    // Strum direction: grid-locked pendulum over the ABSOLUTE step (research
    // §2 rule 4) — rests advance the alternation because direction is a
    // pure function of (mask grid class, st). Bit 3 rides to the TBDings
    // strum engine via the vargStroke companion.
    const bool grid16 = tbd::StrumMaskIs16thGrid(mask);

    uint8_t k = 0;   // gated-step counter drives the chord-tone walk
    for (uint8_t st = 0; st < 16; ++st) {
        const bool gate = ((mask >> (15 - st)) & 1u) != 0;
        const uint8_t tone = (uint8_t)(k % ts.n);
        const int8_t oct = (s.range > 1)
                               ? (int8_t)((k / ts.n) % s.range) : (int8_t)0;
        const bool up = gate && (grid16 ? ((st & 1u) != 0u) : ((st & 2u) != 0u));
        ext.steps[st].off = (int8_t)ts.st[tone];   // SEMITONES (see ArpCalcNote)
        ext.steps[st].oct = oct;
        // Guitar-string anchor for this note: successive gated steps pick
        // successive strings (0,1,2,3,0,...). Stored per step so the gate
        // edge can latch it into the VARG companion for the string bank.
        ext.steps[st].str = (uint8_t)(k & 3u);
        ext.steps[st].flags = (uint8_t)((gate ? 1u : 0u)
                                        | (gate && (st & 3u) == 0u ? 2u : 0u)
                                        | (up ? 8u : 0u));
        if (gate) ++k;
    }
}

// Rebuild all 16 steps for the currently selected algorithm. advanceChord
// only matters for VARG: the bar-boundary call in SeqEngine::Process passes
// true (one chord per bar), every setter passes false (re-voice in place).
inline void ArpRebuildPattern(ArpSource &s, ArpExt &ext, bool advanceChord = true) {
    ArpRebuildScale(s, ext);

    switch (s.algo) {
        case kArpAlgoHyp1:
            for (uint8_t st = 0; st < 16; ++st) ArpRollHyp1Step(s, ext, st);
            break;
        case kArpAlgoMotif:
            ArpBuildMotif(s, ext);
            break;
        case kArpAlgoMarkov:
            ArpBuildMarkov(s, ext);
            break;
        case kArpAlgoMetro:
            ArpBuildMetro(s, ext);
            break;
        case kArpAlgoDejavu:
            ArpBuildDejavu(s, ext);
            break;
        case kArpAlgoVarg:
        case kArpAlgoVargChord:
        default:
            // Same progression/quality/mask/mutation state machine; only the
            // CV consumer differs (single note vs packed block chord).
            ArpBuildVarg(s, ext, advanceChord);
            break;
    }
}

// Intercepts step output on final bar of phrase cycle to inject cadential turnaround fills
inline void ArpApplyTurnaround(ArpSource &s, ArpExt &ext, uint8_t st, ArpStep &io_step) {
    (void)s;
    if (ext.dd_fill == 0) return;
    const uint8_t fill_type = ext.turn_type & 3u;
    if (fill_type == 0 && st >= 8) {
        // Type 0: Ascending Chord-Tone Run (Steps 8..15)
        static const int8_t kRun[8] = { 0, 2, 4, 6, 8, 10, 12, 14 };
        io_step.flags = (uint8_t)(1u | ((st >= 12) ? 2u : 0u) | ((st == 11 || st == 14) ? 4u : 0u));
        io_step.off = kRun[st - 8];
        io_step.oct = 0;
    } else if (fill_type == 1 && st >= 12) {
        // Type 1: Acid Descending Slid Cascade (Steps 12..15)
        static const int8_t kDesc[4] = { 7, 5, 4, 1 };
        io_step.flags = (uint8_t)(1u | 2u | ((st != 15) ? 4u : 0u));
        io_step.off = kDesc[st - 12];
        io_step.oct = 0;
    } else if (fill_type == 2 && st >= 12) {
        // Type 2: Accelerating Octave Ratchet / Pedal Build
        io_step.flags = (uint8_t)(1u | ((st >= 14) ? 2u : 0u));
        io_step.off = ((st & 1u) == 0) ? 0 : 7;
        io_step.oct = 0;
    } else if (fill_type == 3 && st >= 12) {
        // Type 3: Dominant Tension Tie -> Leading-Tone Pickup
        io_step.flags = (uint8_t)(1u | ((st == 12 || st == 15) ? 2u : 0u) | 4u);
        io_step.off = (st < 15) ? 4 : -1;
        io_step.oct = 0;
    }
}

// Compute MIDI note (28..96) for a step. The rack branched on root_param for
// the base note; this port always bases on 48 + root pitch class (drift
// modes mutate s.root itself, W7 leaves it — spec §4.2 fixed base).
// VARG is the exception: its step.off carries SEMITONES above the current
// progression chord root (semitone-exact black-metal harmony), so it does
// not go through the scale-degree folding.
inline uint8_t ArpCalcNote(const ArpSource &s, const ArpExt &ext,
                           const ArpStep &step) {
    if (s.algo == kArpAlgoVarg || s.algo == kArpAlgoVargChord) {
        int16_t midi = (int16_t)(48 + s.root + ext.st.varg.curOff + step.off)
                       + (int16_t)(12 * step.oct);
        while (midi > 96) midi -= 12;
        while (midi < 28) midi += 12;
        return (uint8_t)midi;
    }
    const uint8_t base_root = (uint8_t)(48u + s.root);
    return degree_to_midi_folded(base_root, ext.deg, step.off, step.oct, ext.scale_st, ext.nsteps);
}

} // namespace SEQ
} // namespace CTAG
