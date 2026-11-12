#include "UIMenuPageSeq.hpp"
#include "Display.hpp"
#include "DspProxy.hpp"
#include "CVSlotNames.hpp"
#include <cstring>
#include <cstdio>

using namespace CTAG::DRIVERS;

namespace CTAG {
    namespace CTRL {

        // ---- label tables (full words, spec §4.5) ----
        static const char* gateModeNames[19] = {
            "Generative Barlow", "Classic Alternating", "Trance Offbeat",
            "Floor Pump", "Late Pump", "Forward Gallop", "Reverse Gallop",
            "Rolling Gate", "Anthem Gate", "Dotted Eighths", "Tresillo",
            "Son Clave", "Rumba Syncopator", "Chord Stabs",
            "Call & Response", "Breakbeat Chop", "Accelerating Riser",
            "Turing Mutate", "Markov Set"
        };
        static const char* seqClockNames[3] = {"Global", "Internal", "MIDI"};
        // rack kModDivisions order (index 7 = 1/16)
        static const char* seqDivNames[13] = {
            "8/1", "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16",
            "1/32", "1/4T", "1/8T", "1/16T", "1/32T"
        };
        static const char* arpAlgoNames[15] = {
            "Acid 303 1-OCT", "Acid 303 2-OCT", "Acid 303 3-OCT",
            "Phrase Motif 1", "Phrase Motif 2", "Phrase Motif 3",
            "Markov Walker 1", "Markov Walker 2", "Markov Walker 3",
            "Metropolix 1", "Metropolix 2", "Metropolix 3",
            "Deja-Vu 1", "Deja-Vu 2", "Deja-Vu 3"
        };
        static const char* rootNames[14] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
            "W7 Drift", "Neo-Plagial"
        };
        static const char* neoOpNames[8] = {
            "Random", "P", "L", "R", "N", "S", "H", "PLR"
        };
        static const char* seqScaleNames[20] = {
            "Chromatic", "Ionian", "Dorian", "Phrygian", "Lydian",
            "Mixolydian", "Aeolian", "Locrian", "Pentatonic Major",
            "Pentatonic Minor", "Blues", "Harmonic Minor", "Melodic Minor",
            "Phrygian Dominant", "Whole Tone", "Octatonic", "Hirajoshi",
            "In Sen", "Double Harmonic", "Acid Pool"
        };
        // per-algo P1..P4 labels (the rack's per-algo mapping)
        static const char* arpParamNames[5][4] = {
            {"Density", "Slide", "Accent", "Mutate"},              // Acid 303
            {"Density", "Form", "Transform", "Mutate"},            // Phrase Motif
            {"Density", "Contour", "Pedal", "Gravity"},            // Markov Walker
            {"Probability", "Pulses", "Ratchet", "Accumulator"},   // Metropolix
            {"Density", "Deja-Vu", "Steps", "Spread"}              // Deja-Vu
        };
        // enumerated P values where the row is a choice, nullptr = 0..100
        static const char* const arpParamChoices[5][4] = {
            {nullptr, nullptr, nullptr, nullptr},                                   // Acid 303
            {nullptr, "ABAC/AABC/ABBC/AABA", "Inversion/Retrograde/Transpose/Displace", nullptr},
            {nullptr, nullptr, "Off/Root/Fifth/Bounce", nullptr},                   // Markov Walker
            {nullptr, nullptr, "Off/2x/3x/4x", "Pendulum/Wrap/Random/Hold"},        // Metropolix
            {nullptr, nullptr, "All/Pentatonic/Triad/Root", nullptr},               // Deja-Vu
        };
        static const uint8_t arpParamMax[5][4] = {
            {100, 100, 100, 100},
            {100, 3, 3, 100},
            {100, 15, 3, 100},
            {100, 7, 3, 3},
            {100, 100, 3, 100},
        };
        static const uint8_t arpParamStep[5][4] = {
            {1, 1, 1, 1},
            {1, 1, 1, 1},
            {1, 1, 1, 1},
            {1, 1, 1, 1},
            {1, 1, 1, 1},
        };

