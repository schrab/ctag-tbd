// Shared VARG chord vocabulary — the black-metal harmony data used by both
// the ARP engine (CTAG::SEQ, main/) and the TBDings chord engine (TBD-owned
// wrapper). Pure C++11 constexpr, no ESP-IDF dependency: the host unit tests
// compile this file directly.
//
// VARG Chord sends chords the same way as notes: the chord ROOT is a normal
// 0.2 V/oct pitch on the ARP pitch CV. The chord QUALITY (voicing row) is NOT
// a modulation source — it travels as an internal companion in ProcessData
// (SeqEngine::GetVargVoicing -> pd.vargVoicing[]), so it follows VARG's exact
// progression without consuming a user-visible CV slot. No packed encoding.

#pragma once

#include <cstdint>

namespace CTAG {
namespace SEQ {

enum VargQuality {
    VQ_P5 = 0,   // power chord  1-5-1 (octave-fifth-octave)
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
    { 3, { 0, 7, 12 } },      // P5 = 1-5-1 power chord (root, fifth, octave)
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
// 11..16 are the doc's added shapes (5b5, dim7, m7b5, aug, add9, m+b9);
// 17 is the explicit 1-5-1 power chord used by RAW (black-metal tremolo beds).
inline constexpr uint8_t kVargToVoicing[VQ_COUNT] = {
    17, 10, 3, 11, 12, 13, 14, 15, 16
};

// Number of guitar voicings (11 original + 6 doc shapes + 1-5-1 power). The
// wrapper reads the voicing row directly from the internal companion.
inline constexpr int kVargVoicingCount = 18;

// Four-string (fretted) voicing rows, mirroring rings::chords[2] (the
// polyphony-3 variant) from string_synth_part.cc. Shared here so the global
// VARG sequencer can voice a strum from the SAME table the string synth
// uses — host tests pin the two together. One pedal string is added by the
// plugin, separate from these four.
// Three-fretted-string variant (rings::chords[3]) for the compact bank.
inline constexpr int8_t kVargVoicing3[kVargVoicingCount][3] = {
    {  0,  0, 12 },  // 0  OCT
    {  0,  7, 12 },  // 1  5
    {  5,  7, 12 },  // 2  sus4
    {  0,  3, 12 },  // 3  m
    {  0,  3, 10 },  // 4  m7
    {  3, 10, 14 },  // 5  m9
    {  3, 10, 17 },  // 6  m11
    {  2,  9, 16 },  // 7  69
    {  4, 11, 14 },  // 8  M9
    {  4,  7, 11 },  // 9  M7
    {  4,  7, 12 },  // 10 M
    {  0,  6, 12 },  // 11 5b5
    {  0,  3,  6 },  // 12 dim7
    {  0,  3,  6 },  // 13 m7b5
    {  0,  4,  8 },  // 14 aug
    {  0,  3,  7 },  // 15 add9
    {  0,  1,  7 },  // 16 m+b9
    {  0,  7, 12 },  // 17 1-5-1 power
};

inline constexpr int8_t kVargVoicing4[kVargVoicingCount][4] = {
    { -12,  0,  0, 12 },  // 0  OCT
    { -12,  7,  7, 12 },  // 1  5
    { -12,  5,  7, 12 },  // 2  sus4
    { -12,  3, 12, 12 },  // 3  m
    { -12,  3, 10, 10 },  // 4  m7
    { -12,  3, 10, 14 },  // 5  m9
    { -12,  3, 10, 17 },  // 6  m11
    { -12,  2,  9, 16 },  // 7  69
    { -12,  4, 11, 14 },  // 8  M9
    { -12,  4,  7, 11 },  // 9  M7
    { -12,  4,  7, 12 },  // 10 M
    { -12,  0,  6, 12 },  // 11 5b5
    { -12,  0,  3,  6 },  // 12 dim7
    { -12,  0,  3,  6 },  // 13 m7b5
    { -12,  0,  4,  8 },  // 14 aug
    { -12,  0,  3,  7 },  // 15 add9
    { -12,  0,  1,  7 },  // 16 m+b9
    { -12,  0,  7, 12 },  // 17 1-5-1 power
};

} // namespace SEQ
} // namespace CTAG
