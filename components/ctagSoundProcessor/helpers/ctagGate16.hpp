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

namespace CTAG {
    namespace SP {
        namespace HELPERS {
            class ctagGate16 {
            public:
                enum class Direction { FWD, BWD, PENDULUM, RANDOM };

                ctagGate16();

                struct StepData {
                    bool enabled;
                    uint8_t probability; // 0-100
                };

                void SetStep(int idx, bool enabled, uint8_t probability);
                void SetStepEnabled(int idx, bool enabled);
                bool GetStepEnabled(int idx) const;
                void SetStepProbability(int idx, uint8_t p);
                uint8_t GetStepProbability(int idx) const;

                void SetStepLength(float beats);
                float GetStepLength() const;
                void SetDirection(Direction dir);
                Direction GetDirection() const;

                void SetTrigSlot(int slot);
                int GetTrigSlot() const;
                void SetAccentSlot(int slot);
                int GetAccentSlot() const;

                void SetSwing(float swing); // 0.0 = straight, 1.0 = max swing
                float GetSwing() const;
                void SetGateLength(float frac); // fraction of step that gate is high (0.1-1.0)
                float GetGateLength() const;
                void SetAccentAmount(float a); // global accent 0.0-1.0
                float GetAccentAmount() const;

                void Process(float bpm, float *cv_buffer, uint8_t *trig_buffer, uint32_t block_size);

                void Reset();

            private:
                StepData steps[16];
                float stepLength;
                Direction direction;
                int trigSlot;
                int accentSlot;
                float swingAmount;
                float gateLengthFrac;
                float accentAmount;

                int currentStep;
                float stepPhase;
                bool gateHigh;
                int pendulumDir;
            };
        } // HELPERS
    } // SP
} // CTAG
