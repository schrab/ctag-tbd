#include "UIMenuPageMod.hpp"
#include "Display.hpp"
#include "DspProxy.hpp"
#include "CVSlotNames.hpp"
#include <cstring>
#include <cstdio>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using namespace CTAG::DRIVERS;

namespace CTAG {
    namespace CTRL {

        // ---- label tables ----
        static const char* tModelNames[7] = {
            "Coins", "Cluster", "Drums", "Indep", "Divider", "3State", "Markov"
        };
        static const char* rangeNames[3] = {"0.25x", "1x", "4x"};
        static const char* cvRangeNames[3] = {"2oct", "5oct", "10oct"};
        static const char* ctlNames[3] = {"Ident", "Bump", "Tilt"};
        static const char* dvNames[3] = {"Off", "On", "Lock"};
        static const char* xyClkNames[5] = {"T123", "T1", "T2", "T3", "MIDI"};
        static const char* yDivNames[12] = {
            "1/64", "1/48", "1/32", "1/24", "1/16", "1/12",
            "1/8", "1/6", "1/4", "1/3", "1/2", "1/1"
        };
        static const char* scaleNames[8] = {
            "Ionian", "Dorian", "Phrygian", "Lydian",
            "Mixolydian", "Aeolian", "Locrian", "Penta"
        };

        // Live output indication: T1/T2/T3 dots (filled = gate high) bunched
        // at the left (8 px pitch, 2 px gaps), then X1/X2/X3/Y center-fill
        // bars (frameless, ABSOLUTE ±5 V ruler — bar position does not move
        // when the octave range changes) across four 26 px cells — the wider
        // cells give the CV bars ~44% more travel than the old
        // seven-equal-18px layout.
        void UIMenuPageMod::drawOutputLine() {
            uint32_t s = DspProxy::GetOutputSnapshot();
            for (int i = 0; i < 3; i++) {
                Display::DrawRect(1 + i * 8, 57, 6, 6, (s >> i) & 1);
            }
            const int shifts[4] = {3, 10, 17, 24};
            for (int i = 0; i < 4; i++) {
                int x = 24 + i * 26;         // cells at 24/50/76/102, to 128
                int cx = x + 13;             // zero boundary of the cell
                int q = (s >> shifts[i]) & 0x7f;
                float n = (q - 64) / 63.5f;
                int w = (int)(fabsf(n) * 13.0f + 0.5f);
                if (w > 13) w = 13;
                Display::DrawVLine(cx, 57, 6);   // zero tick
                if (w > 0) {
                    Display::DrawRect(n >= 0 ? cx : cx - w, 57, w, 6, true);
                }
            }
            lastDrawnSnapshot = s;
        }

        void UIMenuPageMod::onTick() {
            // Full redraws are gated on output changes: the display driver
            // flushes per dirty page, so unchanged frames cost nothing.
            if (DspProxy::GetOutputSnapshot() != lastDrawnSnapshot) {
                doRedraw();
            }
        }

        bool UIMenuPageMod::wantsTick() const { return true; }

        void UIMenuPageMod::init() {
            subPage = SP_MAIN;
            cursor = 0;
            ccEditSlot = 0;
            lastTapTick = 0;
            tapHistoryCount = 0;
        }

        void UIMenuPageMod::deinit() {
            if (DspProxy::IsLearning()) DspProxy::StopLearn();
            if (dirty) { DspProxy::SaveConfig(); dirty = false; }
        }

        // ---- row model: one flat list per subpage ----

        int UIMenuPageMod::rowCount() const {
            switch (subPage) {
                case SP_MAIN: return 6;
                case SP_PANEL: return 9;  // the module's 8 knobs + DV length button
                case SP_CLOCK: return 7;  // Clock, BPM, [Tap], Fine, Range, XClock, [Reset]
                case SP_T: return 8;      // Model, Range, PW, PWv, DV, T1..T3 slots
                case SP_X: return 7;      // Scale, Rng, Ctl, DV, X1..X3 slots
                case SP_Y: return 5;      // Rng, Sprd, Bias, Step, Y slot
                case SP_CC_SLOTS: return 8;
                case SP_CC_EDIT: return 3;
                default: return 1;
            }
        }

