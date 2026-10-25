#pragma once

#include "UIMenuPage.hpp"
#include <cstdint>

namespace CTAG {
    namespace CTRL {
        // PANEL_MOD: configuration of the Marbles random modulator
        // (clock, t gate section, X/Y CV sections) and the MIDI-learn
        // dynamic CC slots.
        class UIMenuPageMod final : public UIMenuPage {
        public:
            void init() override;
            void deinit() override;
            void doRedraw() override;
            void onEncoder(int delta) override;
            void onButton(int btnId, bool longPress) override;
            bool onBack() override;

        private:
            enum SubPage { SP_MAIN, SP_CLOCK, SP_T, SP_X, SP_Y, SP_CC_SLOTS, SP_CC_EDIT };
            SubPage subPage;
            int cursor;
            int ccEditSlot;
            bool editing;
            bool dirty = false;

            // tap tempo (Clock page): average of the last 4 tap intervals
            uint32_t lastTapTick = 0;
            uint32_t tapIntervalHistory[4] = {0};
            int tapHistoryCount = 0;
            void onTapTempo();

            int rowCount() const;
            bool rowIsAction(int row) const;
            void activateRow(int row);
            void adjustRow(int row, int delta);
            void formatRow(int row, char *buf, int buflen);

            void redrawMain();
            void redrawRows(const char *title);
            void redrawCCSlots();
            void redrawCCEdit();
        };
    }
}
