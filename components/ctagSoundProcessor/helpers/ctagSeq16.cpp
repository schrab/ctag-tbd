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

#include "ctagSeq16.hpp"
#include <cstdlib>
#include <cmath>

namespace CTAG {
    namespace SP {
        namespace HELPERS {

            ctagSeq16::ctagSeq16() {
                for (int i = 0; i < 16; i++) steps[i] = 0.5f;
                stepLength = 1.0f;
                direction = Direction::FWD;
                slewFactor = 0.0f;
                cvSlot = -1;
                trigSlot = -1;
                currentStep = 0;
                stepPhase = 0.0f;
                currentOutput = 0.5f;
                targetOutput = 0.5f;
                trigOn = false;
                pendulumDir = 1;
            }

            void ctagSeq16::SetStep(int idx, float value) {
                if (idx < 0 || idx >= 16) return;
                steps[idx] = value;
            }

            float ctagSeq16::GetStep(int idx) const {
                if (idx < 0 || idx >= 16) return 0.0f;
                return steps[idx];
            }

            void ctagSeq16::SetStepLength(float beats) {
                if (beats < 0.125f) beats = 0.125f;
                if (beats > 8.0f) beats = 8.0f;
                stepLength = beats;
            }

            float ctagSeq16::GetStepLength() const { return stepLength; }

            void ctagSeq16::SetDirection(Direction dir) { direction = dir; }
            ctagSeq16::Direction ctagSeq16::GetDirection() const { return direction; }

            void ctagSeq16::SetSlew(float slew) {
                if (slew < 0.0f) slew = 0.0f;
                if (slew > 1.0f) slew = 1.0f;
                slewFactor = slew;
            }

            float ctagSeq16::GetSlew() const { return slewFactor; }

            void ctagSeq16::SetCVSlot(int slot) { cvSlot = slot; }
            int ctagSeq16::GetCVSlot() const { return cvSlot; }
            void ctagSeq16::SetTrigSlot(int slot) { trigSlot = slot; }
            int ctagSeq16::GetTrigSlot() const { return trigSlot; }

            static int nextStepFwd(int current) {
                return (current + 1) % 16;
            }

            static int nextStepBwd(int current) {
                return (current + 15) % 16;
            }

            void ctagSeq16::Process(float bpm, float *cv_buffer, uint8_t *trig_buffer, uint32_t block_size) {
                if (cvSlot < 0 && trigSlot < 0) return;

                // Calculate phase advance per sample
                float blocks_per_sec = 44100.0f / static_cast<float>(block_size);
                float beats_per_sec = bpm / 60.0f;
                float delta_phase_per_block = beats_per_sec / blocks_per_sec;

                stepPhase += delta_phase_per_block;
                trigOn = false;

                // Check for step advance
                while (stepPhase >= stepLength) {
                    stepPhase -= stepLength;
                    trigOn = true;

                    targetOutput = steps[currentStep];

                    // Advance to next step based on direction
                    if (direction == Direction::FWD) {
                        currentStep = nextStepFwd(currentStep);
                    } else if (direction == Direction::BWD) {
                        currentStep = nextStepBwd(currentStep);
                    } else if (direction == Direction::PENDULUM) {
                        int next = currentStep + pendulumDir;
                        if (next >= 16) { next = 14; pendulumDir = -1; }
                        else if (next < 0) { next = 1; pendulumDir = 1; }
                        currentStep = next;
                    } else { // RANDOM
                        currentStep = rand() % 16;
                    }
                }

                // Slew: linear interpolation toward target
                if (slewFactor > 0.0f) {
                    float slewStep = 1.0f - slewFactor;
                    if (slewStep < 0.01f) slewStep = 0.01f;
                    currentOutput += (targetOutput - currentOutput) * slewStep;
                } else {
                    currentOutput = targetOutput;
                }

                // Write to CV buffer
                if (cvSlot >= 0) {
                    cv_buffer[cvSlot] = currentOutput * 2.0f - 1.0f; // 0-1 → -1 to 1
                }

                // Write to trigger buffer
                if (trigSlot >= 0) {
                    trig_buffer[trigSlot] = trigOn ? 1 : 0;
                }
            }

            void ctagSeq16::Reset() {
                currentStep = 0;
                stepPhase = 0.0f;
                trigOn = false;
                pendulumDir = 1;
            }
        } // HELPERS
    } // SP
} // CTAG