        bool UIMenuPageMod::rowIsAction(int row) const {
            if (subPage == SP_CLOCK && (row == 2 || row == 6)) return true;  // [Tap], [Reset]
            if (subPage == SP_CC_EDIT && row == 2) return true; // [Learn]
            return false;
        }

        void UIMenuPageMod::activateRow(int row) {
            if (subPage == SP_CLOCK && row == 2) {
                onTapTempo();
            } else if (subPage == SP_CLOCK && row == 6) {
                DspProxy::RequestReset();
            } else if (subPage == SP_CC_EDIT && row == 2) {
                if (DspProxy::IsLearning()) {
                    DspProxy::StopLearn();
                } else {
                    DspProxy::StartLearn();
                }
            }
        }

        // ---- tempo helpers ----
        // BPM is the GLOBAL transport tempo (manual entry, tap tempo, or
        // measured from the MIDI clock) — it never changes when the Marbles
        // Clock Range switch turns. The Marbles t-clock is its own thing,
        // derived from BPM:
        //
        //   marbles_clock = BPM * range(0.25/1/4) * 2^(fine/12)
        //
        // so the Clock Range switch re-pitches Marbles only. The Fine Rate
        // row shows the resulting Marbles clock in Hz.
        static float MasterBPM() {
            return DspProxy::GetBPM();
        }

        static float MasterHz() {
            float factor = 1.0f;
            if (DspProxy::GetTRange() == 0) factor = 0.25f;
            else if (DspProxy::GetTRange() == 2) factor = 4.0f;
            return (DspProxy::GetBPM() / 60.0f) * factor
                   * powf(2.0f, DspProxy::GetTRate() / 12.0f);
        }

        static void SetMasterBPM(float bpm) {
            if (bpm < 20.0f) bpm = 20.0f;
            if (bpm > 480.0f) bpm = 480.0f;
            DspProxy::SetBPM(bpm);
        }

        // ---- tap tempo: rolling average (max 4) of stored tap intervals ----
        void UIMenuPageMod::onTapTempo() {
            uint32_t now = xTaskGetTickCount();
            // A reference tap is valid for 3 s; older -> start a new sequence.
            bool haveRef = lastTapTick != 0 && (now - lastTapTick) <= pdMS_TO_TICKS(3000);
            if (!haveRef) tapHistoryCount = 0;
            if (haveRef) {
                uint32_t interval = now - lastTapTick;
                if (interval >= pdMS_TO_TICKS(125)) {  // < 480 BPM; ignore bounce
                    for (int i = 3; i > 0; i--) tapIntervalHistory[i] = tapIntervalHistory[i - 1];
                    tapIntervalHistory[0] = interval;
                    if (tapHistoryCount < 4) tapHistoryCount++;
                    uint32_t sum = 0;
                    for (int i = 0; i < tapHistoryCount; i++) sum += tapIntervalHistory[i];
                    float avgMs = (float)sum * portTICK_PERIOD_MS / tapHistoryCount;
                    SetMasterBPM(60000.0f / avgMs);
                    dirty = true;
                }
            }
            lastTapTick = now;
        }

