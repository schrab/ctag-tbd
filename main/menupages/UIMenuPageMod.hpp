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
            enum SubPage { SP_MAIN, SP_LFO1, SP_LFO2, SP_TEMPO, SP_CC_SLOTS, SP_CC_EDIT, SP_SEQ1, SP_SEQ2, SP_GATE1, SP_GATE2 };
            SubPage subPage;
            int cursor;
            int ccEditSlot;
            int ccEditValue;
            bool seqEditParams; // true = editing global params sub-screen
            bool editing;
            bool dirty = false;

            void redrawMain();
            void redrawLFO(int lfo);
            void redrawTempo();
            void redrawCCSlots();
            void redrawCCEdit();
            void redrawSeq(int seq);
            void redrawGate(int g);
        };
    }
}
