#pragma once

#include "UIMenuPage.hpp"
#include <cstdint>

namespace CTAG {
    namespace CTRL {
        // PANEL_VARG: the black-metal chord/riff sequencer (harmonic content
        // from docs/Black Metal Chords, Scales, Harmonies, and Progressions.md).
        // Single setup page: progression/quality-bias/hold + rhythm/length +
        // clock/division/root + the four slot assignments, over a live strip
        // (current chord name + dyad pair + 16-glyph riff row with playhead).
        // All row labels are full words. Name tables are panel-local copies
        // of the core's (main/VargEngineCore.hpp is a DSP-build header — the
        // panel compiles its own, keep both in sync).
        class UIMenuPageVarg final : public UIMenuPage {
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
            int cursor = 0;
            bool dirty = false;

            // redraw triggers: live strip changes AND DSP-confirmed value
            // pushes (GetVargVersion bumps on every link/pushVarg receipt)
            uint32_t lastDrawnSnapshot = 0xFFFFFFFF;
            uint32_t lastDrawnVersion = 0xFFFFFFFF;
            void drawOutputLine();

            static constexpr int ROW_COUNT = 14;
            void adjustRow(int row, int delta);
            void formatRow(int row, char *buf, int buflen);
        };
    }
}
