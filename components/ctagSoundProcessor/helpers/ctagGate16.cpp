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

#include "ctagGate16.hpp"
#include "ctagSlotBounds.hpp"
#include <cstdlib>

namespace CTAG {
    namespace SP {
        namespace HELPERS {

            ctagGate16::ctagGate16()
                : stepLength(1.0f)
                , direction(Direction::FWD)
                , trigSlot(-1)
                , accentSlot(-1)
                , swingAmount(0.0f)
                , gateLengthFrac(0.5f)
                , accentAmount(0.5f)
                , currentStep(0)
                , stepPhase(0.0f)
                , gateHigh(false)
                , pendulumDir(1)
            {
                for (int i = 0; i < 16; i++) {
                    steps[i].enabled = (i == 0);
                    steps[i].probability = 100;
                }
            }

            void ctagGate16::SetStep(int idx, bool enabled, uint8_t probability) {
                if (idx < 0 || idx >= 16) return;
                steps[idx].enabled = enabled;
                steps[idx].probability = probability > 100 ? 100 : probability;
            }

            void ctagGate16::SetStepEnabled(int idx, bool enabled) {
                if (idx < 0 || idx >= 16) return;
                steps[idx].enabled = enabled;
            }

            bool ctagGate16::GetStepEnabled(int idx) const {
                if (idx < 0 || idx >= 16) return false;
                return steps[idx].enabled;
            }

            void ctagGate16::SetStepProbability(int idx, uint8_t p) {
                if (idx < 0 || idx >= 16) return;
                steps[idx].probability = p > 100 ? 100 : p;
            }

            uint8_t ctagGate16::GetStepProbability(int idx) const {
                if (idx < 0 || idx >= 16) return 0;
                return steps[idx].probability;
            }

            void ctagGate16::SetStepLength(float beats) {
                if (beats < 0.25f) beats = 0.25f;
                if (beats > 4.0f) beats = 4.0f;
                stepLength = beats;
            }

            float ctagGate16::GetStepLength() const { return stepLength; }

            void ctagGate16::SetDirection(Direction dir) { direction = dir; }
            ctagGate16::Direction ctagGate16::GetDirection() const { return direction; }

            // cv_buffer/trig_buffer are stack locals of audio_task, so an
            // out-of-range index from a hand-edited mod-config.jsn would
            // corrupt live audio-task stack variables every block.
            void ctagGate16::SetTrigSlot(int slot) { trigSlot = ClampTrigSlot(slot); }
            int ctagGate16::GetTrigSlot() const { return trigSlot; }
            void ctagGate16::SetAccentSlot(int slot) { accentSlot = ClampCVSlot(slot); }
            int ctagGate16::GetAccentSlot() const { return accentSlot; }

            void ctagGate16::SetSwing(float swing) {
                swingAmount = swing < 0.0f ? 0.0f : (swing > 1.0f ? 1.0f : swing);
            }
            float ctagGate16::GetSwing() const { return swingAmount; }

            void ctagGate16::SetGateLength(float frac) {
                gateLengthFrac = frac < 0.1f ? 0.1f : (frac > 1.0f ? 1.0f : frac);
            }
            float ctagGate16::GetGateLength() const { return gateLengthFrac; }

            void ctagGate16::SetAccentAmount(float a) {
                accentAmount = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
            }
            float ctagGate16::GetAccentAmount() const { return accentAmount; }

            void ctagGate16::Process(float bpm, float *cv_buffer, uint8_t *trig_buffer, uint32_t block_size) {
                if (bpm < 1.0f) return;
                // Nothing to emit: skip the whole per-sample loop. This is the
                // factory default on every fresh boot, so the saving matters
                // against the ~174k cycle per-block budget.
                if (trigSlot < 0 && accentSlot < 0) return;

                // Loop-invariant: hoisted out of the per-sample loop so the
                // 1378 Hz path does not pay a soft-float division per sample.
                const float kSampleRate = 44100.0f;
                const float beatsPerSample = bpm / 60.0f / kSampleRate;
                const float phaseDelta = beatsPerSample / (stepLength > 0.0f ? stepLength : 1.0f);

                // Swing lengthens the off-beat rather than speeding it up.
                // Scaling the rate instead would shorten the odd step, which is
                // reverse swing. At full swing this yields a 1.5:1 off/on
                // ratio (a 66% shuffle).
                const float kSwingDepth = 0.2f;
                const float onBeatLength = 1.0f - swingAmount * kSwingDepth;
                const float offBeatLength = 1.0f + swingAmount * kSwingDepth;

                for (uint32_t s = 0; s < block_size; s++) {
                    // The step currently playing determines how long it lasts.
                    const float stepLengthNow = (currentStep % 2 == 0)
                                                ? onBeatLength
                                                : offBeatLength;
                    stepPhase += phaseDelta;

                    if (stepPhase >= stepLengthNow) {
                        stepPhase -= stepLengthNow;
                        int nextStep;
                        if (direction == Direction::FWD) {
                            nextStep = (currentStep + 1) % 16;
                        } else if (direction == Direction::BWD) {
                            nextStep = (currentStep + 15) % 16;
                        } else if (direction == Direction::PENDULUM) {
                            nextStep = currentStep + pendulumDir;
                            if (nextStep >= 16) { nextStep = 14; pendulumDir = -1; }
                            else if (nextStep < 0) { nextStep = 1; pendulumDir = 1; }
                        } else {
                            nextStep = std::rand() % 16;
                        }
                        currentStep = nextStep;

                        if (steps[currentStep].enabled) {
                            uint8_t roll = std::rand() % 100;
                            gateHigh = (roll < steps[currentStep].probability);
                        } else {
                            gateHigh = false;
                        }
                    }

                    if (trigSlot >= 0 && trig_buffer != nullptr) {
                        bool gateActive = gateHigh && (stepPhase < gateLengthFrac);
                        trig_buffer[trigSlot] = gateActive ? 1 : 0;
                    }

                    if (accentSlot >= 0 && cv_buffer != nullptr) {
                        float accent = 0.0f;
                        if (gateHigh && stepPhase < gateLengthFrac) {
                            accent = accentAmount;
                        }
                        cv_buffer[accentSlot] = accent;
                    }
                }
            }

            void ctagGate16::Reset() {
                currentStep = 0;
                stepPhase = 0.0f;
                gateHigh = false;
                pendulumDir = 1;
            }
        } // HELPERS
    } // SP
} // CTAG