        // render one choice row: index into "A/B/C" strings
        static void FormatChoice(char *buf, int buflen, const char *choices,
                                 int idx, const char *label) {
            // choices is a '/'-separated list; walk to the idx-th entry
            char tmp[36];
            int cur = 0;
            const char *p = choices;
            while (*p && cur < idx) {
                if (*p == '/') cur++;
                p++;
            }
            int i = 0;
            while (*p && *p != '/' && i < (int)sizeof(tmp) - 1) tmp[i++] = *p++;
            tmp[i] = 0;
            snprintf(buf, buflen, " %s:%s", label, tmp);
        }

        // ---- live output strip: 4 gate dots (G1 G2 A1 A2), two 16-step
        // progress bars (A1/A2), 4 status squares (A1acc A1sld A2acc A2sld)
        // (spec §4.2 bit map: 0-1 gate, 2-3 arp gate, 4-5 acc, 6-7 slide,
        // 8-11/12-15 step, 16-23/24-31 note). All elements share the dot
        // size; the bar fills span their tracks (2 px per step). ----
        void UIMenuPageSeq::drawOutputLine() {
            uint32_t s = DspProxy::GetSeqSnapshot();
            for (int i = 0; i < 4; i++) {
                Display::DrawRect(1 + i * 8, 57, 6, 6, (s >> i) & 1);
            }
            const int barX[2] = {34, 67};
            for (int i = 0; i < 2; i++) {
                int step = (s >> (8 + i * 4)) & 0xFu;
                Display::DrawRect(barX[i], 57, 32, 6, false);
                int fill = (step + 1) * 2;    // 2 px per step across the track
                if (fill > 30) fill = 30;     // 1 px inset on the right
                Display::DrawRect(barX[i] + 1, 58, fill, 4, true);
            }
            const int markX[4] = {101, 108, 115, 122};
            for (int i = 0; i < 2; i++) {
                bool acc = ((s >> (4 + i)) & 1) != 0;
                bool sld = ((s >> (6 + i)) & 1) != 0;
                Display::DrawRect(markX[i * 2], 57, 6, 6, acc);     // accent
                Display::DrawRect(markX[i * 2 + 1], 57, 6, 6, sld); // slide
            }
        }

        void UIMenuPageSeq::init() {
            subPage = SP_MAIN;
            cursor = 0;
        }

        void UIMenuPageSeq::deinit() {
            if (dirty) { DspProxy::SeqSaveConfig(); dirty = false; }
        }

        void UIMenuPageSeq::onTick() {
            // redraw on live-strip changes and on DSP-confirmed value pushes
            uint32_t s = DspProxy::GetSeqSnapshot();
            uint32_t v = DspProxy::GetSeqVersion();
            if (s != lastDrawnSnapshot || v != lastDrawnVersion) {
                doRedraw();
            }
        }

        bool UIMenuPageSeq::wantsTick() const { return true; }

        // ---- row model ----

        int UIMenuPageSeq::rowCount() const {
            switch (subPage) {
                case SP_MAIN:  return 4;
                case SP_GATE1:
                case SP_GATE2: return 9;   // Mode,Density,Sync,Len,Clock,Div,Trig,Level,Pattern
                case SP_ARP1:
                case SP_ARP2:  return 15;  // Algo,P1..P4,Root,NeoOp,Scale,Glide,Clock,Div,4 slots
                default:       return 1;
            }
        }

        bool UIMenuPageSeq::rowIsAction(int row) const {
            (void)row;
            return false;   // no action rows: MAIN rows navigate via onButton
        }

