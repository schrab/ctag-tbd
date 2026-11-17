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

#pragma once

#include <cstdint>
#include "helpers/ctagSlotBounds.hpp"

namespace CTAG::DRIVERS {

// VARG modulator: the black-metal chord/riff sequencer (harmonic content
// from docs/Black Metal Chords, Scales, Harmonies, and Progressions.md).
// Runs alongside Marbles + SeqEngine; same contract shape: scalar
// getters/setters (UIMenu/link task), Process on the audio task. Trig
// outputs are ACTIVE-LOW (0 = active, 1 = inactive, idle 1) and both owned
// slots are written every block; CV/trig writes funnel through
// ClampCVSlot/ClampTrigSlot (audio-task stack safety).
//
// Outputs (defaults):
//   cv 106  = VRG.Root   chord root pitch, 1 V/oct, MIDI 48-anchored
//   cv 107  = VRG.Pair   root + dyad pair interval (quality-defining tone)
//   trig 51 = VRG.Trem   riff gate window (mask bit && phase < len)
//   trig 52 = VRG.Chg    chord-onset pulse (one riff division per chord)
//
// Two clocks from ONE source (spec: bar-level harmony decouples from the
// picking grid): the riff mask advances on the per-source clock division;
// the chord progression advances on musical bars (96 PPQ) — HOLD bars per
// chord (1/2/4). Both clocks freeze while a MIDI-clocked source is stopped
// (the riff trig releases, the chord CVs hold the last chord).
class VargEngine {
public:
    static void Init();   // InitDefaults + LoadConfig
    static void InitDefaults();
    // Audio task, once per block, AFTER SeqEngine::Process. dt in seconds.
    static void Process(float *cv, uint8_t *trig, float dt);
    // Live output word for the panel strip:
    //   bit 0 trem level, bit 1 chord-onset pulse, bits 2-5 riff step,
    //   bits 6-9 chord offset, bits 10-13 quality, bits 14-17 tonic pc,
    //   bits 18-21 pair interval, bits 22-25 chord index
    static uint32_t GetOutputSnapshot();

    static int GetProg();    static void SetProg(int v);     // 0..10
    static int GetBias();    static void SetBias(int v);     // 0..100
    static int GetHold();    static void SetHold(int v);     // 0..2 (bars 1/2/4)
    static int GetRhythm();  static void SetRhythm(int v);   // 0..16
    static int GetDens();    static void SetDens(int v);     // 1..16
    static int GetSync();    static void SetSync(int v);     // 0..100 %
    static float GetLen();   static void SetLen(float v);    // 0..1
    static int GetRoot();    static void SetRoot(int v);     // tonic pc 0..11
    static int GetClk();     static void SetClk(int v);      // 0 global 1 int 2 midi
    static int GetDiv();     static void SetDiv(int v);      // SEQ::kDivisions idx
    static int GetRootSlot();  static void SetRootSlot(int s);   // cv
    static int GetPairSlot();  static void SetPairSlot(int s);   // cv
    static int GetTremSlot();  static void SetTremSlot(int s);   // trig
    static int GetChgSlot();   static void SetChgSlot(int s);    // trig
    static uint32_t GetMask();   // riff pattern preview for the panel

    // --- persistence ("varg" section of mod-config.jsn) ---
    static void LoadConfig();
    static void SaveConfig();
    // Flat JSON over the whole engine state; type-checked + clamped on load.
    static void SerializeConfig(char *dst, int cap);
    static bool DeserializeConfig(const char *json);
};

} // namespace CTAG::DRIVERS
