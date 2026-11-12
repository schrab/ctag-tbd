#pragma once

#include "UIMenuPage.hpp"
#include <cstdint>

namespace CTAG {
    namespace CTRL {
        // PANEL_SEQ: the two rhythmic GATE and two generative ARP
        // modulators (rack port) that run alongside Marbles. MAIN lists the
        // four sources over a live output strip; each source opens its
        // setup sub-page. All row labels are full words (spec §4.5).
        class UIMenuPageSeq final : public UIMenuPage {
        public:
            void init() override;
            void deinit() override;
            void doRedraw() override;
            void onEncoder(int delta) override;      // left encoder: list scroll
            void onValueEncoder(int delta) override; // right encoder: live row edit
            void onButton(int btnId, bool longPress) override;
            bool onBack() override;
            void onTick() override;
            bool wantsTick() const override;

        private:
            enum SubPage { SP_MAIN, SP_GATE1, SP_GATE2, SP_ARP1, SP_ARP2 };
            SubPage subPage = SP_MAIN;
            int cursor = 0;
            bool dirty = false;

            // redraw triggers: live strip changes AND DSP-confirmed value
            // pushes (GetSeqVersion bumps on every link/pushSeq receipt)
            uint32_t lastDrawnSnapshot = 0xFFFFFFFF;
            uint32_t lastDrawnVersion = 0xFFFFFFFF;
            void drawOutputLine();

            int rowCount() const;
            bool rowIsAction(int row) const;
            void adjustRow(int row, int delta);
            void formatRow(int row, char *buf, int buflen);

            void redrawMain();
            void redrawRows(const char *title);
        };
    }
}