        void UIMenuPageSeq::adjustRow(int row, int delta) {
            int i = (subPage == SP_GATE2 || subPage == SP_ARP2) ? 1 : 0;
            if (subPage == SP_GATE1 || subPage == SP_GATE2) {
                switch (row) {
                    case 0: DspProxy::SetSeqGateMode(i, DspProxy::GetSeqGateMode(i) + delta); break;
                    case 1: DspProxy::SetSeqGateDens(i, DspProxy::GetSeqGateDens(i) + delta); break;
                    case 2: DspProxy::SetSeqGateSync(i, DspProxy::GetSeqGateSync(i) + delta * 5); break;
                    case 3: DspProxy::SetSeqGateLen(i, DspProxy::GetSeqGateLen(i) + delta * 5); break;
                    case 4: DspProxy::SetSeqGateClk(i, DspProxy::GetSeqGateClk(i) + delta); break;
                    case 5: DspProxy::SetSeqGateDiv(i, DspProxy::GetSeqGateDiv(i) + delta); break;
                    case 6: DspProxy::SetSeqGateTrigSlot(i, DspProxy::GetSeqGateTrigSlot(i) + delta); break;
                    case 7: DspProxy::SetSeqGateCvSlot(i, DspProxy::GetSeqGateCvSlot(i) + delta); break;
                    default: break;   // row 8 = pattern preview, read-only
                }
            } else if (subPage == SP_ARP1 || subPage == SP_ARP2) {
                int algo = DspProxy::GetSeqArpAlgo(i) / 3;
                switch (row) {
                    case 0: DspProxy::SetSeqArpAlgo(i, DspProxy::GetSeqArpAlgo(i) + delta); break;
                    case 1:
                    case 2:
                    case 3:
                    case 4: {
                        int k = row - 1;
                        if (arpParamChoices[algo][k] != nullptr) {
                            // enumerated choice: step through the engine's own
                            // idx mapping (idx = p*K/101) so display and engine
                            // always agree; 0 stays 0.
                            const int K = arpParamMax[algo][k] + 1;
                            int idx = (DspProxy::GetSeqArpP(i, k) * K) / 101;
                            idx += delta;
                            if (idx < 0) idx = 0;
                            if (idx > arpParamMax[algo][k]) idx = arpParamMax[algo][k];
                            int p = (idx == 0) ? 0 : (idx * 101) / K + 1;
                            DspProxy::SetSeqArpP(i, k, p);
                        } else {
                            DspProxy::SetSeqArpP(i, k,
                                DspProxy::GetSeqArpP(i, k) + delta * arpParamStep[algo][k]);
                        }
                        break;
                    }
                    case 5: {
                        // combined Root row: 0-11 = fixed pitch class,
                        // 12 = W7 Drift, 13 = Neo-Plagial
                        int cur = DspProxy::GetSeqRootMode() == 0 ? DspProxy::GetSeqRoot()
                                  : (DspProxy::GetSeqRootMode() == 1 ? 12 : 13);
                        int next = cur + delta;
                        if (next < 0) next = 0;
                        if (next > 13) next = 13;
                        if (next <= 11) {
                            DspProxy::SetSeqRootMode(0);
                            DspProxy::SetSeqRoot(next);
                        } else {
                            DspProxy::SetSeqRootMode(next == 12 ? 1 : 2);
                        }
                        break;
                    }
                    case 6: DspProxy::SetSeqNeoOp(DspProxy::GetSeqNeoOp() + delta); break;
                    case 7: DspProxy::SetSeqScale(DspProxy::GetSeqScale() + delta); break;
                    case 8: DspProxy::SetSeqArpGlide(i, DspProxy::GetSeqArpGlide(i) + delta); break;
                    case 9: DspProxy::SetSeqArpClk(i, DspProxy::GetSeqArpClk(i) + delta); break;
                    case 10: DspProxy::SetSeqArpDiv(i, DspProxy::GetSeqArpDiv(i) + delta); break;
                    case 11: DspProxy::SetSeqArpPitchSlot(i, DspProxy::GetSeqArpPitchSlot(i) + delta); break;
                    case 12: DspProxy::SetSeqArpGateSlot(i, DspProxy::GetSeqArpGateSlot(i) + delta); break;
                    case 13: DspProxy::SetSeqArpAccSlot(i, DspProxy::GetSeqArpAccSlot(i) + delta); break;
                    case 14: DspProxy::SetSeqArpSlideSlot(i, DspProxy::GetSeqArpSlideSlot(i) + delta); break;
                }
            }
            dirty = true;
        }

