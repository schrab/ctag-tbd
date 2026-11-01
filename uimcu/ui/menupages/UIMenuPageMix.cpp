/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2020 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

The CTAG TBD hardware design is released under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0).
Details here: https://creativecommons.org/licenses/by-nc-sa/4.0/

CTAG TBD is provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.
***************/

#include "UIMenuPageMix.hpp"
#include "Display.hpp"
#include "DspProxy.hpp"
#include "InputSource.hpp"
#include "UIMenu.hpp"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

using namespace CTAG::DRIVERS;
using namespace CTAG::AUDIO;
using namespace rapidjson;

namespace CTAG {
    namespace CTRL {
        // Strip order: input gain, TAPE, output L, output R. The level
        // strips preview the codec directly on every turn so the faders
        // feel live. The IN strip meters the stereo input pair and carries
        // the mic/line source under its meters (OK toggles it); the TAPE
        // strip is not a config fader: it is the tape playback gain with
        // the tape signal meter beside it (routing on OK, shown as its
        // value line).
        const UIMenuPageMix::Strip UIMenuPageMix::STRIP_TABLE[] = {
                {"IN",   "input_gain",      0, 8,                        false, METER_IN,    2},
                {"TAPE", "",                0, 100,                      false, METER_NONE,  0},
                {"LOUT", "ch0_codecLvlOut", 0, DspProxy::MAX_OUTPUT_LEVEL, false, METER_OUT_L, 1},
                {"ROUT", "ch1_codecLvlOut", 0, DspProxy::MAX_OUTPUT_LEVEL, false, METER_OUT_R, 1},
        };

        void UIMenuPageMix::DebugStateJson(std::string &out) {
            out = "{\"cursor\":" + to_string(cursor)
                  + ",\"values\":[";
            for (int i = 0; i < STRIPS; i++) {
                if (i) out += ",";
                out += to_string(value[i]);
            }
            out += "],\"max\":" + to_string(DspProxy::MAX_OUTPUT_LEVEL) + "}";
        }

        void UIMenuPageMix::init() {
            cursor = 0;
            encoderAccel = 0;
            lastEncDir = 0;
            lastEncTick = 0;
            dirty = false;
            for (int i = 0; i < STRIPS; i++) {
                value[i] = STRIP_TABLE[i].min;
                meter[i][0] = 0;
                meter[i][1] = 0;
                hold[i][0] = 0;
                hold[i][1] = 0;
            }
            // The TAPE fader is not config-backed: it seeds from the DSP's
            // current playback gain.
            value[TAP_STRIP] = DspProxy::GetSdGain();
            tapeMeter = 0;
            tapeHold = 0;
            source = 0;
            parseConfig();
        }

        void UIMenuPageMix::deinit() {
            DspProxy::SetMeteringEnabled(false);
            // Persist whatever is staged (including a deferred source switch
            // the user left in edit mode on). Skip the flash write when the
            // page was only viewed.
            if (dirty) {
                applyCurrent(true);
                dirty = false;
            }
        }

        void UIMenuPageMix::parseConfig() {
            for (int i = 0; i < STRIPS; i++) value[i] = STRIP_TABLE[i].min;
            source = 0;

            const char *json = DspProxy::GetCStrJSONConfiguration();
            if (!json) return;

            Document doc;
            doc.Parse(json);
            if (!doc.IsObject()) return;

            for (int i = 0; i < STRIPS; i++) {
                const Strip &st = STRIP_TABLE[i];
                if (i == TAP_STRIP) continue; // not a config strip
                if (!doc.HasMember(st.id) || !doc[st.id].IsString()) continue;
                int v = atoi(doc[st.id].GetString());
                if (v < st.min) v = st.min;
                if (v > st.max) v = st.max;
                value[i] = v;
            }
            if (doc.HasMember("input_source") && doc["input_source"].IsString()) {
                int idx = InputSource::Parse(doc["input_source"].GetString());
                if (idx >= 0) source = idx;
            }
        }

        void UIMenuPageMix::valueText(int i, char *buf, size_t len) const {
            snprintf(buf, len, "%d", value[i]);
        }