        void UIMenuPageMod::adjustRow(int row, int delta) {
            switch (subPage) {
                case SP_PANEL:
                    // the hardware module's 8 knobs, in panel order
                    switch (row) {
                        case 0: DspProxy::SetDejaVuAmount(DspProxy::GetDejaVuAmount() + delta * 0.01f); break;
                        case 1: DspProxy::SetDejaVuLength(DspProxy::GetDejaVuLength() + delta); break;
                        case 2: SetMasterBPM(MasterBPM() + delta); break;          // T RATE
                        case 3: DspProxy::SetTBias(DspProxy::GetTBias() + delta * 0.01f); break;
                        case 4: DspProxy::SetTJitter(DspProxy::GetTJitter() + delta * 0.01f); break;
                        case 5: DspProxy::SetXSpread(DspProxy::GetXSpread() + delta * 0.01f); break;
                        case 6: DspProxy::SetXBias(DspProxy::GetXBias() + delta * 0.01f); break;
                        case 7: DspProxy::SetXSteps(DspProxy::GetXSteps() + delta * 0.01f); break;
                        case 8: DspProxy::SetYDivider(DspProxy::GetYDivider() + delta); break; // Y RATE
                    }
                    break;
                case SP_CLOCK:
                    switch (row) {
                        case 0: DspProxy::SetTExternal(!DspProxy::GetTExternal()); break;
                        case 1: SetMasterBPM(MasterBPM() + delta); break;
                        case 3: DspProxy::SetTRate(DspProxy::GetTRate() + delta); break;
                        // Clock Range multiplies the Marbles clock only —
                        // the global BPM never moves (see tempo helpers).
                        case 4: DspProxy::SetTRange(DspProxy::GetTRange() + delta); break;
                        case 5: DspProxy::SetXYClockSource(DspProxy::GetXYClockSource() + delta); break;
                    }
                    break;
                case SP_T:
                    switch (row) {
                        case 0: DspProxy::SetTModel(DspProxy::GetTModel() + delta); break;
                        // Clock Range multiplies the Marbles clock only.
                        case 1: DspProxy::SetTRange(DspProxy::GetTRange() + delta); break;
                        case 2: DspProxy::SetTPwMean(DspProxy::GetTPwMean() + delta * 5); break;
                        case 3: DspProxy::SetTPwStd(DspProxy::GetTPwStd() + delta * 5); break;
                        case 4: DspProxy::SetTDejaVu(DspProxy::GetTDejaVu() + delta); break;
                        case 5: DspProxy::SetTSlot(0, DspProxy::GetTSlot(0) + delta); break;
                        case 6: DspProxy::SetTSlot(1, DspProxy::GetTSlot(1) + delta); break;
                        case 7: DspProxy::SetTSlot(2, DspProxy::GetTSlot(2) + delta); break;
                    }
                    break;
                case SP_X:
                    switch (row) {
                        case 0: DspProxy::SetXScale(DspProxy::GetXScale() + delta); break;
                        case 1: DspProxy::SetXRange(DspProxy::GetXRange() + delta); break;
                        case 2: DspProxy::SetXControl(DspProxy::GetXControl() + delta); break;
                        case 3: DspProxy::SetXDejaVu(!DspProxy::GetXDejaVu()); break;
                        case 4: DspProxy::SetXSlot(0, DspProxy::GetXSlot(0) + delta); break;
                        case 5: DspProxy::SetXSlot(1, DspProxy::GetXSlot(1) + delta); break;
                        case 6: DspProxy::SetXSlot(2, DspProxy::GetXSlot(2) + delta); break;
                    }
                    break;
                case SP_Y:
                    switch (row) {
                        case 0: DspProxy::SetYRange(DspProxy::GetYRange() + delta); break;
                        case 1: DspProxy::SetYSpread(DspProxy::GetYSpread() + delta * 0.01f); break;
                        case 2: DspProxy::SetYBias(DspProxy::GetYBias() + delta * 0.01f); break;
                        case 3: DspProxy::SetYSteps(DspProxy::GetYSteps() + delta * 0.01f); break;
                        case 4: DspProxy::SetYSlot(DspProxy::GetYSlot() + delta); break;
                    }
                    break;
                case SP_CC_EDIT:
                    if (row == 0) {
                        DspProxy::SetDynamicSlotCC(ccEditSlot, DspProxy::GetDynamicSlotCC(ccEditSlot) + delta);
                    } else if (row == 1) {
                        DspProxy::SetDynamicSlotChan(ccEditSlot, DspProxy::GetDynamicSlotChan(ccEditSlot) + delta);
                    }
                    break;
                default:
                    break;
            }
            dirty = true;
        }

