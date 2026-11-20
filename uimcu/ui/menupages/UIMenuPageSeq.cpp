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
        static const char* arpAlgoNames[21] = {
            "Acid 303 1-OCT", "Acid 303 2-OCT", "Acid 303 3-OCT",
            "Phrase Motif 1", "Phrase Motif 2", "Phrase Motif 3",
            "Markov Walker 1", "Markov Walker 2", "Markov Walker 3",
            "Metropolix 1", "Metropolix 2", "Metropolix 3",
            "Deja-Vu 1", "Deja-Vu 2", "Deja-Vu 3",
            "VARG 1-OCT", "VARG 2-OCT", "VARG 3-OCT",
            "VARG Chord 1-OCT", "VARG Chord 2-OCT", "VARG Chord 3-OCT"
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
        // per-algo P1..P8 labels. VARG algos share Progression/Rhythm/
        // Quality/Mutate and Chord Bars; only the chord algo uses the
        // strum trio. Unused rows are hidden by rowCount() below.
        static const char* arpParamNames[7][9] = {
            {"Density", "Slide", "Accent", "Mutate", "-", "-", "-", "-"},
            {"Density", "Form", "Transform", "Mutate", "-", "-", "-", "-"},
            {"Density", "Contour", "Pedal", "Gravity", "-", "-", "-", "-"},
            {"Probability", "Pulses", "Ratchet", "Accumulator", "-", "-", "-", "-", "-"},
            {"Density", "Deja-Vu", "Steps", "DV Spread", "-", "-", "-", "-"},
            {"Progression", "Rhythm", "Quality", "Mutate", "Chord Bars", "Arp Order", "-", "-", "-"},
            {"Progression", "Rhythm", "Quality", "Mutate", "Chord Bars", "Arp Order", "Spread", "Ghost", "Chord Mode"}
        };
        // enumerated P values where the row is a choice, nullptr = 0..100
        static const char* const arpParamChoices[7][9] = {
            {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
            {nullptr, "ABAC/AABC/ABBC/AABA", "Inversion/Retrograde/Transpose/Displace", nullptr, nullptr, nullptr, nullptr, nullptr},
            {nullptr, nullptr, "Off/Root/Fifth/Bounce", nullptr, nullptr, nullptr, nullptr, nullptr},
            {nullptr, nullptr, "Off/2x/3x/4x", "Pendulum/Wrap/Random/Hold", nullptr, nullptr, nullptr, nullptr},
            {nullptr, nullptr, "All/Pentatonic/Triad/Root", nullptr, nullptr, nullptr, nullptr, nullptr},
            {"Generative/i-bVII/i-bVI-bVII/i-bIII-bVI/Weitzmann/add9-dim7/Mediant/i-bII-v/Pool 5/Planing/i-I+",
             "TRM16/TRM8/QRT/HALF/BURST/GAL/RGAL/68L/STAB/SKIP/CHAR/RESP/TURN/DIRGE/CHOP/DBL/CAMP/BACK/DBEA/CRST/TRIP/QTRIP/P516/WALTZ/TRM32",
             "Raw/Triad/Diss", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},  // VARG
            {"Generative/i-bVII/i-bVI-bVII/i-bIII-bVI/Weitzmann/add9-dim7/Mediant/i-bII-v/Pool 5/Planing/i-I+",
             "TRM16/TRM8/QRT/HALF/BURST/GAL/RGAL/68L/STAB/SKIP/CHAR/RESP/TURN/DIRGE/CHOP/DBL/CAMP/BACK/DBEA/CRST/TRIP/QTRIP/P516/WALTZ/TRM32",
             "Raw/Triad/Diss", nullptr, "1/2/4/8",
             "Up/Down/Pendulum/Converge/Diverge/Random/Thru/Broken1/Broken2",
             nullptr, nullptr, "Burst/Arpeggio"},                                                     // VARG Chord
        };
        static const uint8_t arpParamMax[7][9] = {
            {100, 100, 100, 100, 0, 0, 0, 0, 0},
            {100, 3, 3, 100, 0, 0, 0, 0, 0},
            {100, 15, 3, 100, 0, 0, 0, 0, 0},
            {100, 7, 3, 3, 0, 0, 0, 0, 0},
            {100, 100, 3, 100, 0, 0, 0, 0, 0},
            {10, 24, 2, 100, 3, 9, 0, 0, 0},                                       // VARG
            {10, 24, 2, 100, 3, 9, 100, 100, 1},                                   // VARG Chord
        };
        static const uint8_t arpParamStep[7][9] = {
            {1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1},
            {1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1}, {1,1,1,1,1,1,1,1,1},
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

        // ---- live output strip ----
        // MAIN: 2x2 pattern grid — top row GATE1/GATE2 trance masks, bottom
        // row ARP1/ARP2 gate patterns, 4 px pitch filling the full width.
        // solid 3x3 square with center dot = playhead on a gated step,
        // hollow 3x3 square = playhead on a muted step, 1 px dot = gated,
        // blank = muted. Rows 5 px tall, 2 px gap.
        // Sub-page (GATE n / ARP n): full-width detail view, one ext-font
        // glyph per step — ○ muted, ● gate, ◉ gate+accent, ▲ gate+slide,
        // ◆ gate+accent+slide — ☐ marks the playhead.
        void UIMenuPageSeq::drawOutputLine() {
            uint32_t s = DspProxy::GetSeqSnapshot();
            if (subPage != SP_MAIN) {
                const uint32_t gateMask = (subPage == SP_GATE1)
                                              ? DspProxy::GetSeqGateMask(0)
                                          : (subPage == SP_GATE2)
                                              ? DspProxy::GetSeqGateMask(1)
                                          : (subPage == SP_ARP1)
                                              ? DspProxy::GetSeqArpGateMask(0)
                                              : DspProxy::GetSeqArpGateMask(1);
                const uint32_t accMask = (subPage == SP_ARP1)
                                             ? DspProxy::GetSeqArpAccMask(0)
                                         : (subPage == SP_ARP2)
                                             ? DspProxy::GetSeqArpAccMask(1)
                                             : 0;
                const uint32_t sldMask = (subPage == SP_ARP1)
                                             ? DspProxy::GetSeqArpSlideMask(0)
                                         : (subPage == SP_ARP2)
                                             ? DspProxy::GetSeqArpSlideMask(1)
                                             : 0;
                const int step = (subPage == SP_GATE1)   ? DspProxy::GetSeqGateStep(0)
                                 : (subPage == SP_GATE2) ? DspProxy::GetSeqGateStep(1)
                                 : (subPage == SP_ARP1)  ? (int)((s >> 8) & 0xFu)
                                                         : (int)((s >> 12) & 0xFu);
                for (int st = 0; st < 16; ++st) {
                    const int x = st * 8;
                    const bool gate = ((gateMask >> (15 - st)) & 1u) != 0;
                    const bool acc = ((accMask >> (15 - st)) & 1u) != 0;
                    const bool sld = ((sldMask >> (15 - st)) & 1u) != 0;
                    const char *g = "\xE2\x97\x8B";               // ○ muted
                    if (st == step) g = "\xE2\x98\x90";           // ☐ playhead
                    else if (gate && acc && sld) g = "\xE2\x97\x86"; // ◆
                    else if (gate && acc) g = "\xE2\x97\x89";     // ◉
                    else if (gate && sld) g = "\xE2\x96\xB2";     // ▲
                    else if (gate) g = "\xE2\x97\x8F";            // ●
                    Display::DrawString(x, 53, g, FONT_UI);
                }
                return;
            }
            // ---- MAIN: 2x2 grid, full width ----
            const uint32_t masks[2][2] = {
                { DspProxy::GetSeqGateMask(0), DspProxy::GetSeqGateMask(1) },
                { DspProxy::GetSeqArpGateMask(0), DspProxy::GetSeqArpGateMask(1) },
            };
            const int steps[2][2] = {
                { DspProxy::GetSeqGateStep(0), DspProxy::GetSeqGateStep(1) },
                { (int)((s >> 8) & 0xFu), (int)((s >> 12) & 0xFu) },
            };
            const int rowY[2] = {52, 59};   // 5 px rows, 2 px gap
            for (int row = 0; row < 2; ++row) {
                for (int col = 0; col < 2; ++col) {
                    const int x0 = col * 64;
                    for (int st = 0; st < 16; ++st) {
                        const int x = x0 + st * 4;
                        const bool gated = ((masks[row][col] >> (15 - st)) & 1u) != 0;
                        if (st == steps[row][col]) {
                            Display::DrawRect(x, rowY[row] + 1, 3, 3, gated);
                            if (gated) Display::DrawRect(x + 1, rowY[row] + 2, 1, 1, true);
                        } else if (gated) {
                            Display::DrawRect(x + 1, rowY[row] + 2, 1, 1, true);
                        }
                    }
                }
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
                case SP_ARP2: {
                    // Hide P rows that the current algo does not use, so no
                    // dead "-" rows appear on non-VARG / note-ostinato algos.
                    const int i = (subPage == SP_ARP2) ? 1 : 0;
                    const int algo = DspProxy::GetSeqArpAlgo(i) / 3;
                    const int nP = (algo == 6) ? 9 : (algo == 5) ? 6 : 4;
                    return 12 + nP;  // Algo + P rows + 11 fixed rows
                }
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
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                    case 9: {
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
                    case 10: {
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
                    case 11: DspProxy::SetSeqNeoOp(DspProxy::GetSeqNeoOp() + delta); break;
                    case 12: DspProxy::SetSeqScale(DspProxy::GetSeqScale() + delta); break;
                    case 13: DspProxy::SetSeqArpGlide(i, DspProxy::GetSeqArpGlide(i) + delta); break;
                    case 14: DspProxy::SetSeqArpOct(i, DspProxy::GetSeqArpOct(i) + delta); break;
                    case 15: DspProxy::SetSeqArpClk(i, DspProxy::GetSeqArpClk(i) + delta); break;
                    case 16: DspProxy::SetSeqArpDiv(i, DspProxy::GetSeqArpDiv(i) + delta); break;
                    case 17: DspProxy::SetSeqArpPitchSlot(i, DspProxy::GetSeqArpPitchSlot(i) + delta); break;
                    case 18: DspProxy::SetSeqArpGateSlot(i, DspProxy::GetSeqArpGateSlot(i) + delta); break;
                    case 19: DspProxy::SetSeqArpAccSlot(i, DspProxy::GetSeqArpAccSlot(i) + delta); break;
                    case 20: DspProxy::SetSeqArpSlideSlot(i, DspProxy::GetSeqArpSlideSlot(i) + delta); break;
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
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                    case 9: {
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
                    case 10: {
                        int cur = DspProxy::GetSeqRootMode() == 0 ? DspProxy::GetSeqRoot()
                                  : (DspProxy::GetSeqRootMode() == 1 ? 12 : 13);
                        snprintf(buf, buflen, " Root:%s", rootNames[cur]);
                        break;
                    }
                    case 11:
                        // the op is engine state even while inactive — show
                        // it always (a permanently-dead "(n/a)" row is just
                        // redundant space)
                        snprintf(buf, buflen, " Neo-Op:%s", neoOpNames[DspProxy::GetSeqNeoOp()]);
                        break;
                    case 12:
                        snprintf(buf, buflen, " Scale:%s", seqScaleNames[DspProxy::GetSeqScale()]);
                        break;
                    case 13: {
                        int g = DspProxy::GetSeqArpGlide(i);
                        if (g == 0)
                            snprintf(buf, buflen, " Glide:Instant");
                        else
                            snprintf(buf, buflen, " Glide:%d ms", g);
                        break;
                    }
                    case 14: {
                        int oct = DspProxy::GetSeqArpOct(i);
                        snprintf(buf, buflen, " Octave:%+d", oct);
                        break;
                    }
                    case 15:
                        snprintf(buf, buflen, " Clock:%s", seqClockNames[DspProxy::GetSeqArpClk(i)]);
                        break;
                    case 16:
                        snprintf(buf, buflen, " Division:%s", seqDivNames[DspProxy::GetSeqArpDiv(i)]);
                        break;
                    case 17: {
                        int s = DspProxy::GetSeqArpPitchSlot(i);
                        if (s >= 0 && s < (int)(sizeof(cvSlotDisplayNames) / sizeof(cvSlotDisplayNames[0])))
                            snprintf(buf, buflen, " Pitch Slot:%s", cvSlotDisplayNames[s]);
                        else
                            snprintf(buf, buflen, " Pitch Slot:%d", s);
                        break;
                    }
                    case 18:
                        snprintf(buf, buflen, " Gate Slot:%s", TrigSlotName(DspProxy::GetSeqArpGateSlot(i)));
                        break;
                    case 19:
                        snprintf(buf, buflen, " Accent Slot:%s", TrigSlotName(DspProxy::GetSeqArpAccSlot(i)));
                        break;
                    case 20:
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