        // "mic"/"line" — full names for the config write.
        void UIMenuPageMix::sourceText(char *buf, size_t len) const {
            // Walk InputSource::OPTIONS by index (comma-separated list).
            char optCopy[32];
            snprintf(optCopy, sizeof(optCopy), "%s", InputSource::OPTIONS);
            int idx = 0;
            const char *p = optCopy;
            while (p != nullptr && *p != '\0' && idx < source) {
                const char *comma = strchr(p, ',');
                if (!comma) break;
                p = comma + 1;
                idx++;
            }
            if (p == nullptr || *p == '\0') snprintf(buf, len, "mic");
            else {
                size_t optLen = strcspn(p, ",");
                snprintf(buf, len, "%.*s", (int)optLen, p);
            }
        }

        // Short forms for the value line: "5 lin" / "5 mic" — one line with
        // the gain number, per the strip's 25 px width.
        void UIMenuPageMix::sourceShort(char *buf, size_t len) const {
            snprintf(buf, len, "%s", source == 1 ? "lin" : "mic");
        }

        void UIMenuPageMix::applyCurrent(bool includeDeferred) {
            const char *fullJson = DspProxy::GetCStrJSONConfiguration();
            if (!fullJson) return;
            Document doc;
            doc.Parse(fullJson);
            if (!doc.IsObject()) return;

            for (int i = 0; i < STRIPS; i++) {
                const Strip &st = STRIP_TABLE[i];
                if (i == TAP_STRIP) continue; // not a config strip
                if (st.deferred && !includeDeferred) continue;

                char valStr[16];
                snprintf(valStr, sizeof(valStr), "%d", value[i]);
                Value v(valStr, doc.GetAllocator());
                if (doc.HasMember(st.id)) {
                    doc[st.id].Swap(v);
                } else {
                    Value key(st.id, doc.GetAllocator());
                    doc.AddMember(key, v, doc.GetAllocator());
                }
                if (i == GAIN_STRIP) {
                    // input_source lives on the IN strip
                    char src[16];
                    sourceText(src, sizeof(src));
                    Value sv(src, doc.GetAllocator());
                    if (doc.HasMember("input_source")) {
                        doc["input_source"].Swap(sv);
                    } else {
                        Value k("input_source", doc.GetAllocator());
                        doc.AddMember(k, sv, doc.GetAllocator());
                    }
                }
            }

            StringBuffer buf;
            Writer<StringBuffer> writer(buf);
            doc.Accept(writer);
            DspProxy::SetConfigurationFromJSON(buf.GetString());
        }

        void UIMenuPageMix::refreshMeters() {
            for (int i = 0; i < STRIPS; i++) {
                int src = STRIP_TABLE[i].meter; // first vu channel, or METER_NONE
                for (int ch = 0; ch < STRIP_TABLE[i].meterN; ch++) {
                    int raw = src >= 0 ? (int)DspProxy::GetVUPeak(src + ch) : 0;
                    if (raw > 1000) raw = 1000;
                    if (raw > meter[i][ch]) {
                        meter[i][ch] = raw;
                        hold[i][ch] = VU_HOLD_TICKS;
                    } else if (hold[i][ch] > 0) {
                        hold[i][ch]--;
                    } else {
                        meter[i][ch] -= VU_DECAY_PER_TICK;
                        if (meter[i][ch] < 0) meter[i][ch] = 0;
                    }
                }
            }
        }

        // Tape level uses the same hold/decay smoothing as the VU strips,
        // sourced from the stream's tape peak ("tl" key).
        void UIMenuPageMix::refreshTapeMeter() {
            int raw = (int)DspProxy::GetSdLevel();
            if (raw > 1000) raw = 1000;
            if (raw > tapeMeter) {
                tapeMeter = raw;
                tapeHold = VU_HOLD_TICKS;
            } else if (tapeHold > 0) {
                tapeHold--;
            } else {
                tapeMeter -= VU_DECAY_PER_TICK;
                if (tapeMeter < 0) tapeMeter = 0;
            }
        }

