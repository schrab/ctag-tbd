// Shared VARG chord vocabulary — the black-metal harmony data used by both
// the ARP engine (CTAG::SEQ, main/) and the TBDings chord engine (TBD-owned
// wrapper). Pure C++11 constexpr, no ESP-IDF dependency: the host unit tests
// compile this file directly.
//
// The block-chord algorithm packs (root offset, quality) into one ARP pitch
// CV slot: index = off * 9 + qual, cv = index / 108.0f. Decode is the exact
// inverse and must stay cheap (it runs in the audio block).

#pragma once

#include <cstdint>

namespace CTAG {
namespace SEQ {

enum VargQuality {
    VQ_P5 = 0,   // power chord  1-5
    VQ_MAJ,      // major triad  1-3-5
    VQ_MIN,      // minor triad  1-b3-5
    VQ_5B5,      // tritone power 1-b5
    VQ_DIM7,     // dim7         1-b3-b5-bb7
    VQ_M7B5,     // half-dim     1-b3-b5-b7
    VQ_AUG,      // augmented    1-3-#5
    VQ_ADD9,     // minor add9   1-b3-5-9
    VQ_MB9,      // minor + b9 pedal friction
    VQ_COUNT
};

// Chord-tone sets (semitones above the chord root, ascending)
struct VargToneSet { uint8_t n; uint8_t st[5]; };

inline constexpr VargToneSet kVargTones[VQ_COUNT] = {
    { 2, { 0, 7 } },          // P5
    { 3, { 0, 4, 7 } },       // MAJ
    { 3, { 0, 3, 7 } },       // MIN
    { 2, { 0, 6 } },          // 5b5
    { 4, { 0, 3, 6, 9 } },    // dim7
    { 4, { 0, 3, 6, 10 } },   // m7b5
    { 3, { 0, 4, 8 } },       // AUG
    { 4, { 0, 3, 7, 14 } },   // add9
    { 3, { 0, 1, 7 } },       // m+b9
};

// Quality -> rings::chords row. 0..10 are the original 11 guitar voicings;
// 11..16 are the doc's added shapes (5b5, dim7, m7b5, aug, add9, m+b9).
inline constexpr uint8_t kVargToVoicing[VQ_COUNT] = {
    1, 10, 3, 11, 12, 13, 14, 15, 16
};

inline constexpr int kVargChordSlotCount = 540;   // 5 octaves (-2..+2) * 12 roots * 9 qualities
inline constexpr int kVargChordPerOctave = 108;

inline uint16_t EncodeChord(uint8_t off, uint8_t qual, int8_t octave) {
    int8_t oct = octave < -2 ? -2 : (octave > 2 ? 2 : octave);
    return (uint16_t)((oct + 2) * kVargChordPerOctave
                      + off * 9u + (uint16_t)(qual % VQ_COUNT));
}
// Two-arg form stays for callers that do not carry an octave (default 0).
inline uint16_t EncodeChord(uint8_t off, uint8_t qual) {
    return EncodeChord(off, qual, 0);
}
inline void DecodeChord(uint16_t idx, uint8_t &off, uint8_t &qual, int8_t &octave) {
    if (idx >= (uint16_t)kVargChordSlotCount) idx = kVargChordSlotCount - 1;
    octave = (int8_t)((int)idx / kVargChordPerOctave - 2);
    const uint16_t within = (uint16_t)(idx % kVargChordPerOctave);
    off = (uint8_t)(within / 9u);
    qual = (uint8_t)(within % 9u);
}
inline void DecodeChord(uint16_t idx, uint8_t &off, uint8_t &qual) {
    int8_t oct = 0;
    DecodeChord(idx, off, qual, oct);
}

} // namespace SEQ
} // namespace CTAG
