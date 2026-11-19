// Strum plan vocabulary — the guitar-strumming rules from
// docs/guitar-strumming-voicing-research.md §2, kept pure and host-testable
// (compiled by tests/host/run_tests.sh through string_synth_part.cc and
// directly by test_strum_plan.cpp). No ESP-IDF dependency.
//
// The plan answers ONE question per strum edge: which chord-row slots fire,
// in which order, how long after the edge, and at what amplitude. The
// StringSynthPart applies it to per-note envelopes; SeqEngineCore.hpp uses
// the grid/parity helpers to stamp stroke direction into ArpStep flags.
//
// Rule 4 (pendulum grid-lock) is computed from the ABSOLUTE 16th-step of the
// riff mask, never from a stroke counter — rests advance the alternation
// because direction is a function of grid position:
//   16th grid (any odd step gated): even step = downstroke, odd = upstroke
//     (1 e & a: numbered beats and &s are D, e and a are U)
//   8th grid (only even steps gated): st%4==0 = D (beats), st%4==2 = U (&)
// Validated against the research's own notation: GAL 0xBBBB -> D . D U,
// RGAL 0xDDDD -> D U D ., TRM16 0xFFFF -> D U D U tremolo.

#pragma once

#include <cstdint>

namespace tbd {

// Strum styles (TBDings `strumStyle` param). BLOCK = legacy group-envelope
// path, per-note engine bypassed — the preset-compat default ("def":0).
enum StrumStyle {
    STRUM_BLOCK = 0,
    STRUM_PENDULUM = 1,   // grid-locked D/U; subset+velocity on upstrokes
    STRUM_DOWN = 2,       // every stroke low->high, full voicing
    STRUM_CONVERGE = 3,   // outer voices alternating inward (research §4)
    STRUM_DIVERGE = 4,    // middle voices expanding outward (research §4)
    STRUM_RANDOM = 5,     // seeded shuffle per stroke (research §4)
    STRUM_COUNT
};

// Velocity ladder (research §2 rule 3: upstrokes drop 10-20 velocity points
// vs downstrokes; accents carry full weight).
inline constexpr float kStrumVelAccent = 1.00f;
inline constexpr float kStrumVelDown = 0.85f;
inline constexpr float kStrumVelUp = 0.70f;
inline constexpr float kStrumVelUpSoft = 0.60f;

// ---- grid classification + pendulum direction (rule 4) ----

// A mask is a 16th grid iff any ODD step is gated. Odd step st <-> bit
// (15 - st) even <-> 0x5555.
inline bool StrumMaskIs16thGrid(uint16_t mask) {
    return (mask & 0x5555u) != 0u;
}

// Pendulum direction of the grid position st (gated or not — rests advance
// the alternation implicitly because this is a pure function of st).
inline bool StrumStepIsUp(uint16_t mask, uint8_t st) {
    return StrumMaskIs16thGrid(mask) ? ((st & 1u) != 0u) : ((st & 2u) != 0u);
}

// ---- per-stroke trigger plan ----

struct StrumPlan {
    uint8_t n;            // notes fired (<= 8)
    uint8_t slot[8];      // chord-row slot per fired note, in trigger order
    uint16_t delay[8];    // samples after the strum edge
    float vel[8];         // per-note amplitude scale
};

// Small stateless PRNG for STRUM_RANDOM (splitmix32; deterministic per seed).
inline uint32_t StrumRand(uint32_t &state) {
    state += 0x9e3779b9u;
    uint32_t z = state;
    z = (z ^ (z >> 16)) * 0x21f0aaadu;
    z = (z ^ (z >> 15)) * 0x735a2d97u;
    return z ^ (z >> 15);
}

// Build the trigger plan for one stroke.
//
//   pitch_order     chord-row slots sorted by pitch ascending (n_slots vals)
//   n_slots         sounding slots (chord_size, 1..8)
//   style           StrumStyle (BLOCK callers never reach here)
//   stroke          +1 down / -1 up / 0 = no companion -> use alt_up
//   alt_up          engine-side alternation state (used when stroke == 0)
//   accent          downbeat (velocity weight)
//   spread_samples  total stagger window (per-note step = spread/(fired-1))
//   rng             PRNG state for STRUM_RANDOM
inline StrumPlan StrumBuildPlan(const uint8_t *pitch_order, uint8_t n_slots,
                                uint8_t style, int8_t stroke, bool alt_up,
                                bool accent, uint16_t spread_samples,
                                uint32_t &rng) {
    StrumPlan p;
    p.n = 0;
    if (n_slots == 0 || n_slots > 8) return p;
    if (pitch_order == nullptr) return p;
    if (style >= STRUM_COUNT) style = STRUM_PENDULUM;

    // Fired-slot sequence in trigger order (order indices first, remapped
    // through pitch_order where the sequence is built positionally).
    uint8_t seq[8];
    uint8_t k = 0;
    uint8_t fired = n_slots;

    const bool is_up = (style == STRUM_PENDULUM)
                           ? ((stroke == 0) ? alt_up : (stroke < 0))
                           : false;

    if (is_up) {
        // Research §2 rule 3: upstrokes catch only the top half of the
        // voicing (bass strings omitted) and reverse the order (high->low).
        fired = (uint8_t)((n_slots + 1) / 2);
        for (uint8_t i = 0; i < fired; ++i)
            seq[k++] = pitch_order[n_slots - 1 - i];
    } else if (style == STRUM_CONVERGE) {
        // Outer voices alternating inward: n=8 -> [0,7,1,6,2,5,3,4].
        int lo = 0, hi = n_slots - 1;
        while (k < 8 && lo <= hi) {
            seq[k++] = (uint8_t)(lo++);
            if (lo <= hi && k < 8) seq[k++] = (uint8_t)(hi--);
        }
        fired = k;
        for (uint8_t i = 0; i < fired; ++i) seq[i] = pitch_order[seq[i]];
    } else if (style == STRUM_DIVERGE) {
        // Middle voices expanding outward: n=8 -> [3,4,2,5,1,6,0,7];
        // odd n keeps a single center first (n=3 -> [1,0,2], n=5 -> [2,1,3,0,4]).
        int lo = (n_slots - 1) / 2, hi = n_slots / 2;
        if (lo == hi) {
            seq[k++] = (uint8_t)lo;
            --lo;
            ++hi;
        } else {
            seq[k++] = (uint8_t)lo;
            seq[k++] = (uint8_t)hi;
            --lo;
            ++hi;
        }
        while (k < 8 && (lo >= 0 || hi < n_slots)) {
            if (lo >= 0 && k < 8) seq[k++] = (uint8_t)(lo--);
            if (hi < (int)n_slots && k < 8) seq[k++] = (uint8_t)(hi++);
        }
        fired = k;
        for (uint8_t i = 0; i < fired; ++i) seq[i] = pitch_order[seq[i]];
    } else if (style == STRUM_RANDOM) {
        // Seeded Fisher-Yates over the ascending pitch order.
        uint8_t idx[8];
        for (uint8_t i = 0; i < n_slots; ++i) idx[i] = i;
        for (uint8_t i = n_slots; i > 1; --i) {
            const uint8_t j = (uint8_t)(StrumRand(rng) % i);
            const uint8_t t = idx[i - 1];
            idx[i - 1] = idx[j];
            idx[j] = t;
        }
        for (uint8_t i = 0; i < n_slots; ++i) seq[k++] = pitch_order[idx[i]];
        fired = k;
    } else {  // STRUM_DOWN and STRUM_PENDULUM downstroke: low->high, full
        for (uint8_t i = 0; i < n_slots; ++i) seq[k++] = pitch_order[i];
        fired = k;
    }

    // Velocity + delays (denominator keeps a single note at delay 0).
    const float vel = is_up ? (accent ? kStrumVelUp : kStrumVelUpSoft)
                            : (accent ? kStrumVelAccent : kStrumVelDown);
    const uint32_t denom = (fired > 1) ? (uint32_t)(fired - 1) : 1u;
    for (uint8_t i = 0; i < fired; ++i) {
        p.slot[p.n] = seq[i];
        p.delay[p.n] = (uint16_t)((uint32_t)spread_samples * i / denom);
        p.vel[p.n] = vel;
        ++p.n;
    }
    return p;
}

}  // namespace tbd