        void UIMenuPageMix::drawFader(int i) {
            const Strip &st = STRIP_TABLE[i];
            int x = STRIP_X0 + i * STRIP_W + 2;
            // track
            Display::DrawRect(x, FADER_Y, FADER_W, FADER_H, false, true);
            // value height, min 1px so a zero setting still shows the cap;
            // ALWAYS bottom-anchored. The selection highlight is a drawn
            // outer frame (only while the panel is open for editing — at
            // ROOT nothing is editable), never an inversion: an inverted
            // fader reads as filling from the top.
            int range = st.max - st.min;
            int fillH = range > 0 ? ((value[i] - st.min) * (FADER_H - 2)) / range : 0;
            if (fillH < 1) fillH = 1;
            if (fillH > FADER_H - 2) fillH = FADER_H - 2;
            Display::DrawRect(x + 1, FADER_Y + FADER_H - 1 - fillH, FADER_W - 2, fillH, true, true);
            if (UIMenu::IsPanelEdit() && i == cursor) {
                Display::DrawRect(x - 2, FADER_Y - 2, FADER_W + 4, FADER_H + 4, false, true);
            }
        }

        void UIMenuPageMix::drawMeters(int i) {
            if (STRIP_TABLE[i].meter == METER_NONE) return;
            // 1 px clear of the selection frame's right edge.
            int bx = STRIP_X0 + i * STRIP_W + 12;
            for (int ch = 0; ch < STRIP_TABLE[i].meterN; ch++) {
                float lvl = (float)meter[i][ch] / 1000.0f;
                Display::DrawVUMeterV(bx + ch * (VU_W + 2), FADER_Y, VU_W, VU_H, lvl, false);
            }
        }

        void UIMenuPageMix::drawStrip(int i) {
            const Strip &st = STRIP_TABLE[i];
            int x = STRIP_X0 + i * STRIP_W;

            Display::DrawString(x, LABEL_Y, st.label, FONT_UI);
            // No frame around the label: the fader already carries the
            // selection highlight.

            if (i == TAP_STRIP) {
                // Tape strip: a real fader (playback gain, value encoder)
                // beside a vertical meter of the actual tape signal, the
                // routing as its value line, and the label inverted while a
                // recording is running. drawFader carries the standard
                // fader-style selection highlight.
                if (DspProxy::SdIsRecording()) {
                    Display::InvertRect(x, LABEL_Y - 1, STRIP_W - 1, 9);
                }
                drawFader(i);
                Display::DrawVUMeterV(x + 12, FADER_Y, VU_W, VU_H,
                                      (float)tapeMeter / 1000.0f, false);
                char val[8];
                snprintf(val, sizeof(val), "%s", DspProxy::GetSdRoute() == 1 ? "IN" : "OUT");
                Display::DrawString(x, VALUE_Y, val, FONT_UI);
                return;
            }

            drawFader(i);
            drawMeters(i);

            char val[16];
            valueText(i, val, sizeof(val));
            if (i == GAIN_STRIP) {
                // Source rides right of the gain number, one line for all
                // bottom labels.
                char src[8];
                sourceShort(src, sizeof(src));
                snprintf(val, sizeof(val), "%d %s", value[GAIN_STRIP], src);
            }
            Display::DrawString(x, VALUE_Y, val, FONT_UI);
        }

        void UIMenuPageMix::doRedraw() {
            Display::Clear();
            for (int i = 0; i < STRIPS; i++) drawStrip(i);
            Display::Flush();
        }

        void UIMenuPageMix::onTick() {
            // Ticked at 25 Hz while the page is on screen (ROOT included).
            // Arm metering on the DSP (no-op after the first tick); the peak
            // pass is skipped on the A1S while it is off.
            DspProxy::SetMeteringEnabled(true);
            // Re-sample the peaks and repaint only when a bar moved — an
            // idle mixer must not generate SPI traffic.
            int before[STRIPS][2];
            memcpy(before, meter, sizeof(meter));
            refreshMeters();
            refreshTapeMeter();
            static int lastDrawnTapeMeter = -1;
            static bool lastDrawnRec = false;
            static int lastDrawnRoute = -1;
            bool tapeChanged = tapeMeter != lastDrawnTapeMeter ||
                               DspProxy::SdIsRecording() != lastDrawnRec ||
                               DspProxy::GetSdRoute() != lastDrawnRoute;
            lastDrawnTapeMeter = tapeMeter;
            lastDrawnRec = DspProxy::SdIsRecording();
            lastDrawnRoute = DspProxy::GetSdRoute();
            if (memcmp(before, meter, sizeof(meter)) != 0 || tapeChanged) {
                doRedraw();
            }
        }

