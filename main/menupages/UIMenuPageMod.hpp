#pragma once

#include "UIMenuPage.hpp"

namespace CTAG {
    namespace CTRL {
        class UIMenuPageMod final : public UIMenuPage {
        public:
            void init() override;
            void deinit() override;
            void doRedraw() override;
            void onEncoder(int delta) override;
            void onButton(int btnId, bool longPress) override;
            bool onBack() override;

        private:
            enum SubPage { SP_MAIN, SP_LFO1, SP_LFO2, SP_CC_SLOTS, SP_CC_EDIT };
            SubPage subPage;
            int cursor;
            int ccEditSlot;
            int ccEditValue;
            bool editing;

            void redrawMain();
            void redrawLFO(int lfo);
            void redrawCCSlots();
            void redrawCCEdit();
        };
    }
}
