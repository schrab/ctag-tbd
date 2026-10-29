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

#include "UIMenuPage.hpp"
#include <cstdint>
#include <cstddef>

namespace CTAG {
    namespace CTRL {
        // Vertical-fader mixer page. Four control strips (input gain, input
        // source, output L, output R), the first and last two carrying L/R VU
        // meters. Left encoder moves between strips, right encoder edits the
        // active strip (no edit mode); continuous (non-deferred) strips
        // re-apply the codec live while turning. Output routing and mixer
        // mode stay on the System page — this page is only the level/source
        // faders.
        class UIMenuPageMix final : public UIMenuPage {
        public:
            void init() override;
            void deinit() override;
            void doRedraw() override;
            void onButton(int btnId, bool longPress) override;
            void onEncoder(int delta) override;      // left encoder: strip select
            void onValueEncoder(int delta) override; // right encoder: live fader edit
            void onTick() override;
            bool onBack() override;
            bool wantsTick() const override { return true; }

            // Debug introspection: host-side scripts assert on these instead of
            // inferring state from pixels (physical input races with injected
            // events, and a silent no-op edit is otherwise invisible).
            void DebugStateJson(std::string &out) override;

        private:
            // Layout: 4 strips across 128 px, 30 px wide with a 4 px margin.
            // Content sits below the persistent nav bar (rows 0..2).
            static constexpr int STRIPS = 4;
            static constexpr int GAIN_STRIP = 0;    // input_gain
            static constexpr int SRC_STRIP = 1;     // input_source, needs alias handling
            static constexpr int OUT_L_STRIP = 2;   // ch0_codecLvlOut
            static constexpr int OUT_R_STRIP = 3;   // ch1_codecLvlOut
            static constexpr int STRIP_W = 30;
            static constexpr int STRIP_X0 = 4;
            static constexpr int LABEL_Y = 5;
            static constexpr int FADER_Y = 14;
            static constexpr int FADER_H = 28;
            static constexpr int FADER_W = 7;
            static constexpr int VU_W = 5;
            static constexpr int VU_H = 28;
            // Meter smoothing, tuned for the 25 Hz tick: hold the peak
            // briefly (200 ms) then fall at DECAY_PER_TICK (1200/s) for a
            // ~1.7 s release from full scale.
            static constexpr int VU_DECAY_PER_TICK = 48;
            static constexpr int VU_HOLD_TICKS = 5;
            static constexpr int VALUE_Y = 45;

            // First VU channel of the strip's meter: IN meters the stereo
            // input pair (vu 0+1), each output strip meters its own channel
            // (vu 2 / vu 3) — they are mono channels, not L/R pairs.
            enum MeterSrc : int { METER_NONE = -1, METER_IN = 0, METER_OUT_L = 2, METER_OUT_R = 3 };

            struct Strip {
                const char *label;
                const char *id;
                int min;
                int max;
                bool deferred;
                const char *enumOpts; // nullptr => plain int value
                MeterSrc meter;
                int meterN;           // bars: 2 for the stereo input, 1 per output strip
            };

            static const Strip STRIP_TABLE[STRIPS];

            int cursor;
            bool dirty; // staged values differ from the persisted config
            int value[STRIPS];
            int meter[STRIPS][2]; // smoothed level 0..1000, [0]=L [1]=R
            int hold[STRIPS][2];  // remaining peak-hold ticks

            int encoderAccel;
            int lastEncDir;
            uint32_t lastEncTick;

            void parseConfig();
            void applyCurrent(bool includeDeferred);
            void refreshMeters();
            void drawStrip(int i);
            void drawFader(int i);
            void drawSwitch(int i); // enum strips: discrete segments, not a fader
            void drawMeters(int i);
            void valueText(int i, char *buf, size_t len) const;
        };
    }
}
