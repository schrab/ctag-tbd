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

} // namespace SEQ
} // namespace CTAG
