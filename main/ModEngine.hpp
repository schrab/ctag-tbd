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
***************/

#pragma once

#include <cstdint>

namespace CTAG::DRIVERS {

// Modulation engine: 2 LFOs running at audio block rate
// Writes to CV slots 98 (LFO1) and 99 (LFO2)

class ModEngine {
public:
    static void Init();
    // Called each audio block (1378 Hz) to update LFO values
    static void Process(float *cv_buffer);

    // LFO configuration
    static void SetLFOShape(int lfo, int shape);  // 0=sine,1=tri,2=saw,3=sq,4=S&H
    static void SetLFORate(int lfo, float hz);     // 0.01 - 100 Hz
    static void SetLFOAmplitude(int lfo, float amp); // 0.0 - 1.0

    // Mapping config: which CV slot LFO1/2 writes to
    static int  GetLFOCVSlot(int lfo);
    static void SetLFOCVSlot(int lfo, int slot);

    static int  GetLFOShape(int lfo);
    static float GetLFORate(int lfo);
    static float GetLFOAmplitude(int lfo);

    // MIDI Learn: capture next CC on global channel into a dynamic slot
    static void StartLearn();
    static void StopLearn();
    static bool IsLearning();
    static int  GetLastLearnedSlot(); // returns CV slot index that was learned

    // Dynamic CC slot 0-7 → CV slot 90-97
    static int  GetDynamicSlotCC(int slot_idx); // returns CC number for given slot
    static void SetDynamicSlotCC(int slot_idx, int cc);
    static void SetDynamicSlotChan(int slot_idx, int chan);
    static int  GetDynamicSlotChan(int slot_idx);

private:
    static float lfoPhase[2];
    static float lfoRate[2];
    static float lfoAmplitude[2];
    static int lfoShape[2];
    static int lfoCVSlot[2];
    static float lfoHold[2];  // for S&H

    // Dynamic CC mapping: slot 0-7 maps (chan, cc) → CV slot 90-97
    static int dynCC[8];   // CC number or -1 = unused
    static int dynChan[8]; // MIDI channel (0-15)
    static int dynTargetSlot[8]; // which CV slot this CC writes to (90-97)

    static bool learning;
    static int lastLearnedSlot;
};

// Free functions called from Midi::controlChange()
void ModEngine_MaybeCaptureCC(uint8_t channel, uint8_t cc_num, uint8_t value);
void ModEngine_ApplyDynamicCC(uint8_t channel, uint8_t cc_num, uint8_t value, float *cv_buffer);

}