        void UIMenuPageSeq::formatRow(int row, char *buf, int buflen) {
            int i = (subPage == SP_GATE2 || subPage == SP_ARP2) ? 1 : 0;
            if (subPage == SP_GATE1 || subPage == SP_GATE2) {
                switch (row) {
                    case 0:
                        snprintf(buf, buflen, " Mode:%s", gateModeNames[DspProxy::GetSeqGateMode(i)]);
                        break;
                    case 1:
                        snprintf(buf, buflen, " Density:%d", DspProxy::GetSeqGateDens(i));
                        break;
                    case 2:
                        snprintf(buf, buflen, " Syncopation:%d%%", DspProxy::GetSeqGateSync(i));
                        break;
                    case 3:
                        snprintf(buf, buflen, " Gate Length:%d%%", DspProxy::GetSeqGateLen(i));
                        break;
                    case 4:
                        snprintf(buf, buflen, " Clock:%s", seqClockNames[DspProxy::GetSeqGateClk(i)]);
                        break;
                    case 5:
                        snprintf(buf, buflen, " Division:%s", seqDivNames[DspProxy::GetSeqGateDiv(i)]);
                        break;
                    case 6:
                        snprintf(buf, buflen, " Trig Slot:%s", TrigSlotName(DspProxy::GetSeqGateTrigSlot(i)));
                        break;
                    case 7: {
                        int s = DspProxy::GetSeqGateCvSlot(i);
                        if (s >= 0 && s < (int)(sizeof(cvSlotDisplayNames) / sizeof(cvSlotDisplayNames[0])))
                            snprintf(buf, buflen, " Level Slot:%s", cvSlotDisplayNames[s]);
                        else
                            snprintf(buf, buflen, " Level Slot:%d", s);
                        break;
                    }
                    case 8: {
                        // 16-step pattern preview, 'o' = on, 'x' = off
                        uint32_t mask = DspProxy::GetSeqGateMask(i);
                        char pat[17];
                        for (int st = 0; st < 16; st++)
                            pat[st] = ((mask >> (15 - st)) & 1u) ? 'o' : 'x';
                        pat[16] = 0;
                        snprintf(buf, buflen, " %s", pat);
                        break;
                    }
                }
            } else if (subPage == SP_ARP1 || subPage == SP_ARP2) {
                int algo = DspProxy::GetSeqArpAlgo(i) / 3;
                switch (row) {
                    case 0:
                        snprintf(buf, buflen, " Algorithm:%s", arpAlgoNames[DspProxy::GetSeqArpAlgo(i)]);
                        break;
                    case 1:
                    case 2:
                    case 3:
                    case 4: {
                        int k = row - 1;
                        const char *choices = arpParamChoices[algo][k];
                        if (choices != nullptr) {
                            // display the ENGINE's idx (p*K/101), not the raw p
                            const int K = arpParamMax[algo][k] + 1;
                            FormatChoice(buf, buflen, choices,
                                         (DspProxy::GetSeqArpP(i, k) * K) / 101,
                                         arpParamNames[algo][k]);
                        } else {
                            snprintf(buf, buflen, " %s:%d", arpParamNames[algo][k],
                                     DspProxy::GetSeqArpP(i, k));
                        }
                        break;
                    }
                    case 5: {
                        int cur = DspProxy::GetSeqRootMode() == 0 ? DspProxy::GetSeqRoot()
                                  : (DspProxy::GetSeqRootMode() == 1 ? 12 : 13);
                        snprintf(buf, buflen, " Root:%s", rootNames[cur]);
                        break;
                    }
                    case 6:
                        // the op is engine state even while inactive — show
                        // it always (a permanently-dead "(n/a)" row is just
                        // redundant space)
                        snprintf(buf, buflen, " Neo-Op:%s", neoOpNames[DspProxy::GetSeqNeoOp()]);
                        break;
                    case 7:
                        snprintf(buf, buflen, " Scale:%s", seqScaleNames[DspProxy::GetSeqScale()]);
                        break;
                    case 8: {
                        int g = DspProxy::GetSeqArpGlide(i);
                        if (g == 0)
                            snprintf(buf, buflen, " Glide:Instant");
                        else
                            snprintf(buf, buflen, " Glide:%d ms", g);
                        break;
                    }
                    case 9:
                        snprintf(buf, buflen, " Clock:%s", seqClockNames[DspProxy::GetSeqArpClk(i)]);
                        break;
                    case 10:
                        snprintf(buf, buflen, " Division:%s", seqDivNames[DspProxy::GetSeqArpDiv(i)]);
                        break;
                    case 11: {
                        int s = DspProxy::GetSeqArpPitchSlot(i);
                        if (s >= 0 && s < (int)(sizeof(cvSlotDisplayNames) / sizeof(cvSlotDisplayNames[0])))
                            snprintf(buf, buflen, " Pitch Slot:%s", cvSlotDisplayNames[s]);
                        else
                            snprintf(buf, buflen, " Pitch Slot:%d", s);
                        break;
                    }
                    case 12:
                        snprintf(buf, buflen, " Gate Slot:%s", TrigSlotName(DspProxy::GetSeqArpGateSlot(i)));
                        break;
                    case 13:
                        snprintf(buf, buflen, " Accent Slot:%s", TrigSlotName(DspProxy::GetSeqArpAccSlot(i)));
                        break;
                    case 14:
                        snprintf(buf, buflen, " Slide Slot:%s", TrigSlotName(DspProxy::GetSeqArpSlideSlot(i)));
                        break;
                }
            }
        }