        void UIMenuPageMod::formatRow(int row, char *buf, int buflen) {
            switch (subPage) {
                case SP_PANEL:
                    // the hardware module's 8 knobs + DV length button, in panel order
                    switch (row) {
                        case 0:
                            snprintf(buf, buflen, " Deja Vu:%.2f", DspProxy::GetDejaVuAmount());
                            break;
                        case 1:
                            snprintf(buf, buflen, " Loop Length:%d", DspProxy::GetDejaVuLength());
                            break;
                        case 2:
                            snprintf(buf, buflen, " Clock Rate:%.0fbpm", MasterBPM());
                            break;
                        case 3:
                            snprintf(buf, buflen, " T Bias:%.2f", DspProxy::GetTBias());
                            break;
                        case 4:
                            snprintf(buf, buflen, " T Jitter:%.2f", DspProxy::GetTJitter());
                            break;
                        case 5:
                            snprintf(buf, buflen, " X Spread:%.2f", DspProxy::GetXSpread());
                            break;
                        case 6:
                            snprintf(buf, buflen, " X Bias:%.2f", DspProxy::GetXBias());
                            break;
                        case 7:
                            snprintf(buf, buflen, " X Steps:%.2f", DspProxy::GetXSteps());
                            break;
                        case 8:
                            snprintf(buf, buflen, " Y Rate:%s", yDivNames[DspProxy::GetYDivider()]);
                            break;
                    }
                    break;
                case SP_CLOCK:
                    switch (row) {
                        case 0:
                            snprintf(buf, buflen, " Clock Source:%s", DspProxy::GetTExternal() ? "MIDI" : "Int");
                            break;
                        case 1:
                            snprintf(buf, buflen, " BPM:%.1f%s", MasterBPM(),
                                     DspProxy::GetTExternal() ? " MIDI" : "");
                            break;
                        case 3:
                            snprintf(buf, buflen, " Fine Rate:%.2fHz", MasterHz());
                            break;
                        case 4:
                            snprintf(buf, buflen, " Clock Range:%s", rangeNames[DspProxy::GetTRange()]);
                            break;
                        case 5:
                            snprintf(buf, buflen, " X Clock:%s", xyClkNames[DspProxy::GetXYClockSource()]);
                            break;
                        case 2:
                            snprintf(buf, buflen, tapHistoryCount > 0 ? " [Tap] %.0fbpm" : " [Tap]", MasterBPM());
                            break;
                        case 6:
                            snprintf(buf, buflen, " [Reset]");
                            break;
                    }
                    break;
                case SP_T:
                    switch (row) {
                        case 0:
                            snprintf(buf, buflen, " Model:%s", tModelNames[DspProxy::GetTModel()]);
                            break;
                        case 1:
                            snprintf(buf, buflen, " Clock Range:%s", rangeNames[DspProxy::GetTRange()]);
                            break;
                        case 2:
                            snprintf(buf, buflen, " Gate Length:%d%%", DspProxy::GetTPwMean() * 100 / 255);
                            break;
                        case 3:
                            snprintf(buf, buflen, " Gate Len Var:%d%%", DspProxy::GetTPwStd() * 100 / 255);
                            break;
                        case 4:
                            snprintf(buf, buflen, " Deja Vu:%s", dvNames[DspProxy::GetTDejaVu()]);
                            break;
                        case 5:
                        case 6:
                        case 7: {
                            int s = DspProxy::GetTSlot(row - 5);
                            snprintf(buf, buflen, " T%d out:%s", row - 4, TrigSlotName(s));
                            break;
                        }
                    }
                    break;
                case SP_X:
                    switch (row) {
                        case 0:
                            snprintf(buf, buflen, " Scale:%s", scaleNames[DspProxy::GetXScale()]);
                            break;
                        case 1:
                            snprintf(buf, buflen, " Voltage Range:%s", cvRangeNames[DspProxy::GetXRange()]);
                            break;
                        case 2:
                            snprintf(buf, buflen, " Diversity:%s", ctlNames[DspProxy::GetXControl()]);
                            break;
                        case 3:
                            snprintf(buf, buflen, " Deja Vu:%s", dvNames[DspProxy::GetXDejaVu()]);
                            break;
                        case 4:
                        case 5:
                        case 6: {
                            int s = DspProxy::GetXSlot(row - 4);
                            if (s < 0) snprintf(buf, buflen, " X%d out: none", row - 5);
                            else snprintf(buf, buflen, " X%d out:%s", row - 5, cvSlotDisplayNames[s]);
                            break;
                        }
                    }
                    break;
                case SP_Y:
                    switch (row) {
                        case 0:
                            snprintf(buf, buflen, " Voltage Range:%s", cvRangeNames[DspProxy::GetYRange()]);
                            break;
                        case 1:
                            snprintf(buf, buflen, " Y Spread:%.2f", DspProxy::GetYSpread());
                            break;
                        case 2:
                            snprintf(buf, buflen, " Y Bias:%.2f", DspProxy::GetYBias());
                            break;
                        case 3:
                            snprintf(buf, buflen, " Y Steps:%.2f", DspProxy::GetYSteps());
                            break;
                        case 4: {
                            int s = DspProxy::GetYSlot();
                            if (s < 0) snprintf(buf, buflen, " Y out: none");
                            else snprintf(buf, buflen, " Y out:%s", cvSlotDisplayNames[s]);
                            break;
                        }
                    }
                    break;
                case SP_CC_EDIT:
                    if (row == 0) {
                        int cc = DspProxy::GetDynamicSlotCC(ccEditSlot);
                        if (cc < 0) snprintf(buf, buflen, " CC: --");
                        else snprintf(buf, buflen, " CC: %d", cc);
                    } else if (row == 1) {
                        snprintf(buf, buflen, " Channel: %d", DspProxy::GetDynamicSlotChan(ccEditSlot));
                    } else {
                        snprintf(buf, buflen, " [%s]", DspProxy::IsLearning() ? "StopLearn" : "Learn");
                    }
                    break;
                default:
                    buf[0] = 0;
                    break;
            }
        }

