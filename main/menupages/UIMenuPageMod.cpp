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
            seqEditParams = false;
            editing = false;
        }

        void UIMenuPageMod::deinit() {
            if (dirty) { ModEngine::SaveConfig(); dirty = false; }
        }

        void UIMenuPageMod::onEncoder(int delta) {
            if (subPage == SP_MAIN) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 7) cursor = 7;
            } else if (subPage == SP_TEMPO) {
                if (editing) {
                    if (cursor == 0) {
                        float bpm = ModEngine::GetTempoEngine().GetBPM() + delta * 0.5f;
                        if (bpm < 20.0f) bpm = 20.0f;
                        if (bpm > 300.0f) bpm = 300.0f;
                        ModEngine::GetTempoEngine().SetBPM(bpm);
                        dirty = true;
                    } else if (cursor == 1) {
                        auto src = ModEngine::GetTempoEngine().GetSource();
                        int s = (src == SP::HELPERS::ctagTempo::Source::INTERNAL) ? 0 : 1;
                        s += delta;
                        if (s < 0) s = 0;
                        if (s > 1) s = 1;
                        ModEngine::GetTempoEngine().SetSource(
                            s == 0 ? SP::HELPERS::ctagTempo::Source::INTERNAL
                                   : SP::HELPERS::ctagTempo::Source::MIDI_CLOCK);
                        dirty = true;
                    }
                } else {
                    cursor += delta;
                    if (cursor < 0) cursor = 0;
                    if (cursor > 2) cursor = 2;
                }
            } else if (subPage == SP_LFO1 || subPage == SP_LFO2) {
                int lfo = (subPage == SP_LFO1) ? 0 : 1;
                if (editing) {
                    if (cursor == 0) {
                        int s = ModEngine::GetLFOShape(lfo) + delta;
                        if (s < 0) s = 0;
                        if (s > 4) s = 4;
                        ModEngine::SetLFOShape(lfo, s);
                        dirty = true;
                    } else if (cursor == 1) {
                        float r = ModEngine::GetLFORate(lfo) + delta * 0.1f;
                        if (r < 0.01f) r = 0.01f;
                        if (r > 100.0f) r = 100.0f;
                        ModEngine::SetLFORate(lfo, r);
                        dirty = true;
                    } else if (cursor == 2) {
                        float a = ModEngine::GetLFOAmplitude(lfo) + delta * 0.01f;
                        if (a < 0.0f) a = 0.0f;
                        if (a > 1.0f) a = 1.0f;
                        ModEngine::SetLFOAmplitude(lfo, a);
                        dirty = true;
                    } else if (cursor == 3) {
                        int s = ModEngine::GetLFOCVSlot(lfo) + delta;
                        if (s < -1) s = -1;
                        if (s >= N_CVS) s = N_CVS - 1;
                        ModEngine::SetLFOCVSlot(lfo, s);
                        dirty = true;
                    }
                } else {
                    cursor += delta;
                    if (cursor < 0) cursor = 0;
                    if (cursor > 3) cursor = 3;
                }
            } else if (subPage == SP_CC_SLOTS) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 7) cursor = 7;
            } else if (subPage == SP_CC_EDIT) {
                if (editing) {
                    if (cursor == 0) {
                        int cc = ModEngine::GetDynamicSlotCC(ccEditSlot) + delta;
                        if (cc < -1) cc = -1;
                        if (cc > 127) cc = 127;
                        ModEngine::SetDynamicSlotCC(ccEditSlot, cc);
                        dirty = true;
                    } else if (cursor == 1) {
                        int ch = ModEngine::GetDynamicSlotChan(ccEditSlot) + delta;
                        if (ch < 0) ch = 0;
                        if (ch > 15) ch = 15;
                        ModEngine::SetDynamicSlotChan(ccEditSlot, ch);
                        dirty = true;
                    }
                } else {
                    cursor += delta;
                    if (cursor < 0) cursor = 0;
                    if (cursor > 2) cursor = 2;
                }
            } else if (subPage == SP_GATE1 || subPage == SP_GATE2) {
                int g = (subPage == SP_GATE1) ? 0 : 1;
                if (seqEditParams) {
                    if (editing) {
                        if (cursor == 0) {
                            float len = ModEngine::GetGate(g).GetStepLength() + delta * 0.25f;
                            if (len < 0.25f) len = 0.25f;
                            if (len > 4.0f) len = 4.0f;
                            ModEngine::GetGate(g).SetStepLength(len);
                            dirty = true;
                        } else if (cursor == 1) {
                            int d = (int)ModEngine::GetGate(g).GetDirection() + delta;
                            if (d < 0) d = 0;
                            if (d > 3) d = 3;
                            ModEngine::GetGate(g).SetDirection((SP::HELPERS::ctagGate16::Direction)d);
                            dirty = true;
                        } else if (cursor == 2) {
                            float sw = ModEngine::GetGate(g).GetSwing() + delta * 0.05f;
                            if (sw < 0.0f) sw = 0.0f;
                            if (sw > 1.0f) sw = 1.0f;
                            ModEngine::GetGate(g).SetSwing(sw);
                            dirty = true;
                        } else if (cursor == 3) {
                            float gl = ModEngine::GetGate(g).GetGateLength() + delta * 0.05f;
                            if (gl < 0.1f) gl = 0.1f;
                            if (gl > 1.0f) gl = 1.0f;
                            ModEngine::GetGate(g).SetGateLength(gl);
                            dirty = true;
                        } else if (cursor == 4) {
                            int t = ModEngine::GetGate(g).GetTrigSlot() + delta;
                            if (t < -1) t = -1;
                            if (t >= N_TRIGS) t = N_TRIGS - 1;
                            ModEngine::GetGate(g).SetTrigSlot(t);
                            dirty = true;
                        }
                    } else {
                        cursor += delta;
                        if (cursor < 0) cursor = 0;
                        if (cursor > 5) cursor = 5;
                    }
                } else {
                    if (editing && cursor <= 15) {
                        int p = ModEngine::GetGate(g).GetStepProbability(cursor) + delta * 5;
                        if (p < 0) p = 0;
                        if (p > 100) p = 100;
                        ModEngine::GetGate(g).SetStepProbability(cursor, (uint8_t)p);
                        dirty = true;
                    } else {
                        cursor += delta;
                        if (cursor < 0) cursor = 0;
                        if (cursor > 16) cursor = 16;
                    }
                }
            } else if (subPage == SP_SEQ1 || subPage == SP_SEQ2) {
                int seq = (subPage == SP_SEQ1) ? 0 : 1;
                if (seqEditParams) {
                    if (editing) {
                        if (cursor == 0) {
                            float len = ModEngine::GetSequencer(seq).GetStepLength() + delta * 0.25f;
                            if (len < 0.25f) len = 0.25f;
                            if (len > 4.0f) len = 4.0f;
                            ModEngine::GetSequencer(seq).SetStepLength(len);
                            dirty = true;
                        } else if (cursor == 1) {
                            int d = (int)ModEngine::GetSequencer(seq).GetDirection() + delta;
                            if (d < 0) d = 0;
                            if (d > 3) d = 3;
                            ModEngine::GetSequencer(seq).SetDirection((SP::HELPERS::ctagSeq16::Direction)d);
                            dirty = true;
                        } else if (cursor == 2) {
                            float s = ModEngine::GetSequencer(seq).GetSlew() + delta * 0.05f;
                            if (s < 0.0f) s = 0.0f;
                            if (s > 1.0f) s = 1.0f;
                            ModEngine::GetSequencer(seq).SetSlew(s);
                            dirty = true;
                        } else if (cursor == 3) {
                            int cv = ModEngine::GetSequencer(seq).GetCVSlot() + delta;
                            if (cv < -1) cv = -1;
                            if (cv >= N_CVS) cv = N_CVS - 1;
                            ModEngine::GetSequencer(seq).SetCVSlot(cv);
                            dirty = true;
                        } else if (cursor == 4) {
                            int t = ModEngine::GetSequencer(seq).GetTrigSlot() + delta;
                            if (t < -1) t = -1;
                            if (t >= N_TRIGS) t = N_TRIGS - 1;
                            ModEngine::GetSequencer(seq).SetTrigSlot(t);
                            dirty = true;
                        }
                    } else {
                        cursor += delta;
                        if (cursor < 0) cursor = 0;
                        if (cursor > 4) cursor = 4;
                    }
                } else {
                    // Step list: cursor 0..15 are steps, 16 is "[Params]".
                    // Editing a step adjusts its CV value, mirroring the gate
                    // step list. Without this the steps could be read but
                    // never changed from the UI.
                    if (editing && cursor <= 15) {
                        float v = ModEngine::GetSequencer(seq).GetStep(cursor) + delta * 0.05f;
                        if (v < 0.0f) v = 0.0f;
                        if (v > 1.0f) v = 1.0f;
                        ModEngine::GetSequencer(seq).SetStep(cursor, v);
                        dirty = true;
                    } else {
                        cursor += delta;
                        if (cursor < 0) cursor = 0;
                        if (cursor > 16) cursor = 16; // 16 steps + "[Params]"
                    }
                }
            }
            doRedraw();
        }

        void UIMenuPageMod::onButton(int btnId, bool longPress) {
            if (subPage == SP_MAIN) {
                if (btnId == 2 && !longPress) {
                    if (cursor == 0) { subPage = SP_LFO1; cursor = 0; editing = false; }
                    else if (cursor == 1) { subPage = SP_LFO2; cursor = 0; editing = false; }
                    else if (cursor == 2) { subPage = SP_TEMPO; cursor = 0; editing = false; }
                    else if (cursor == 3) { subPage = SP_CC_SLOTS; cursor = 0; editing = false; }
                    else if (cursor == 4) { subPage = SP_SEQ1; cursor = 0; editing = false; seqEditParams = false; }
                    else if (cursor == 5) { subPage = SP_SEQ2; cursor = 0; editing = false; seqEditParams = false; }
                    else if (cursor == 6) { subPage = SP_GATE1; cursor = 0; editing = false; seqEditParams = false; }
                    else if (cursor == 7) { subPage = SP_GATE2; cursor = 0; editing = false; seqEditParams = false; }
                }
            } else if (subPage == SP_TEMPO) {
                if (btnId == 2 && !longPress) {
                    if (cursor == 2) {
                        ModEngine::GetTempoEngine().OnTapTempo();
                    } else if (cursor == 3) {
                        // Rewind the internal transport to the downbeat. Always
                        // available, so a stopped tempo is recoverable from the UI.
                        ModEngine::GetTempoEngine().Reset();
                    } else {
                        editing = !editing;
                    }
                }
            } else if (subPage == SP_LFO1 || subPage == SP_LFO2) {
                if (btnId == 2 && !longPress) {
                    editing = !editing;
                }
            } else if (subPage == SP_CC_SLOTS) {
                if (btnId == 2 && !longPress) {
                    ccEditSlot = cursor;
                    ccEditValue = 0;
                    subPage = SP_CC_EDIT;
                    cursor = 0;
                    editing = false;
                }
            } else if (subPage == SP_CC_EDIT) {
                if (btnId == 2 && !longPress) {
                    if (cursor == 2) {
                        if (ModEngine::IsLearning()) {
                            ModEngine::StopLearn();
                        } else {
                            ModEngine::StartLearn();
                        }
                    } else {
                        editing = !editing;
                    }
                }
            } else if (subPage == SP_GATE1 || subPage == SP_GATE2) {
                int g = (subPage == SP_GATE1) ? 0 : 1;
                if (seqEditParams) {
                    if (btnId == 2 && !longPress) {
                        editing = !editing;
                    }
                } else {
                    if (btnId == 2 && !longPress) {
                        if (cursor == 16) {
                            seqEditParams = true;
                            cursor = 0;
                            editing = false;
                        } else if (cursor <= 15) {
                            // Toggle step enabled
                            bool en = ModEngine::GetGate(g).GetStepEnabled(cursor);
                            ModEngine::GetGate(g).SetStepEnabled(cursor, !en);
                            dirty = true;
                        }
                    } else if (btnId == 2 && longPress) {
                        // Long press: edit probability for this step
                        if (cursor <= 15) {
                            editing = !editing;
                        }
                    }
                }
            } else if (subPage == SP_SEQ1 || subPage == SP_SEQ2) {
                int seq = (subPage == SP_SEQ1) ? 0 : 1;
                if (seqEditParams) {
                    if (btnId == 2 && !longPress) {
                        editing = !editing;
                    }
                } else {
                    if (btnId == 2 && longPress) {
                        // Long press on a step toggles trigger
                        if (cursor <= 15) {
                            float curVal = ModEngine::GetSequencer(seq).GetStep(cursor);
                            ModEngine::GetSequencer(seq).SetStep(cursor, curVal);
                            // Toggle trig not implemented yet – just update step
                        }
                    } else if (btnId == 2 && !longPress) {
                        if (cursor == 16) {
                            // Enter params sub-screen
                            seqEditParams = true;
                            cursor = 0;
                            editing = false;
                        } else {
                            // Toggle edit mode for step CV
                            editing = !editing;
                        }
                    }
                }
            }
            doRedraw();
        }

        bool UIMenuPageMod::onBack() {
            if (editing) {
                editing = false;
                doRedraw();
                return true;
            }
            if (subPage == SP_TEMPO) {
                subPage = SP_MAIN;
                cursor = 2;
                doRedraw();
                return true;
            }
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
            if (subPage == SP_SEQ1 || subPage == SP_SEQ2 || subPage == SP_GATE1 || subPage == SP_GATE2) {
                if (seqEditParams) {
                    seqEditParams = false;
                    cursor = 16;
                    doRedraw();
                    return true;
                }
                int prevFocus;
                if (subPage == SP_SEQ1) prevFocus = 4;
                else if (subPage == SP_SEQ2) prevFocus = 5;
                else if (subPage == SP_GATE1) prevFocus = 6;
                else prevFocus = 7;
                subPage = SP_MAIN;
                cursor = prevFocus;
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
                case SP_TEMPO: redrawTempo(); break;
                case SP_CC_SLOTS: redrawCCSlots(); break;
                case SP_CC_EDIT: redrawCCEdit(); break;
                case SP_SEQ1: redrawSeq(0); break;
                case SP_SEQ2: redrawSeq(1); break;
                case SP_GATE1: redrawGate(0); break;
                case SP_GATE2: redrawGate(1); break;
            }
        }

        static const char* shapeNames[5] = {"Sine", "Tri", "Saw", "Sq", "S&H"};

        void UIMenuPageMod::redrawMain() {
            Display::Clear();
            char buf[32];
            // 8 items at LINE_H=8 starting at ITEM_Y0=5 would run off the
            // 64-row display (row 7 starts at y=61). Scroll so only rows that
            // fit are drawn, matching the other list subpages.
            constexpr int TOTAL = 8;
            int scrollOff = 0;
            if (cursor >= VISIBLE_ITEMS_HDR) {
                scrollOff = cursor - VISIBLE_ITEMS_HDR + 1;
            }

            for (int i = 0; i < TOTAL; i++) {
                if (i < scrollOff || i >= scrollOff + VISIBLE_ITEMS_HDR) continue;
                int y = ROW(i - scrollOff);
                if (i == 0) {
                    float r = ModEngine::GetLFORate(0);
                    snprintf(buf, sizeof(buf), " LFO1: %s %.1fHz", shapeNames[ModEngine::GetLFOShape(0)], r);
                } else if (i == 1) {
                    float r = ModEngine::GetLFORate(1);
                    snprintf(buf, sizeof(buf), " LFO2: %s %.1fHz", shapeNames[ModEngine::GetLFOShape(1)], r);
                } else if (i == 2) {
                    float bpm = ModEngine::GetTempoEngine().GetBPM();
                    auto src = ModEngine::GetTempoEngine().GetSource();
                    const char* srcStr = (src == SP::HELPERS::ctagTempo::Source::INTERNAL) ? "Int" : "MIDI";
                    snprintf(buf, sizeof(buf), " Tempo:%.0f %s", bpm, srcStr);
                } else if (i == 3) {
                    snprintf(buf, sizeof(buf), " CC Slots");
                } else if (i == 4) {
                    snprintf(buf, sizeof(buf), " Seq1");
                } else if (i == 5) {
                    snprintf(buf, sizeof(buf), " Seq2");
                } else if (i == 6) {
                    snprintf(buf, sizeof(buf), " Gate1");
                } else {
                    snprintf(buf, sizeof(buf), " Gate2");
                }
                Display::DrawString(0, y, buf, Display::FONT_5X7);
            }
            int cy = ROW(cursor - scrollOff);
            Display::InvertRect(0, cy, 128, 8);
            Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0, VISIBLE_ITEMS_HDR * LINE_H, TOTAL, cursor);
            Display::Flush();
        }

        void UIMenuPageMod::redrawLFO(int lfo) {
            Display::Clear();
            char buf[32];
            const char* title = (lfo == 0) ? "LFO1" : "LFO2";
            snprintf(buf, sizeof(buf), "%s Config", title);
            Display::DrawString(0, ROW(0), buf, Display::FONT_5X7);

            int s = ModEngine::GetLFOShape(lfo);
            snprintf(buf, sizeof(buf), " Shp:%s", shapeNames[s >= 0 && s < 5 ? s : 0]);
            Display::DrawString(0, ROW_HDR(0), buf, Display::FONT_5X7);
            if (editing && cursor == 0) {
                int x = 6 * 5 + 1;
                Display::InvertRect(x, ROW_HDR(0), 128 - x, 8);
            }

            float r = ModEngine::GetLFORate(lfo);
            snprintf(buf, sizeof(buf), " Rate:%.1fHz", r);
            Display::DrawString(0, ROW_HDR(1), buf, Display::FONT_5X7);
            if (editing && cursor == 1) {
                int x = 6 * 6 + 1;
                Display::InvertRect(x, ROW_HDR(1), 128 - x, 8);
            }

            float a = ModEngine::GetLFOAmplitude(lfo);
            snprintf(buf, sizeof(buf), " Amp:%.2f", a);
            Display::DrawString(0, ROW_HDR(2), buf, Display::FONT_5X7);
            if (editing && cursor == 2) {
                int x = 6 * 5 + 1;
                Display::InvertRect(x, ROW_HDR(2), 128 - x, 8);
            }

            int cv = ModEngine::GetLFOCVSlot(lfo);
            if (cv < 0) {
                snprintf(buf, sizeof(buf), " Out:None");
            } else if (cv < 100) {
                snprintf(buf, sizeof(buf), " Out:%s", cvSlotDisplayNames[cv]);
            } else {
                snprintf(buf, sizeof(buf), " Out:%d", cv);
            }
            Display::DrawString(0, ROW_HDR(3), buf, Display::FONT_5X7);
            if (editing && cursor == 3) {
                int x = 6 * 5 + 1;
                Display::InvertRect(x, ROW_HDR(3), 128 - x, 8);
            }

            if (!editing) {
                int cy = ROW_HDR(cursor);
                Display::InvertRect(0, cy, 128, 8);
            }
            Display::Flush();
        }

        void UIMenuPageMod::redrawTempo() {
            Display::Clear();
            char buf[32];
            Display::DrawString(0, ROW(0), "Tempo", Display::FONT_5X7);

            float bpm = ModEngine::GetTempoEngine().GetBPM();
            snprintf(buf, sizeof(buf), " BPM:%.0f", bpm);
            Display::DrawString(0, ROW_HDR(0), buf, Display::FONT_5X7);
            if (editing && cursor == 0) {
                int x = 6 * 6 + 1;
                Display::InvertRect(x, ROW_HDR(0), 128 - x, 8);
            }

            auto src = ModEngine::GetTempoEngine().GetSource();
            const char* srcStr = (src == SP::HELPERS::ctagTempo::Source::INTERNAL) ? "Internal" : "MIDI Clk";
            snprintf(buf, sizeof(buf), " Source:%s", srcStr);
            Display::DrawString(0, ROW_HDR(1), buf, Display::FONT_5X7);
            if (editing && cursor == 1) {
                int x = 6 * 8 + 1;
                Display::InvertRect(x, ROW_HDR(1), 128 - x, 8);
            }

            snprintf(buf, sizeof(buf), " [Tap Tempo]");
            Display::DrawString(0, ROW_HDR(2), buf, Display::FONT_5X7);

            snprintf(buf, sizeof(buf), " [%s]",
                     ModEngine::GetTempoEngine().IsRunning() ? "Reset" : "Restart");
            Display::DrawString(0, ROW_HDR(3), buf, Display::FONT_5X7);

            if (!editing) {
                int cy = ROW_HDR(cursor);
                Display::InvertRect(0, cy, 128, 8);
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
            int cy = ROW_HDR(cursor - scrollOff);
            Display::InvertRect(0, cy, 128, 8);
            if (TOTAL > VISIBLE_ITEMS_HDR)
                Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0_HDR, VISIBLE_ITEMS_HDR * LINE_H, TOTAL, cursor);
            Display::Flush();
        }

        void UIMenuPageMod::redrawCCEdit() {
            Display::Clear();
            char buf[32];
            snprintf(buf, sizeof(buf), "Slot %d", ccEditSlot);
            Display::DrawString(0, ROW(0), buf, Display::FONT_5X7);

            int cc = ModEngine::GetDynamicSlotCC(ccEditSlot);
            if (cc < 0) {
                snprintf(buf, sizeof(buf), " CC: --");
            } else {
                snprintf(buf, sizeof(buf), " CC: %d", cc);
            }
            Display::DrawString(0, ROW_HDR(0), buf, Display::FONT_5X7);
            if (editing && cursor == 0) {
                int x = 6 * 5 + 1;
                Display::InvertRect(x, ROW_HDR(0), 128 - x, 8);
            }

            int ch = ModEngine::GetDynamicSlotChan(ccEditSlot);
            snprintf(buf, sizeof(buf), " Chan: %d", ch);
            Display::DrawString(0, ROW_HDR(1), buf, Display::FONT_5X7);
            if (editing && cursor == 1) {
                int x = 6 * 6 + 1;
                Display::InvertRect(x, ROW_HDR(1), 128 - x, 8);
            }

            const char* learnStatus = ModEngine::IsLearning() ? "StopLearn" : "Learn";
            snprintf(buf, sizeof(buf), " [%s]", learnStatus);
            Display::DrawString(0, ROW_HDR(2), buf, Display::FONT_5X7);

            if (!editing) {
                int cy = ROW_HDR(cursor);
                Display::InvertRect(0, cy, 128, 8);
            }
            Display::Flush();
        }

        static const char* dirNames[4] = {"Fwd", "Bwd", "Pend", "Rand"};

        void UIMenuPageMod::redrawSeq(int seq) {
            Display::Clear();
            char buf[32];
            auto& sq = ModEngine::GetSequencer(seq);

            if (seqEditParams) {
                snprintf(buf, sizeof(buf), "SEQ%d Params", seq + 1);
                Display::DrawString(0, ROW(0), buf, Display::FONT_5X7);

                float sl = sq.GetStepLength();
                snprintf(buf, sizeof(buf), " Len:%.2f", sl);
                Display::DrawString(0, ROW_HDR(0), buf, Display::FONT_5X7);
                if (editing && cursor == 0) Display::InvertRect(6*5+1, ROW_HDR(0), 128-6*5-1, 8);

                int d = (int)sq.GetDirection();
                snprintf(buf, sizeof(buf), " Dir:%s", dirNames[d >= 0 && d < 4 ? d : 0]);
                Display::DrawString(0, ROW_HDR(1), buf, Display::FONT_5X7);
                if (editing && cursor == 1) Display::InvertRect(6*5+1, ROW_HDR(1), 128-6*5-1, 8);

                float sw = sq.GetSlew();
                snprintf(buf, sizeof(buf), " Slew:%.2f", sw);
                Display::DrawString(0, ROW_HDR(2), buf, Display::FONT_5X7);
                if (editing && cursor == 2) Display::InvertRect(6*6+1, ROW_HDR(2), 128-6*6-1, 8);

                int cv = sq.GetCVSlot();
                if (cv < 0) snprintf(buf, sizeof(buf), " CV:-");
                else if (cv < 100) snprintf(buf, sizeof(buf), " CV:%s", cvSlotDisplayNames[cv]);
                else snprintf(buf, sizeof(buf), " CV:%d", cv);
                Display::DrawString(0, ROW_HDR(3), buf, Display::FONT_5X7);
                if (editing && cursor == 3) Display::InvertRect(6*5+1, ROW_HDR(3), 128-6*5-1, 8);

                int tr = sq.GetTrigSlot();
                if (tr < 0) snprintf(buf, sizeof(buf), " Trig:-");
                else if (tr < 100) snprintf(buf, sizeof(buf), " Trig:%s", cvSlotDisplayNames[tr]);
                else snprintf(buf, sizeof(buf), " Trig:%d", tr);
                Display::DrawString(0, ROW_HDR(4), buf, Display::FONT_5X7);
                if (editing && cursor == 4) Display::InvertRect(6*6+1, ROW_HDR(4), 128-6*6-1, 8);

                if (!editing) {
                    int cy = ROW_HDR(cursor);
                    Display::InvertRect(0, cy, 128, 8);
                }
            } else {
                snprintf(buf, sizeof(buf), "SEQ%d Steps", seq + 1);
                Display::DrawString(0, ROW(0), buf, Display::FONT_5X7);

                constexpr int TOTAL = 17; // 16 steps + "[Params]"
                int scrollOff = (cursor > VISIBLE_ITEMS_HDR - 1) ? cursor - (VISIBLE_ITEMS_HDR - 1) : 0;
                int visible = TOTAL - scrollOff;
                if (visible > VISIBLE_ITEMS_HDR) visible = VISIBLE_ITEMS_HDR;
                for (int i = 0; i < visible; i++) {
                    int idx = scrollOff + i;
                    int y = ROW_HDR(i);
                    if (idx <= 15) {
                        float v = sq.GetStep(idx);
                        char trigChar = ' ';
                        snprintf(buf, sizeof(buf), " %2d:%+.2f", idx + 1, (double)v);
                        if (editing && cursor == idx) {
                            int x = 6 * 5 + 1;
                            Display::InvertRect(x, y, 128 - x, 8);
                        }
                    } else {
                        snprintf(buf, sizeof(buf), " [Params]");
                    }
                    Display::DrawString(0, y, buf, Display::FONT_5X7);
                }
                if (!editing) {
                    int cy = ROW_HDR(cursor - scrollOff);
                    Display::InvertRect(0, cy, 128, 8);
                }
                if (TOTAL > VISIBLE_ITEMS_HDR)
                    Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0_HDR, VISIBLE_ITEMS_HDR * LINE_H, TOTAL, cursor);
            }
            Display::Flush();
        }

        void UIMenuPageMod::redrawGate(int g) {
            Display::Clear();
            char buf[32];
            auto& gt = ModEngine::GetGate(g);

            if (seqEditParams) {
                snprintf(buf, sizeof(buf), "GATE%d Params", g + 1);
                Display::DrawString(0, ROW(0), buf, Display::FONT_5X7);

                float sl = gt.GetStepLength();
                snprintf(buf, sizeof(buf), " Len:%.2f", sl);
                Display::DrawString(0, ROW_HDR(0), buf, Display::FONT_5X7);
                if (editing && cursor == 0) Display::InvertRect(6*5+1, ROW_HDR(0), 128-6*5-1, 8);

                int d = (int)gt.GetDirection();
                snprintf(buf, sizeof(buf), " Dir:%s", dirNames[d >= 0 && d < 4 ? d : 0]);
                Display::DrawString(0, ROW_HDR(1), buf, Display::FONT_5X7);
                if (editing && cursor == 1) Display::InvertRect(6*5+1, ROW_HDR(1), 128-6*5-1, 8);

                float sw = gt.GetSwing();
                snprintf(buf, sizeof(buf), " Swg:%.2f", sw);
                Display::DrawString(0, ROW_HDR(2), buf, Display::FONT_5X7);
                if (editing && cursor == 2) Display::InvertRect(6*6+1, ROW_HDR(2), 128-6*6-1, 8);

                float gl = gt.GetGateLength();
                snprintf(buf, sizeof(buf), " Gate:%.2f", gl);
                Display::DrawString(0, ROW_HDR(3), buf, Display::FONT_5X7);
                if (editing && cursor == 3) Display::InvertRect(6*6+1, ROW_HDR(3), 128-6*6-1, 8);

                int t = gt.GetTrigSlot();
                if (t < 0) snprintf(buf, sizeof(buf), " Trig:-");
                else snprintf(buf, sizeof(buf), " Trig:%d", t);
                Display::DrawString(0, ROW_HDR(4), buf, Display::FONT_5X7);
                if (editing && cursor == 4) Display::InvertRect(6*6+1, ROW_HDR(4), 128-6*6-1, 8);

                float aa = gt.GetAccentAmount();
                snprintf(buf, sizeof(buf), " AAmt:%.2f", aa);
                Display::DrawString(0, ROW_HDR(5), buf, Display::FONT_5X7);
                if (editing && cursor == 5) Display::InvertRect(6*6+1, ROW_HDR(5), 128-6*6-1, 8);

                if (!editing) {
                    int cy = ROW_HDR(cursor);
                    Display::InvertRect(0, cy, 128, 8);
                }
            } else {
                snprintf(buf, sizeof(buf), "GATE%d Steps", g + 1);
                Display::DrawString(0, ROW(0), buf, Display::FONT_5X7);

                constexpr int TOTAL = 17;
                int scrollOff = (cursor > VISIBLE_ITEMS_HDR - 1) ? cursor - (VISIBLE_ITEMS_HDR - 1) : 0;
                int visible = TOTAL - scrollOff;
                if (visible > VISIBLE_ITEMS_HDR) visible = VISIBLE_ITEMS_HDR;
                for (int i = 0; i < visible; i++) {
                    int idx = scrollOff + i;
                    int y = ROW_HDR(i);
                    if (idx <= 15) {
                        bool en = gt.GetStepEnabled(idx);
                        uint8_t prob = gt.GetStepProbability(idx);
                        snprintf(buf, sizeof(buf), " %2d:%s P%3d", idx + 1, en ? "ON " : "OFF", prob);
                        if (editing && cursor == idx) {
                            int x = 6 * 8 + 1;
                            Display::InvertRect(x, y, 128 - x, 8);
                        }
                    } else {
                        snprintf(buf, sizeof(buf), " [Params]");
                    }
                    Display::DrawString(0, y, buf, Display::FONT_5X7);
                }
                if (!editing) {
                    int cy = ROW_HDR(cursor - scrollOff);
                    Display::InvertRect(0, cy, 128, 8);
                }
                if (TOTAL > VISIBLE_ITEMS_HDR)
                    Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0_HDR, VISIBLE_ITEMS_HDR * LINE_H, TOTAL, cursor);
            }
            Display::Flush();
        }
    }
}
