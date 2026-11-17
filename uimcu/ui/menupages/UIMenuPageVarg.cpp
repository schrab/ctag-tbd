#include "UIMenuPageVarg.hpp"
#include "Display.hpp"
#include "DspProxy.hpp"
#include "CVSlotNames.hpp"
#include <cstring>
#include <cstdio>

using namespace CTAG::DRIVERS;

namespace CTAG {
    namespace CTRL {

        // ---- label tables (panel-local copies of the core's names — keep
        // in sync with main/VargEngineCore.hpp kProgressions/kRiffNames) ----
        static const char* progNames[11] = {
            "Generative", "i-bVII", "i-bVI-bVII", "i-bIII-bVI-bVII", "Weitzmann",
            "add9-dim7", "Mediant", "i-bII-v", "Pool 5", "Planing", "i-I+"
        };
        static const char* riffNames[17] = {
            "Generative", "TRM16", "TRM8", "QRT", "HALF", "BURST", "GAL", "RGAL",
            "68L", "STAB", "SKIP", "CHAR", "RESP", "TURN", "DIRGE", "CHOP", "TMR"
        };
        static const char* vargClockNames[3] = {"Global", "Internal", "MIDI"};
        // rack kModDivisions order (index 7 = 1/16)
        static const char* vargDivNames[13] = {
            "8/1", "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16",
            "1/32", "1/4T", "1/8T", "1/16T", "1/32T"
        };
        // flats spelling — the genre's own naming (Eb, Bb, Ab...)
        static const char* noteNames[12] = {
            "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
        };
        // chord-quality short names (panel copy of kQualityPair semantics)
        static const char* qualNames[9] = {
            "5", "M", "m", "5b5", "dim7", "m7b5", "aug", "add9", "m+b9"
        };
        static const char* holdNames[3] = {"1 bar", "2 bars", "4 bars"};

        // snapshot bit map (VargEngine::GetOutputSnapshot)
        //   bit 0 trem, bit 1 chord-onset, bits 2-5 riff step,
        //   bits 6-9 chord offset, bits 10-13 quality, bits 14-17 tonic pc,
        //   bits 18-21 pair interval, bits 22-25 chord index
        inline int SnapStep(uint32_t s)  { return (int)((s >> 2) & 0xFu); }
        inline int SnapOff(uint32_t s)   { return (int)((s >> 6) & 0xFu); }
        inline int SnapQual(uint32_t s)  { return (int)((s >> 10) & 0xFu); }
        inline int SnapRoot(uint32_t s)  { return (int)((s >> 14) & 0xFu); }
        inline int SnapPair(uint32_t s)  { return (int)((s >> 18) & 0xFu); }
        inline bool SnapChg(uint32_t s)  { return (s & 0x2u) != 0; }

        // ---- live output strip ----
        // The chord readout rides the title row ("VARG Am+3 +3 !"), the
        // 16-glyph riff row sits at the page bottom — ○ muted, ● gate,
        // ☐ playhead.
        void UIMenuPageVarg::drawOutputLine() {
            const uint32_t s = DspProxy::GetVargSnapshot();
            const uint32_t mask = DspProxy::GetVargMask();
            const int step = SnapStep(s);
            for (int st = 0; st < 16; ++st) {
                const int x = st * 8;
                const bool gate = ((mask >> (15 - st)) & 1u) != 0;
                const char *g = "\xE2\x97\x8B";                // ○ muted
                if (st == step) g = "\xE2\x98\x90";            // ☐ playhead
                else if (gate) g = "\xE2\x97\x8F";             // ●
                Display::DrawString(x, 53, g, FONT_UI);
            }
        }

        void UIMenuPageVarg::init() {
            cursor = 0;
        }

        void UIMenuPageVarg::deinit() {
            if (dirty) { DspProxy::VargSaveConfig(); dirty = false; }
        }

        void UIMenuPageVarg::onTick() {
            // redraw on live-strip changes and on DSP-confirmed value pushes
            uint32_t s = DspProxy::GetVargSnapshot();
            uint32_t v = DspProxy::GetVargVersion();
            if (s != lastDrawnSnapshot || v != lastDrawnVersion) {
                doRedraw();
            }
        }

        bool UIMenuPageVarg::wantsTick() const { return true; }

        // ---- row model ----
        // 0 Progression, 1 Bias, 2 Hold, 3 Rhythm, 4 Density, 5 Syncopation,
        // 6 Length, 7 Clock, 8 Division, 9 Root, 10 Root Slot, 11 Pair Slot,
        // 12 Trem Slot, 13 Chg Slot

        void UIMenuPageVarg::adjustRow(int row, int delta) {
            switch (row) {
                case 0: DspProxy::SetVargProg(DspProxy::GetVargProg() + delta); break;
                case 1: DspProxy::SetVargBias(DspProxy::GetVargBias() + delta * 5); break;
                case 2: DspProxy::SetVargHold(DspProxy::GetVargHold() + delta); break;
                case 3: DspProxy::SetVargRhythm(DspProxy::GetVargRhythm() + delta); break;
                case 4: DspProxy::SetVargDens(DspProxy::GetVargDens() + delta); break;
                case 5: DspProxy::SetVargSync(DspProxy::GetVargSync() + delta * 5); break;
                case 6: DspProxy::SetVargLen(DspProxy::GetVargLen() + delta * 5); break;
                case 7: DspProxy::SetVargClk(DspProxy::GetVargClk() + delta); break;
                case 8: DspProxy::SetVargDiv(DspProxy::GetVargDiv() + delta); break;
                case 9: DspProxy::SetVargRoot(DspProxy::GetVargRoot() + delta); break;
                case 10: DspProxy::SetVargRootSlot(DspProxy::GetVargRootSlot() + delta); break;
                case 11: DspProxy::SetVargPairSlot(DspProxy::GetVargPairSlot() + delta); break;
                case 12: DspProxy::SetVargTremSlot(DspProxy::GetVargTremSlot() + delta); break;
                case 13: DspProxy::SetVargChgSlot(DspProxy::GetVargChgSlot() + delta); break;
                default: break;
            }
            dirty = true;
        }

