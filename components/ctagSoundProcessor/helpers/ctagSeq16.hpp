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
            class ctagSeq16 {
            public:
                enum class Direction { FWD, BWD, PENDULUM, RANDOM };

                ctagSeq16();

                void SetStep(int idx, float value);
                float GetStep(int idx) const;
                void SetStepLength(float beats); // 0.25, 0.5, 1, 2, 4
                float GetStepLength() const;
                void SetDirection(Direction dir);
                Direction GetDirection() const;
                void SetSlew(float slew); // 0.0 = instant, 1.0 = very slow
                float GetSlew() const;

                void SetCVSlot(int slot);
                int GetCVSlot() const;
                void SetTrigSlot(int slot);
                int GetTrigSlot() const;

                // Called each audio block to advance step based on BPM
                void Process(float bpm, float *cv_buffer, uint8_t *trig_buffer, uint32_t block_size);

                // Reset to start
                void Reset();

            private:
                float steps[16];
                float stepLength; // beats per step
                Direction direction;
                float slewFactor; // 0-1, 0=instant
                int cvSlot;
                int trigSlot;

                int currentStep;
                float stepPhase; // fractional step position
                float currentOutput; // slewed output value
                float targetOutput;
                bool trigOn; // trigger gate state

                int pendulumDir; // 1 or -1 for pendulum mode
            };
        } // HELPERS
    } // SP
} // CTAG
