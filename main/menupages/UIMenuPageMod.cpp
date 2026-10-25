#include "UIMenuPageMod.hpp"
#include "Display.hpp"
#include "MarblesEngine.hpp"
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
            "Coins", "Clustr", "Drums", "Indep", "Divid", "3St", "Markov"
        };
        static const char* rangeNames[3] = {"0.25x", "1x", "4x"};
        static const char* cvRangeNames[3] = {"Narw", "Pos", "Full"};
        static const char* ctlNames[3] = {"Ident", "Bump", "Tilt"};
        static const char* dvNames[3] = {"Off", "On", "Lock"};
        static const char* xyClkNames[5] = {"T123", "T1", "T2", "T3", "MIDI"};
        static const char* yDivNames[12] = {
            "1/64", "1/48", "1/32", "1/24", "1/16", "1/12",
            "1/8", "1/6", "1/4", "1/3", "1/2", "1/1"
        };
        static const char* scaleNames[6] = {
            "Major", "Minor", "Penta", "Pelog", "Bhair", "Shri"
        };

        void UIMenuPageMod::init() {
            subPage = SP_MAIN;
            cursor = 0;
            ccEditSlot = 0;
            editing = false;
            lastTapTick = 0;
            tapHistoryCount = 0;
        }

        void UIMenuPageMod::deinit() {
            if (dirty) { MarblesEngine::SaveConfig(); dirty = false; }
        }

        // ---- row model: one flat list per subpage ----

        int UIMenuPageMod::rowCount() const {
            switch (subPage) {
                case SP_MAIN: return 5;
                case SP_CLOCK: return 7;   // TClk, BPM, Rate, Range, XClk, [Tap], [Reset]
                case SP_T: return 11;
                case SP_X: return 12;
                case SP_Y: return 6;
                case SP_CC_SLOTS: return 8;
                case SP_CC_EDIT: return 3;
                default: return 1;
            }
        }

        bool UIMenuPageMod::rowIsAction(int row) const {
            if (subPage == SP_CLOCK && (row == 5 || row == 6)) return true;  // [Tap], [Reset]
            if (subPage == SP_CC_EDIT && row == 2) return true; // [Learn]
            return false;
        }

        void UIMenuPageMod::activateRow(int row) {
            if (subPage == SP_CLOCK && row == 5) {
                onTapTempo();
            } else if (subPage == SP_CLOCK && row == 6) {
                MarblesEngine::RequestReset();
            } else if (subPage == SP_CC_EDIT && row == 2) {
                if (MarblesEngine::IsLearning()) {
                    MarblesEngine::StopLearn();
                } else {
                    MarblesEngine::StartLearn();
                }
            }
        }

        // ---- internal clock helpers (BPM <-> marbles semitone rate) ----
        // One t-clock cycle = one beat, so BPM = Hz * 60. BPM entry is
        // clamped to 20..480 (musical range, reachable in all three ranges);
        // the fine Rate row keeps the full +-60 semitone span.
        static float MasterBaseHz() {
            float base = 2.0f;
            if (MarblesEngine::GetTRange() == 0) base = 0.5f;
            else if (MarblesEngine::GetTRange() == 2) base = 8.0f;
            return base;
        }

        static float MasterHz() {
            return MasterBaseHz() * powf(2.0f, MarblesEngine::GetTRate() / 12.0f);
        }

        static float MasterBPM() {
            return MasterHz() * 60.0f;
        }

        static void SetMasterBPM(float bpm) {
            if (bpm < 20.0f) bpm = 20.0f;
            if (bpm > 480.0f) bpm = 480.0f;
            float st = 12.0f * log2f((bpm / 60.0f) / MasterBaseHz());
            MarblesEngine::SetTRate(st);  // engine clamps to +-60 st
        }

        // ---- tap tempo: average the last 4 tap intervals (ms -> BPM) ----
        void UIMenuPageMod::onTapTempo() {
            uint32_t now = xTaskGetTickCount();
            uint32_t stale = pdMS_TO_TICKS(3000);
            if (tapHistoryCount > 0 && (now - lastTapTick) > stale) {
                tapHistoryCount = 0;  // previous sequence expired, start over
            }
            if (tapHistoryCount > 0) {
                uint32_t interval = now - lastTapTick;
                uint32_t minInterval = pdMS_TO_TICKS(125);   // 480 BPM
                if (interval < minInterval) {
                    lastTapTick = now;
                    return;  // too fast to be a deliberate tap
                }
                for (int i = 3; i > 0; i--) tapIntervalHistory[i] = tapIntervalHistory[i - 1];
                tapIntervalHistory[0] = interval;
                if (tapHistoryCount < 4) tapHistoryCount++;
                uint32_t sum = 0;
                for (int i = 0; i < tapHistoryCount; i++) sum += tapIntervalHistory[i];
                float avgMs = (float)sum * portTICK_PERIOD_MS / tapHistoryCount;
                SetMasterBPM(60000.0f / avgMs);
                dirty = true;
            } else {
                tapHistoryCount = 0;  // first tap just marks time
            }
            lastTapTick = now;
        }

        void UIMenuPageMod::adjustRow(int row, int delta) {
            switch (subPage) {
                case SP_CLOCK:
                    switch (row) {
                        case 0: MarblesEngine::SetTExternal(!MarblesEngine::GetTExternal()); break;
                        case 1: SetMasterBPM(MasterBPM() + delta); break;
                        case 2: MarblesEngine::SetTRate(MarblesEngine::GetTRate() + delta); break;
                        case 3: MarblesEngine::SetTRange(MarblesEngine::GetTRange() + delta); break;
                        case 4: MarblesEngine::SetXYClockSource(MarblesEngine::GetXYClockSource() + delta); break;
                    }
                    break;
                case SP_T:
                    switch (row) {
                        case 0: MarblesEngine::SetTModel(MarblesEngine::GetTModel() + delta); break;
                        case 1: MarblesEngine::SetTBias(MarblesEngine::GetTBias() + delta * 0.01f); break;
                        case 2: MarblesEngine::SetTJitter(MarblesEngine::GetTJitter() + delta * 0.01f); break;
                        case 3: MarblesEngine::SetTPwMean(MarblesEngine::GetTPwMean() + delta * 5); break;
                        case 4: MarblesEngine::SetTPwStd(MarblesEngine::GetTPwStd() + delta * 5); break;
                        case 5: MarblesEngine::SetTDejaVu(MarblesEngine::GetTDejaVu() + delta); break;
                        case 6: MarblesEngine::SetTSlot(0, MarblesEngine::GetTSlot(0) + delta); break;
                        case 7: MarblesEngine::SetTSlot(1, MarblesEngine::GetTSlot(1) + delta); break;
                        case 8: MarblesEngine::SetTSlot(2, MarblesEngine::GetTSlot(2) + delta); break;
                        case 9: MarblesEngine::SetDejaVuLength(MarblesEngine::GetDejaVuLength() + delta); break;
                        case 10: MarblesEngine::SetDejaVuAmount(MarblesEngine::GetDejaVuAmount() + delta * 0.01f); break;
                    }
                    break;
                case SP_X:
                    switch (row) {
                        case 0: MarblesEngine::SetXScale(MarblesEngine::GetXScale() + delta); break;
                        case 1: MarblesEngine::SetXRange(MarblesEngine::GetXRange() + delta); break;
                        case 2: MarblesEngine::SetXControl(MarblesEngine::GetXControl() + delta); break;
                        case 3: MarblesEngine::SetXSpread(MarblesEngine::GetXSpread() + delta * 0.01f); break;
                        case 4: MarblesEngine::SetXBias(MarblesEngine::GetXBias() + delta * 0.01f); break;
                        case 5: MarblesEngine::SetXSteps(MarblesEngine::GetXSteps() + delta * 0.01f); break;
                        case 6: MarblesEngine::SetXRegister(!MarblesEngine::GetXRegister()); break;
                        case 7: MarblesEngine::SetXRegisterValue(MarblesEngine::GetXRegisterValue() + delta * 0.01f); break;
                        case 8: MarblesEngine::SetXDejaVu(MarblesEngine::GetXDejaVu() + delta); break;
                        case 9: MarblesEngine::SetXSlot(0, MarblesEngine::GetXSlot(0) + delta); break;
                        case 10: MarblesEngine::SetXSlot(1, MarblesEngine::GetXSlot(1) + delta); break;
                        case 11: MarblesEngine::SetXSlot(2, MarblesEngine::GetXSlot(2) + delta); break;
                    }
                    break;
                case SP_Y:
                    switch (row) {
                        case 0: MarblesEngine::SetYDivider(MarblesEngine::GetYDivider() + delta); break;
                        case 1: MarblesEngine::SetYRange(MarblesEngine::GetYRange() + delta); break;
                        case 2: MarblesEngine::SetYSpread(MarblesEngine::GetYSpread() + delta * 0.01f); break;
                        case 3: MarblesEngine::SetYBias(MarblesEngine::GetYBias() + delta * 0.01f); break;
                        case 4: MarblesEngine::SetYSteps(MarblesEngine::GetYSteps() + delta * 0.01f); break;
                        case 5: MarblesEngine::SetYSlot(MarblesEngine::GetYSlot() + delta); break;
                    }
                    break;
                case SP_CC_EDIT:
                    if (row == 0) {
                        MarblesEngine::SetDynamicSlotCC(ccEditSlot, MarblesEngine::GetDynamicSlotCC(ccEditSlot) + delta);
                    } else if (row == 1) {
                        MarblesEngine::SetDynamicSlotChan(ccEditSlot, MarblesEngine::GetDynamicSlotChan(ccEditSlot) + delta);
                    }
                    break;
                default:
                    break;
            }
            dirty = true;
        }

        void UIMenuPageMod::formatRow(int row, char *buf, int buflen) {
            switch (subPage) {
                case SP_CLOCK:
                    switch (row) {
                        case 0:
                            snprintf(buf, buflen, " TClk:%s", MarblesEngine::GetTExternal() ? "MIDI" : "Int");
                            break;
                        case 1:
                            snprintf(buf, buflen, " BPM:%.1f", MasterBPM());
                            break;
                        case 2:
                            snprintf(buf, buflen, " Rate:%.2fHz", MasterHz());
                            break;
                        case 3:
                            snprintf(buf, buflen, " Range:%s", rangeNames[MarblesEngine::GetTRange()]);
                            break;
                        case 4:
                            snprintf(buf, buflen, " XClk:%s", xyClkNames[MarblesEngine::GetXYClockSource()]);
                            break;
                        case 5:
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
                            snprintf(buf, buflen, " Mode:%s", tModelNames[MarblesEngine::GetTModel()]);
                            break;
                        case 1:
                            snprintf(buf, buflen, " Bias:%.2f", MarblesEngine::GetTBias());
                            break;
                        case 2:
                            snprintf(buf, buflen, " Jit:%.2f", MarblesEngine::GetTJitter());
                            break;
                        case 3:
                            snprintf(buf, buflen, " PW:%d%%", MarblesEngine::GetTPwMean() * 100 / 255);
                            break;
                        case 4:
                            snprintf(buf, buflen, " PWv:%d%%", MarblesEngine::GetTPwStd() * 100 / 255);
                            break;
                        case 5:
                            snprintf(buf, buflen, " DV:%s", dvNames[MarblesEngine::GetTDejaVu()]);
                            break;
                        case 6:
                        case 7:
                        case 8: {
                            int s = MarblesEngine::GetTSlot(row - 6);
                            snprintf(buf, buflen, " T%d>%s", row - 5, TrigSlotName(s));
                            break;
                        }
                        case 9:
                            snprintf(buf, buflen, " DVLn:%d", MarblesEngine::GetDejaVuLength());
                            break;
                        case 10:
                            snprintf(buf, buflen, " DVAmt:%.2f", MarblesEngine::GetDejaVuAmount());
                            break;
                    }
                    break;
                case SP_X:
                    switch (row) {
                        case 0:
                            snprintf(buf, buflen, " Scl:%s", scaleNames[MarblesEngine::GetXScale()]);
                            break;
                        case 1:
                            snprintf(buf, buflen, " Rng:%s", cvRangeNames[MarblesEngine::GetXRange()]);
                            break;
                        case 2:
                            snprintf(buf, buflen, " Ctl:%s", ctlNames[MarblesEngine::GetXControl()]);
                            break;
                        case 3:
                            snprintf(buf, buflen, " Sprd:%.2f", MarblesEngine::GetXSpread());
                            break;
                        case 4:
                            snprintf(buf, buflen, " Bias:%.2f", MarblesEngine::GetXBias());
                            break;
                        case 5:
                            snprintf(buf, buflen, " Step:%.2f", MarblesEngine::GetXSteps());
                            break;
                        case 6:
                            snprintf(buf, buflen, " Reg:%s", MarblesEngine::GetXRegister() ? "On" : "Off");
                            break;
                        case 7:
                            snprintf(buf, buflen, " RVal:%.2f", MarblesEngine::GetXRegisterValue());
                            break;
                        case 8:
                            snprintf(buf, buflen, " DV:%s", dvNames[MarblesEngine::GetXDejaVu()]);
                            break;
                        case 9:
                        case 10:
                        case 11: {
                            int s = MarblesEngine::GetXSlot(row - 9);
                            if (s < 0) snprintf(buf, buflen, " X%d>-", row - 8);
                            else snprintf(buf, buflen, " X%d>%s", row - 8, cvSlotDisplayNames[s]);
                            break;
                        }
                    }
                    break;
                case SP_Y:
                    switch (row) {
                        case 0:
                            snprintf(buf, buflen, " Rate:%s", yDivNames[MarblesEngine::GetYDivider()]);
                            break;
                        case 1:
                            snprintf(buf, buflen, " Rng:%s", cvRangeNames[MarblesEngine::GetYRange()]);
                            break;
                        case 2:
                            snprintf(buf, buflen, " Sprd:%.2f", MarblesEngine::GetYSpread());
                            break;
                        case 3:
                            snprintf(buf, buflen, " Bias:%.2f", MarblesEngine::GetYBias());
                            break;
                        case 4:
                            snprintf(buf, buflen, " Step:%.2f", MarblesEngine::GetYSteps());
                            break;
                        case 5: {
                            int s = MarblesEngine::GetYSlot();
                            if (s < 0) snprintf(buf, buflen, " Y>-");
                            else snprintf(buf, buflen, " Y>%s", cvSlotDisplayNames[s]);
                            break;
                        }
                    }
                    break;
                case SP_CC_EDIT:
                    if (row == 0) {
                        int cc = MarblesEngine::GetDynamicSlotCC(ccEditSlot);
                        if (cc < 0) snprintf(buf, buflen, " CC: --");
                        else snprintf(buf, buflen, " CC: %d", cc);
                    } else if (row == 1) {
                        snprintf(buf, buflen, " Chan: %d", MarblesEngine::GetDynamicSlotChan(ccEditSlot));
                    } else {
                        snprintf(buf, buflen, " [%s]", MarblesEngine::IsLearning() ? "StopLearn" : "Learn");
                    }
                    break;
                default:
                    buf[0] = 0;
                    break;
            }
        }

        // ---- event handling ----

        void UIMenuPageMod::onEncoder(int delta) {
            if (subPage == SP_MAIN) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 4) cursor = 4;
            } else if (editing) {
                adjustRow(cursor, delta);
            } else {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > rowCount() - 1) cursor = rowCount() - 1;
            }
            doRedraw();
        }

        void UIMenuPageMod::onButton(int btnId, bool longPress) {
            if (btnId != 2 || longPress) {
                doRedraw();
                return;
            }
            if (subPage == SP_MAIN) {
                SubPage target = SP_MAIN;
                int focus = 0;
                switch (cursor) {
                    case 0: target = SP_CLOCK; break;
                    case 1: target = SP_T; focus = 1; break;
                    case 2: target = SP_X; focus = 2; break;
                    case 3: target = SP_Y; focus = 3; break;
                    case 4: target = SP_CC_SLOTS; break;
                }
                if (target != SP_MAIN) {
                    subPage = target;
                    cursor = 0;
                    editing = false;
                }
            } else if (subPage == SP_CC_SLOTS) {
                ccEditSlot = cursor;
                subPage = SP_CC_EDIT;
                cursor = 0;
                editing = false;
            } else if (rowIsAction(cursor)) {
                activateRow(cursor);
            } else {
                editing = !editing;
            }
            doRedraw();
        }

        bool UIMenuPageMod::onBack() {
            if (editing) {
                editing = false;
                doRedraw();
                return true;
            }
            switch (subPage) {
                case SP_MAIN:
                    return false;
                case SP_CC_EDIT:
                    subPage = SP_CC_SLOTS;
                    cursor = ccEditSlot;
                    doRedraw();
                    return true;
                case SP_CLOCK: cursor = 0; break;
                case SP_T: cursor = 1; break;
                case SP_X: cursor = 2; break;
                case SP_Y: cursor = 3; break;
                case SP_CC_SLOTS: cursor = 4; break;
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
                case SP_CLOCK: redrawRows("Clock"); break;
                case SP_T: redrawRows("T Section"); break;
                case SP_X: redrawRows("X Section"); break;
                case SP_Y: redrawRows("Y Section"); break;
                case SP_CC_SLOTS: redrawCCSlots(); break;
                case SP_CC_EDIT: redrawRows(nullptr); break;
                default: break;
            }
        }

        void UIMenuPageMod::redrawMain() {
            Display::Clear();
            char buf[32];
            const int TOTAL = 5;
            for (int i = 0; i < TOTAL; i++) {
                int y = ROW(i);
                switch (i) {
                    case 0:
                        if (MarblesEngine::GetTExternal()) {
                            snprintf(buf, sizeof(buf), " Clk:MIDI");
                        } else {
                            snprintf(buf, sizeof(buf), " Clk:Int %.0fbpm", MasterBPM());
                        }
                        break;
                    case 1:
                        snprintf(buf, sizeof(buf), " T:%s", tModelNames[MarblesEngine::GetTModel()]);
                        break;
                    case 2:
                        snprintf(buf, sizeof(buf), " X:%s", scaleNames[MarblesEngine::GetXScale()]);
                        break;
                    case 3:
                        snprintf(buf, sizeof(buf), " Y:%s", yDivNames[MarblesEngine::GetYDivider()]);
                        break;
                    default:
                        snprintf(buf, sizeof(buf), " CC Slots");
                        break;
                }
                Display::DrawString(0, y, buf, Display::FONT_5X7);
            }
            int cy = ROW(cursor);
            Display::InvertRect(0, cy, 128, 8);
            Display::Flush();
        }

        // Generic scrolling parameter list used by all Marbles subpages
        // (and CC Edit). nullptr title renders the CC-slot header.
        void UIMenuPageMod::redrawRows(const char *title) {
            Display::Clear();
            char buf[32];
            if (title != nullptr) {
                Display::DrawString(0, ROW(0), title, Display::FONT_5X7);
    } else {
        snprintf(buf, sizeof(buf), "CC Slot %d", ccEditSlot + 1);
        Display::DrawString(0, ROW(0), buf, Display::FONT_5X7);
    }

            const int TOTAL = rowCount();
            int scrollOff = (cursor > VISIBLE_ITEMS_HDR - 1) ? cursor - (VISIBLE_ITEMS_HDR - 1) : 0;
            int visible = TOTAL - scrollOff;
            if (visible > VISIBLE_ITEMS_HDR) visible = VISIBLE_ITEMS_HDR;
            for (int i = 0; i < visible; i++) {
                int idx = scrollOff + i;
                int y = ROW_HDR(i);
                formatRow(idx, buf, sizeof(buf));
                Display::DrawString(0, y, buf, Display::FONT_5X7);
                if (editing && cursor == idx) {
                    // invert the value region (after " Lll:")
                    int vstart = 1;
                    while (buf[vstart] && buf[vstart] != ':') vstart++;
                    vstart++;
                    int x = vstart * 6 + 1;
                    if (x < 127) Display::InvertRect(x, y, 128 - x, 8);
                }
            }
            if (!editing) {
                int cy = ROW_HDR(cursor - scrollOff);
                Display::InvertRect(0, cy, 128, 8);
            }
            if (TOTAL > VISIBLE_ITEMS_HDR) {
                Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0_HDR, VISIBLE_ITEMS_HDR * LINE_H, TOTAL, cursor);
            }
            Display::Flush();
        }

        void UIMenuPageMod::redrawCCSlots() {
            Display::Clear();
            Display::DrawString(0, ROW(0), "CC Slots", Display::FONT_5X7);
            constexpr int TOTAL = 8;
            int scrollOff = (cursor > VISIBLE_ITEMS_HDR - 1) ? cursor - (VISIBLE_ITEMS_HDR - 1) : 0;
            int visible = TOTAL - scrollOff;
            if (visible > VISIBLE_ITEMS_HDR) visible = VISIBLE_ITEMS_HDR;
            for (int i = 0; i < visible; i++) {
                int idx = scrollOff + i;
                int y = ROW_HDR(i);
                int cc = MarblesEngine::GetDynamicSlotCC(idx);
                int ch = MarblesEngine::GetDynamicSlotChan(idx);
                char buf[48];
                if (cc < 0) {
                    snprintf(buf, sizeof(buf), " CC%d: --", idx + 1);
                } else {
                    snprintf(buf, sizeof(buf), " CC%d: CC%d Ch%d", idx + 1, cc, ch);
                }
                Display::DrawString(0, y, buf, Display::FONT_5X7);
            }
            int cy = ROW_HDR(cursor - scrollOff);
            Display::InvertRect(0, cy, 128, 8);
            if (TOTAL > VISIBLE_ITEMS_HDR)
                Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0_HDR, VISIBLE_ITEMS_HDR * LINE_H, TOTAL, cursor);
            Display::Flush();
        }
    }
}