        void UIMenuPageVarg::formatRow(int row, char *buf, int buflen) {
            switch (row) {
                case 0:
                    snprintf(buf, buflen, " Progression:%s",
                             progNames[DspProxy::GetVargProg() % 11]);
                    break;
                case 1: {
                    int b = DspProxy::GetVargBias();
                    const char *band = (b <= 33) ? "Raw" : (b <= 66) ? "Triad" : "Diss";
                    snprintf(buf, buflen, " Bias:%d%% %s", b, band);
                    break;
                }
                case 2:
                    snprintf(buf, buflen, " Hold:%s",
                             holdNames[DspProxy::GetVargHold() % 3]);
                    break;
                case 3:
                    snprintf(buf, buflen, " Rhythm:%s",
                             riffNames[DspProxy::GetVargRhythm() % 17]);
                    break;
                case 4:
                    snprintf(buf, buflen, " Density:%d", DspProxy::GetVargDens());
                    break;
                case 5:
                    snprintf(buf, buflen, " Syncopation:%d%%", DspProxy::GetVargSync());
                    break;
                case 6:
                    snprintf(buf, buflen, " Length:%d%%", DspProxy::GetVargLen());
                    break;
                case 7:
                    snprintf(buf, buflen, " Clock:%s",
                             vargClockNames[DspProxy::GetVargClk()]);
                    break;
                case 8:
                    snprintf(buf, buflen, " Division:%s",
                             vargDivNames[DspProxy::GetVargDiv()]);
                    break;
                case 9:
                    snprintf(buf, buflen, " Root:%s",
                             noteNames[DspProxy::GetVargRoot() % 12]);
                    break;
                case 10: {
                    int s = DspProxy::GetVargRootSlot();
                    if (s >= 0 && s < (int)(sizeof(cvSlotDisplayNames) / sizeof(cvSlotDisplayNames[0])))
                        snprintf(buf, buflen, " Root Slot:%s", cvSlotDisplayNames[s]);
                    else
                        snprintf(buf, buflen, " Root Slot:%d", s);
                    break;
                }
                case 11: {
                    int s = DspProxy::GetVargPairSlot();
                    if (s >= 0 && s < (int)(sizeof(cvSlotDisplayNames) / sizeof(cvSlotDisplayNames[0])))
                        snprintf(buf, buflen, " Pair Slot:%s", cvSlotDisplayNames[s]);
                    else
                        snprintf(buf, buflen, " Pair Slot:%d", s);
                    break;
                }
                case 12:
                    snprintf(buf, buflen, " Trem Slot:%s",
                             TrigSlotName(DspProxy::GetVargTremSlot()));
                    break;
                case 13:
                    snprintf(buf, buflen, " Chg Slot:%s",
                             TrigSlotName(DspProxy::GetVargChgSlot()));
                    break;
            }
        }

        // ---- event handling ----

        void UIMenuPageVarg::onEncoder(int delta) {
            cursor += delta;
            if (cursor < 0) cursor = 0;
            if (cursor > ROW_COUNT - 1) cursor = ROW_COUNT - 1;
            doRedraw();
        }

        void UIMenuPageVarg::onValueEncoder(int delta) {
            adjustRow(cursor, delta);
            doRedraw();
        }

        void UIMenuPageVarg::onButton(int btnId, bool longPress) {
            // No subpages: all rows edit in place via the value encoder.
            (void)btnId;
            (void)longPress;
            doRedraw();
        }

        bool UIMenuPageVarg::onBack() {
            return false;   // single page: BACK returns to ROOT
        }

        // ---- rendering ----

        void UIMenuPageVarg::doRedraw() {
            Display::Clear();
            char buf[48];
            // title row doubles as the chord readout
            {
                const uint32_t s = DspProxy::GetVargSnapshot();
                const int rp = SnapRoot(s), off = SnapOff(s), q = SnapQual(s),
                          pr = SnapPair(s);
                const int chordPc = (rp + off) % 12;
                if (q < 9)
                    snprintf(buf, sizeof(buf), "VARG %s%s +%d%s", noteNames[chordPc],
                             qualNames[q], pr, SnapChg(s) ? " !" : "");
                else
                    snprintf(buf, sizeof(buf), "VARG");
                Display::DrawString(0, ROW(0), buf, FONT_UI);
            }

            const int VIS = VISIBLE_ITEMS_HDR - 1;  // bottom rows = output strip
            int scrollOff = (cursor > VIS - 1) ? cursor - (VIS - 1) : 0;
            int visible = ROW_COUNT - scrollOff;
            if (visible > VIS) visible = VIS;
            for (int i = 0; i < visible; i++) {
                int idx = scrollOff + i;
                int y = ROW_HDR(i);
                formatRow(idx, buf, sizeof(buf));
                Display::DrawString(0, y, buf, FONT_UI);
            }
            int cy = ROW_HDR(cursor - scrollOff);
            Display::InvertRect(0, cy - 1, 128, 8);
            if (ROW_COUNT > VIS) {
                Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0_HDR, VIS * LINE_H, ROW_COUNT, cursor);
            }
            drawOutputLine();
            lastDrawnSnapshot = DspProxy::GetVargSnapshot();
            lastDrawnVersion = DspProxy::GetVargVersion();
            Display::Flush();
        }

    }
}