        // ---- event handling ----

        void UIMenuPageMod::onEncoder(int delta) {
            cursor += delta;
            if (cursor < 0) cursor = 0;
            if (cursor > rowCount() - 1) cursor = rowCount() - 1;
            doRedraw();
        }

        // Right encoder: live value edit for the row under the cursor (no
        // edit mode, no confirm). SP_MAIN/SP_CC_SLOTS are pure navigation —
        // adjustRow would only flip dirty and trigger a pointless save.
        void UIMenuPageMod::onValueEncoder(int delta) {
            if (subPage == SP_MAIN || subPage == SP_CC_SLOTS) return;
            if (!rowIsAction(cursor)) {
                adjustRow(cursor, delta);
                doRedraw();
            }
        }

        void UIMenuPageMod::onButton(int btnId, bool longPress) {
            if (btnId != 2 || longPress) {
                doRedraw();
                return;
            }
            if (subPage == SP_MAIN) {
                SubPage target = SP_MAIN;
                switch (cursor) {
                    case 0: target = SP_PANEL; break;
                    case 1: target = SP_CLOCK; break;
                    case 2: target = SP_T; break;
                    case 3: target = SP_X; break;
                    case 4: target = SP_Y; break;
                    case 5: target = SP_CC_SLOTS; break;
                }
                if (target != SP_MAIN) {
                    subPage = target;
                    cursor = 0;
                }
            } else if (subPage == SP_CC_SLOTS) {
                ccEditSlot = cursor;
                subPage = SP_CC_EDIT;
                cursor = 0;
            } else if (rowIsAction(cursor)) {
                activateRow(cursor);
            }
            doRedraw();
        }

        bool UIMenuPageMod::onBack() {
            switch (subPage) {
                case SP_MAIN:
                    return false;
                case SP_CC_EDIT:
                    subPage = SP_CC_SLOTS;
                    cursor = ccEditSlot;
                    doRedraw();
                    return true;
                case SP_PANEL: cursor = 0; break;
                case SP_CLOCK: cursor = 1; break;
                case SP_T: cursor = 2; break;
                case SP_X: cursor = 3; break;
                case SP_Y: cursor = 4; break;
                case SP_CC_SLOTS: cursor = 5; break;
                default: cursor = 0; break;
            }
            subPage = SP_MAIN;
            doRedraw();
            return true;
        }

        // ---- rendering ----

        void UIMenuPageMod::doRedraw() {
            switch (subPage) {
                case SP_MAIN: redrawMain(); break;
                case SP_PANEL: redrawRows("Knobs"); break;
                case SP_CLOCK: redrawRows("Clock"); break;
                case SP_T: redrawRows("T Setup"); break;
                case SP_X: redrawRows("X Setup"); break;
                case SP_Y: redrawRows("Y Setup"); break;
                case SP_CC_SLOTS: redrawCCSlots(); break;
                case SP_CC_EDIT: redrawRows(nullptr); break;
                default: break;
            }
        }