        void UIMenuPageMix::onEncoder(int delta) {
            if (delta == 0) return;

            int nc = cursor + delta;
            if (nc < 0) nc = 0;
            if (nc >= STRIPS) nc = STRIPS - 1;
            if (nc != cursor) {
                encoderAccel = 0;
                lastEncDir = 0;
            }
            cursor = nc;
            doRedraw();
        }

        // Right encoder: live edit of the strip under the cursor (no edit
        // mode, no confirm — deinit() commits a staged edit when the page is
        // left, OK commits earlier on request). The TAPE fader drives the
        // tape playback gain live; on the IN strip the source switches on
        // OK so the value encoder stays levels-only.
        void UIMenuPageMix::onValueEncoder(int delta) {
            if (delta == 0) return;
            const Strip &st = STRIP_TABLE[cursor];

            // Momentum on consecutive same-direction turns, matching the
            // parameter page, so a full 0..33 sweep is quick.
            uint32_t now = xTaskGetTickCount();
            if (now - lastEncTick > pdMS_TO_TICKS(100)) encoderAccel = 0;
            lastEncTick = now;

            int dir = delta > 0 ? 1 : -1;
            if (dir != lastEncDir) encoderAccel = 0;
            else if (encoderAccel < 10) encoderAccel++;
            lastEncDir = dir;

            int step = delta * (1 + encoderAccel * encoderAccel);
            int v = value[cursor] + step;
            if (v < st.min) v = st.min;
            if (v > st.max) v = st.max;
            const bool changed = (v != value[cursor]);
            value[cursor] = v;
            // The TAPE fader is live-applied, not config-backed — it must
            // not arm the deferred commit.
            if (changed && cursor != TAP_STRIP) dirty = true;

            // Preview only the affected codec registers. Do NOT call
            // applyCurrent() here: that rewrites the whole config and
            // persists it to SPIFFS while the audio mutex is held, which
            // mutes the DSP and blocks the UI for the duration of the
            // flash write. The full config is committed on OK / page leave.
            if (!st.deferred) {
                if (cursor == GAIN_STRIP) {
                    DspProxy::SetInputGain(value[GAIN_STRIP]);
                } else if (cursor == OUT_L_STRIP || cursor == OUT_R_STRIP) {
                    DspProxy::SetOutputLevels(value[OUT_L_STRIP], value[OUT_R_STRIP]);
                } else if (cursor == TAP_STRIP) {
                    DspProxy::SetSdGain(value[TAP_STRIP]);
                }
            }
            doRedraw();
        }

        void UIMenuPageMix::onButton(int btnId, bool longPress) {
            if (btnId == 2 && !longPress) {
                if (cursor == GAIN_STRIP) {
                    // Mic/line switches on OK: advance and commit
                    // immediately — re-routing the codec pops, so it must
                    // happen exactly on the user's press, not staged.
                    source = (source + 1) % InputSource::COUNT;
                    applyCurrent(true);
                    dirty = false;
                } else if (cursor == TAP_STRIP) {
                    // Tape routing on OK, mirroring the IN pattern: the
                    // value encoder on this strip is the level fader.
                    DspProxy::SetSdRoute(DspProxy::GetSdRoute() == 1 ? 0 : 1);
                } else if (dirty) {
                    applyCurrent(true);
                    dirty = false;
                }
            }
            doRedraw();
        }

        bool UIMenuPageMix::onBack() {
            // Metering stays on here: at ROOT this page is still the visible
            // panel and keeps ticking. deinit() disables it on a real panel
            // change.
            return false;
        }
    }
}
