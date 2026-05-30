#include "UIMenuPageMod.hpp"
#include "Display.hpp"
#include "ModEngine.hpp"
#include "CVSlotNames.hpp"
#include <cstring>
#include <cstdio>

using namespace CTAG::DRIVERS;

namespace CTAG {
    namespace CTRL {

        void UIMenuPageMod::init() {
            subPage = SP_MAIN;
            cursor = 0;
            ccEditSlot = 0;
            ccEditValue = 0;
        }

        void UIMenuPageMod::deinit() {}

        void UIMenuPageMod::onEncoder(int delta) {
            if (subPage == SP_MAIN) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 2) cursor = 2;
            } else if (subPage == SP_LFO1 || subPage == SP_LFO2) {
                int lfo = (subPage == SP_LFO1) ? 0 : 1;
                if (cursor == 0) {
                    int s = ModEngine::GetLFOShape(lfo) + delta;
                    if (s < 0) s = 0;
                    if (s > 4) s = 4;
                    ModEngine::SetLFOShape(lfo, s);
                    ModEngine::SaveConfig();
                } else if (cursor == 1) {
                    float r = ModEngine::GetLFORate(lfo) + delta * 0.1f;
                    if (r < 0.01f) r = 0.01f;
                    if (r > 100.0f) r = 100.0f;
                    ModEngine::SetLFORate(lfo, r);
                    ModEngine::SaveConfig();
                } else if (cursor == 2) {
                    float a = ModEngine::GetLFOAmplitude(lfo) + delta * 0.01f;
                    if (a < 0.0f) a = 0.0f;
                    if (a > 1.0f) a = 1.0f;
                    ModEngine::SetLFOAmplitude(lfo, a);
                    ModEngine::SaveConfig();
                } else if (cursor == 3) {
                    int s = ModEngine::GetLFOCVSlot(lfo) + delta;
                    if (s < -1) s = -1;
                    if (s >= N_CVS) s = N_CVS - 1;
                    ModEngine::SetLFOCVSlot(lfo, s);
                    ModEngine::SaveConfig();
                }
            } else if (subPage == SP_CC_SLOTS) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 7) cursor = 7;
            } else if (subPage == SP_CC_EDIT) {
                if (cursor == 0) {
                    int cc = ModEngine::GetDynamicSlotCC(ccEditSlot) + delta;
                    if (cc < -1) cc = -1;
                    if (cc > 127) cc = 127;
                    ModEngine::SetDynamicSlotCC(ccEditSlot, cc);
                    ModEngine::SaveConfig();
                } else if (cursor == 1) {
                    int ch = ModEngine::GetDynamicSlotChan(ccEditSlot) + delta;
                    if (ch < 0) ch = 0;
                    if (ch > 15) ch = 15;
                    ModEngine::SetDynamicSlotChan(ccEditSlot, ch);
                    ModEngine::SaveConfig();
                } else if (cursor == 2) {
                    ModEngine::StartLearn();
                }
            }
            doRedraw();
        }

        void UIMenuPageMod::onButton(int btnId, bool longPress) {
            if (subPage == SP_MAIN) {
                if (btnId == 2 && !longPress) {
                    if (cursor == 0) { subPage = SP_LFO1; cursor = 0; }
                    else if (cursor == 1) { subPage = SP_LFO2; cursor = 0; }
                    else if (cursor == 2) { subPage = SP_CC_SLOTS; cursor = 0; }
                }
            } else if (subPage == SP_CC_SLOTS) {
                if (btnId == 2 && !longPress) {
                    ccEditSlot = cursor;
                    ccEditValue = 0;
                    subPage = SP_CC_EDIT;
                    cursor = 0;
                }
            } else if (subPage == SP_CC_EDIT) {
                if (btnId == 2 && !longPress) {
                    subPage = SP_CC_SLOTS;
                    cursor = ccEditSlot;
                } else if (btnId == 2 && longPress && cursor == 2) {
                    ModEngine::StopLearn();
                }
            }
            doRedraw();
        }

        bool UIMenuPageMod::onBack() {
            if (subPage == SP_LFO1 || subPage == SP_LFO2) {
                int prev = subPage;
                subPage = SP_MAIN;
                cursor = (prev == SP_LFO1) ? 0 : 1;
                doRedraw();
                return true;
            }
            if (subPage == SP_CC_SLOTS) {
                subPage = SP_MAIN;
                cursor = 2;
                doRedraw();
                return true;
            }
            if (subPage == SP_CC_EDIT) {
                subPage = SP_CC_SLOTS;
                cursor = ccEditSlot;
                doRedraw();
                return true;
            }
            return false;
        }

        void UIMenuPageMod::doRedraw() {
            switch (subPage) {
                case SP_MAIN: redrawMain(); break;
                case SP_LFO1: redrawLFO(0); break;
                case SP_LFO2: redrawLFO(1); break;
                case SP_CC_SLOTS: redrawCCSlots(); break;
                case SP_CC_EDIT: redrawCCEdit(); break;
            }
        }

        static const char* shapeNames[5] = {"Sine", "Tri", "Saw", "Sq", "S&H"};

        void UIMenuPageMod::redrawMain() {
            Display::Clear();
            char buf[32];
            for (int i = 0; i < 3; i++) {
                int y = 5 + i * 9;
                if (i == 0) {
                    float r = ModEngine::GetLFORate(0);
                    snprintf(buf, sizeof(buf), " LFO1: %s %.1fHz", shapeNames[ModEngine::GetLFOShape(0)], r);
                } else if (i == 1) {
                    float r = ModEngine::GetLFORate(1);
                    snprintf(buf, sizeof(buf), " LFO2: %s %.1fHz", shapeNames[ModEngine::GetLFOShape(1)], r);
                } else {
                    snprintf(buf, sizeof(buf), " CC Slots");
                }
                Display::DrawString(0, y, buf, Display::FONT_5X7);
            }
            int cy = 5 + cursor * 9;
            Display::InvertRect(0, cy, 128, 8);
            Display::Flush();
        }

        void UIMenuPageMod::redrawLFO(int lfo) {
            Display::Clear();
            char buf[32];
            const char* title = (lfo == 0) ? "LFO1" : "LFO2";
            snprintf(buf, sizeof(buf), "%s Config", title);
            Display::DrawString(0, 5, buf, Display::FONT_5X7);

            int s = ModEngine::GetLFOShape(lfo);
            snprintf(buf, sizeof(buf), " Shp: %s", shapeNames[s >= 0 && s < 5 ? s : 0]);
            Display::DrawString(0, 14, buf, Display::FONT_5X7);

            float r = ModEngine::GetLFORate(lfo);
            snprintf(buf, sizeof(buf), " Rate: %.1fHz", r);
            Display::DrawString(0, 23, buf, Display::FONT_5X7);

            float a = ModEngine::GetLFOAmplitude(lfo);
            snprintf(buf, sizeof(buf), " Amp: %.2f", a);
            Display::DrawString(0, 32, buf, Display::FONT_5X7);

            int cv = ModEngine::GetLFOCVSlot(lfo);
            if (cv < 0) {
                snprintf(buf, sizeof(buf), " Out: None");
            } else if (cv < 100) {
                snprintf(buf, sizeof(buf), " Out: %s", cvSlotDisplayNames[cv]);
            } else {
                snprintf(buf, sizeof(buf), " Out: %d", cv);
            }
            Display::DrawString(0, 41, buf, Display::FONT_5X7);

            int cy = 14 + cursor * 9;
            Display::InvertRect(0, cy, 128, 8);
            Display::Flush();
        }

        void UIMenuPageMod::redrawCCSlots() {
            Display::Clear();
            Display::DrawString(0, 5, "CC Slots", Display::FONT_5X7);
            int scrollOff = (cursor > 4) ? cursor - 4 : 0;
            int visible = 8 - scrollOff;
            if (visible > 5) visible = 5;
            for (int i = 0; i < visible; i++) {
                int idx = scrollOff + i;
                int y = 14 + i * 9;
                int cc = ModEngine::GetDynamicSlotCC(idx);
                int ch = ModEngine::GetDynamicSlotChan(idx);
                char buf[48];
                if (cc < 0) {
                    snprintf(buf, sizeof(buf), " %d: --", idx);
                } else {
                    snprintf(buf, sizeof(buf), " %d: CC %d (Ch%d)", idx, cc, ch);
                }
                Display::DrawString(0, y, buf, Display::FONT_5X7);
            }
            int cy = 14 + (cursor - scrollOff) * 9;
            Display::InvertRect(0, cy, 128, 8);
            if (8 > 5)
                Display::DrawScrollbar(126, 14, 45, 8, cursor);
            Display::Flush();
        }

        void UIMenuPageMod::redrawCCEdit() {
            Display::Clear();
            char buf[32];
            snprintf(buf, sizeof(buf), "Slot %d", ccEditSlot);
            Display::DrawString(0, 5, buf, Display::FONT_5X7);

            int cc = ModEngine::GetDynamicSlotCC(ccEditSlot);
            if (cc < 0) {
                snprintf(buf, sizeof(buf), " CC: --");
            } else {
                snprintf(buf, sizeof(buf), " CC: %d", cc);
            }
            Display::DrawString(0, 14, buf, Display::FONT_5X7);

            int ch = ModEngine::GetDynamicSlotChan(ccEditSlot);
            snprintf(buf, sizeof(buf), " Chan: %d", ch);
            Display::DrawString(0, 23, buf, Display::FONT_5X7);

            const char* learnStatus = ModEngine::IsLearning() ? "StopLearn" : "Learn";
            snprintf(buf, sizeof(buf), " [%s]", learnStatus);
            Display::DrawString(0, 32, buf, Display::FONT_5X7);

            int cy = 14 + cursor * 9;
            Display::InvertRect(0, cy, 128, 8);
            Display::Flush();
        }
    }
}
