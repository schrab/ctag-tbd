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
#include "freertos/FreeRTOS.h"

namespace CTAG {
    namespace SP {
        namespace HELPERS {
            class ctagTempo {
            public:
                enum class Source { INTERNAL, MIDI_CLOCK };

                ctagTempo();

                void SetSampleRate(float fs_Hz, uint32_t block_size);
                void SetBPM(float bpm);
                float GetBPM() const;

                void SetSource(Source src);
                Source GetSource() const;

                void Start();
                void Stop();
                void Continue();
                bool IsRunning() const;

                // Call once per audio block to advance internal phase
                void Tick();

                // Returns current phase (0.0 to 1.0, where 1.0 = 1 beat)
                float GetPhase() const;

                // For MIDI Clock: call on every 0xF8 message
                void OnMidiClock();

                // For Tap Tempo: call on each tap event
                void OnTapTempo();

            private:
                Source source = Source::INTERNAL;
                float bpm = 120.0f;
                float phase = 0.0f;
                float delta_phase_per_block = 0.0f;
                bool running = false;

                // Tap tempo state
                TickType_t lastTapTick = 0;
                uint32_t tapCount = 0;
            };
        } // HELPERS
    } // SP
} // CTAG