        void UIMenuPageMod::redrawMain() {
            Display::Clear();
            char buf[32];
            const int TOTAL = 6;
            for (int i = 0; i < TOTAL; i++) {
                int y = ROW(i);
                switch (i) {
                    case 0:
                        snprintf(buf, sizeof(buf), " Knobs");
                        break;
                    case 1:
                        if (DspProxy::GetTExternal()) {
                            snprintf(buf, sizeof(buf), " Clk:MIDI");
                        } else {
                            snprintf(buf, sizeof(buf), " Clock:Int %.0fbpm", MasterBPM());
                        }
                        break;
                    case 2:
                        snprintf(buf, sizeof(buf), " T Setup:%s", tModelNames[DspProxy::GetTModel()]);
                        break;
                    case 3:
                        snprintf(buf, sizeof(buf), " X Setup:%s", scaleNames[DspProxy::GetXScale()]);
                        break;
                    case 4:
                        snprintf(buf, sizeof(buf), " Y Setup:%s", yDivNames[DspProxy::GetYDivider()]);
                        break;
                    default:
                        snprintf(buf, sizeof(buf), " CC Slots");
                        break;
                }
                Display::DrawString(0, y, buf, FONT_UI);
            }
            int cy = ROW(cursor);
            Display::InvertRect(0, cy - 1, 128, 8);
            drawOutputLine();
            Display::Flush();
        }

        // Generic scrolling parameter list used by all Marbles subpages
        // (and CC Edit). nullptr title renders the CC-slot header.
        void UIMenuPageMod::redrawRows(const char *title) {
            Display::Clear();
            char buf[32];
            if (title != nullptr) {
                Display::DrawString(0, ROW(0), title, FONT_UI);
    } else {
        snprintf(buf, sizeof(buf), "CC Slot %d", ccEditSlot + 1);
        Display::DrawString(0, ROW(0), buf, FONT_UI);
    }

            const int TOTAL = rowCount();
            const int VIS = VISIBLE_ITEMS_HDR - 1;  // bottom row = output strip
            int scrollOff = (cursor > VIS - 1) ? cursor - (VIS - 1) : 0;
            int visible = TOTAL - scrollOff;
            if (visible > VIS) visible = VIS;
            for (int i = 0; i < visible; i++) {
                int idx = scrollOff + i;
                int y = ROW_HDR(i);
                formatRow(idx, buf, sizeof(buf));
                Display::DrawString(0, y, buf, FONT_UI);
            }
            // cursor row is always the edited row: full-row invert, as on
            // the Params page
            int cy = ROW_HDR(cursor - scrollOff);
            Display::InvertRect(0, cy - 1, 128, 8);
            if (TOTAL > VIS) {
                Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0_HDR, VIS * LINE_H, TOTAL, cursor);
            }
            drawOutputLine();
            Display::Flush();
        }

        void UIMenuPageMod::redrawCCSlots() {
            Display::Clear();
            Display::DrawString(0, ROW(0), "CC Slots", FONT_UI);
            constexpr int TOTAL = 8;
            const int VIS = VISIBLE_ITEMS_HDR - 1;  // bottom row = output strip
            int scrollOff = (cursor > VIS - 1) ? cursor - (VIS - 1) : 0;
            int visible = TOTAL - scrollOff;
            if (visible > VIS) visible = VIS;
            for (int i = 0; i < visible; i++) {
                int idx = scrollOff + i;
                int y = ROW_HDR(i);
                int cc = DspProxy::GetDynamicSlotCC(idx);
                int ch = DspProxy::GetDynamicSlotChan(idx);
                char buf[48];
                if (cc < 0) {
                    snprintf(buf, sizeof(buf), " CC%d: --", idx + 1);
                } else {
                    snprintf(buf, sizeof(buf), " CC%d: CC%d Ch%d", idx + 1, cc, ch);
                }
                Display::DrawString(0, y, buf, FONT_UI);
            }
            int cy = ROW_HDR(cursor - scrollOff);
            Display::InvertRect(0, cy - 1, 128, 8);
            if (TOTAL > VIS)
                Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0_HDR, VIS * LINE_H, TOTAL, cursor);
            drawOutputLine();
            Display::Flush();
        }
    }
}