        // ---- event handling ----

        void UIMenuPageSeq::onEncoder(int delta) {
            cursor += delta;
            if (cursor < 0) cursor = 0;
            if (cursor > rowCount() - 1) cursor = rowCount() - 1;
            doRedraw();
        }

        void UIMenuPageSeq::onValueEncoder(int delta) {
            if (subPage == SP_MAIN) return;
            adjustRow(cursor, delta);
            doRedraw();
        }

        void UIMenuPageSeq::onButton(int btnId, bool longPress) {
            if (btnId != 2 || longPress) {
                doRedraw();
                return;
            }
            if (subPage == SP_MAIN) {
                SubPage target = SP_MAIN;
                switch (cursor) {
                    case 0: target = SP_GATE1; break;
                    case 1: target = SP_GATE2; break;
                    case 2: target = SP_ARP1; break;
                    case 3: target = SP_ARP2; break;
                }
                if (target != SP_MAIN) {
                    subPage = target;
                    cursor = 0;
                }
            }
            doRedraw();
        }

        bool UIMenuPageSeq::onBack() {
            if (subPage == SP_MAIN) return false;
            // restore the MAIN row that opened this sub-page (same behavior
            // as UIMenuPageMod's back navigation)
            switch (subPage) {
                case SP_GATE1: cursor = 0; break;
                case SP_GATE2: cursor = 1; break;
                case SP_ARP1:  cursor = 2; break;
                case SP_ARP2:  cursor = 3; break;
                default:       cursor = 0; break;
            }
            subPage = SP_MAIN;
            doRedraw();
            return true;
        }

        // ---- rendering ----

        void UIMenuPageSeq::doRedraw() {
            switch (subPage) {
                case SP_MAIN:  redrawMain(); break;
                case SP_GATE1: redrawRows("Gate 1"); break;
                case SP_GATE2: redrawRows("Gate 2"); break;
                case SP_ARP1:  redrawRows("ARP 1"); break;
                case SP_ARP2:  redrawRows("ARP 2"); break;
                default: break;
            }
        }

        void UIMenuPageSeq::redrawMain() {
            Display::Clear();
            char buf[32];
            const int TOTAL = 4;
            static const char* mainNames[4] = {"Gate 1", "Gate 2", "ARP 1", "ARP 2"};
            for (int i = 0; i < TOTAL; i++) {
                int y = ROW(i);
                if (i < 2) {
                    snprintf(buf, sizeof(buf), " %s:%s", mainNames[i],
                             gateModeNames[DspProxy::GetSeqGateMode(i)]);
                } else {
                    snprintf(buf, sizeof(buf), " %s:%s", mainNames[i],
                             arpAlgoNames[DspProxy::GetSeqArpAlgo(i - 2)]);
                }
                Display::DrawString(0, y, buf, FONT_UI);
            }
            int cy = ROW(cursor);
            Display::InvertRect(0, cy - 1, 128, 8);
            drawOutputLine();
            lastDrawnSnapshot = DspProxy::GetSeqSnapshot();
            lastDrawnVersion = DspProxy::GetSeqVersion();
            Display::Flush();
        }

        void UIMenuPageSeq::redrawRows(const char *title) {
            Display::Clear();
            char buf[48];
            Display::DrawString(0, ROW(0), title, FONT_UI);

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
            int cy = ROW_HDR(cursor - scrollOff);
            Display::InvertRect(0, cy - 1, 128, 8);
            if (TOTAL > VIS) {
                Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0_HDR, VIS * LINE_H, TOTAL, cursor);
            }
            drawOutputLine();
            lastDrawnSnapshot = DspProxy::GetSeqSnapshot();
            lastDrawnVersion = DspProxy::GetSeqVersion();
            Display::Flush();
        }

    }
}
