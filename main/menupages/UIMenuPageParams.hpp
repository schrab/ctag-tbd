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

namespace CTAG {
    namespace CTRL {
        class UIMenuPageParams final : public UIMenuPage {
        public:
            void init() override;
            void deinit() override;
            void doRedraw() override;
            void onEncoder(int delta) override;
            void onButton(int btnId, bool longPress) override;
            bool onBack() override;

        private:
            struct ParamInfo {
                char id[24];
                char name[24];
                char type[24];
                int min;
                int max;
                int current;
            };

            struct PresetInfo {
                char name[32];
                int number;
            };

            enum Mode { MODE_SELECT, MODE_EDIT, MODE_MAP, MODE_PSET, MODE_VALUEEDIT, MODE_MAPEDIT, MODE_PRESETS };
            int cursor;
            int scrollOffset;
            Mode mode;
            int paramCount;
            static const int MAX_PARAMS = 64;
            ParamInfo params[MAX_PARAMS];

            static const int MAX_PRESETS = 64;
            PresetInfo presets[MAX_PRESETS];
            int presetCount;
            int presetChan; // channel we're editing presets for

            void parseParams();
            void parsePresets(int chan);
            void redrawSelect();
            void redrawEdit();
            void redrawPresets();
            int paramIndexToScreen(int idx) const;
        };
    }
}
