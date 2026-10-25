/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2020 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

The CTAG TBD hardware design is released under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0).
Details here: https://creativecommons.org/licenses/by-nc-sa/4.0/

CTAG TBD is provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.

The random-generation core is a port of Mutable Instruments "Marbles"
(Émilie Gillet, MIT license), see components/mutable/eurorack/marbles/.
***************/

#pragma once

#include <cstdint>
#include "helpers/ctagSlotBounds.hpp"

namespace CTAG::DRIVERS {

// Marbles engine: port of the Mutable Instruments Marbles random modulator.
//
// Replaces the former LFO/sequencer/gate modulation engine. Structure:
//   t section: 3 gate outputs — T1/T3 random gates, T2 the master clock.
//              7 models incl. the upstream-hidden "Markov" heuristic model.
//   X section: 3 quantized random CV channels (scale, spread/bias/steps,
//              ASR register mode, deja-vu loop).
//   Y section: 1 random CV derived from X2's clock through a divider.
//
// Runs at audio block rate (~1378 Hz): Process() is called with size==1 at
// the block tick, so the marbles generators are initialized with sr=1378.125
// and all rate constants stay in absolute Hz.
//
// Output mapping (all slots re-assignable, -1 = off):
//   trig 40 = T1, 41 = T2 (clock), 42 = T3
//   cv   98 = X1, 99 = X2, 100 = X3, 101 = Y
// CV values are marbles volts scaled by 0.2: full ±5 V -> ±1.0.
//
// Thread model (same as the old ModEngine): parameter setters run on the
// UIMenu task, Process() on the audio task. All shared state is plain
// statics; a torn float at worst applies one block late.

class MarblesEngine {
public:
    static void Init();
    // Called each audio block (1378 Hz) before the sound processors.
    static void Process(float *cv_buffer, uint8_t *trig_buffer);

    // External clock (MIDI clock, 24 PPQN). Called from the MIDI task.
    static void OnMidiClock();
    // MIDI Start/Continue (0xFA/0xFB): reset pattern phase to the downbeat.
    static void OnMidiStart();
    // UI action: one-shot reset of t + X/Y sequences.
    static void RequestReset();

    // Live output indication for the UI (lock-free, updated every block):
    // bit 0-2   T1/T2/T3 gate states (1 = high; T2 is the master clock)
    // bit 3-9   X1 level normalized to its octave range, 0..127 (64 = zero)
    // bit 10-16 X2, bit 17-23 X3, bit 24-30 Y (same encoding)
    static uint32_t GetOutputSnapshot();

    // --- t section ---
    static int GetTModel();  static void SetTModel(int v);    // 0..6, 6 = Markov
    static int GetTRange();  static void SetTRange(int v);    // 0=0.25x 1=1x 2=4x
    static float GetTRate(); static void SetTRate(float v);   // semitones -60..+60
    static float GetTBias(); static void SetTBias(float v);   // 0..1
    static float GetTJitter(); static void SetTJitter(float v); // 0..1
    static int GetTPwMean(); static void SetTPwMean(int v);   // 0..255
    static int GetTPwStd();  static void SetTPwStd(int v);    // 0..255
    static int GetTDejaVu(); static void SetTDejaVu(int v);   // 0=off 1=on 2=locked
    static bool GetTExternal(); static void SetTExternal(bool v); // clock = MIDI

    // --- clocking of the X section ---
    // 0 = T1/T2/T3 (default), 1 = T1, 2 = T2 (master), 3 = T3, 4 = MIDI clock
    static int GetXYClockSource(); static void SetXYClockSource(int v);

    // --- X section ---
    static int GetXScale();  static void SetXScale(int v);    // 0..5 preset scales
    static int GetXRange();  static void SetXRange(int v);    // 0=2oct 1=5oct 2=10oct
    static int GetXControl(); static void SetXControl(int v); // 0=ident 1=bump 2=tilt
    static bool GetXRegister(); static void SetXRegister(bool v);
    static float GetXRegisterValue(); static void SetXRegisterValue(float v); // 0..1
    static float GetXSpread(); static void SetXSpread(float v);
    static float GetXBias(); static void SetXBias(float v);
    static float GetXSteps(); static void SetXSteps(float v);
    static int GetXDejaVu(); static void SetXDejaVu(int v);

    // --- Y section ---
    static int GetYRange();  static void SetYRange(int v);    // 0=2oct 1=5oct 2=10oct
    static float GetYSpread(); static void SetYSpread(float v);
    static float GetYBias(); static void SetYBias(float v);
    static float GetYSteps(); static void SetYSteps(float v);
    static int GetYDivider(); static void SetYDivider(int v); // 0=1/64 .. 11=1/1
    // 0=off (upstream hardcodes this), 1=follow DV amount/length, 2=locked
    static int GetYDejaVu(); static void SetYDejaVu(int v);

    // --- shared deja-vu ---
    static float GetDejaVuAmount(); static void SetDejaVuAmount(float v); // 0..1
    static int GetDejaVuLength(); static void SetDejaVuLength(int v);     // 1..16

    // --- output slot assignment ---
    static int GetTSlot(int i);  static void SetTSlot(int i, int slot);   // i 0..2, trig
    static int GetXSlot(int i);  static void SetXSlot(int i, int slot);   // i 0..2, cv
    static int GetYSlot();       static void SetYSlot(int slot);          // cv

    // --- dynamic CC slots (unchanged from ModEngine) ---
    static void StartLearn();
    static void StopLearn();
    static bool IsLearning();
    // CV slot (90..97) of the most recent MIDI-learn capture, -1 if none.
    static int  GetLastLearnedSlot();
    static int  GetDynamicSlotCC(int slot_idx);
    static void SetDynamicSlotCC(int slot_idx, int cc);
    static void SetDynamicSlotChan(int slot_idx, int chan);
    static int  GetDynamicSlotChan(int slot_idx);

    // Persistence: /spiffs/data/mod-config.jsn
    static void SaveConfig();
    static void LoadConfig();
};

// Free functions called from Midi::controlChange()
void MarblesEngine_MaybeCaptureCC(uint8_t channel, uint8_t cc_num, uint8_t value);
void MarblesEngine_ApplyDynamicCC(uint8_t channel, uint8_t cc_num, uint8_t value, float *cv_buffer);

}
