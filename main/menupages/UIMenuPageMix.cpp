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
#include "SPManager.hpp"
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
        // Strip order: input gain, input source, output L, output R. The level
        // strips re-apply on every turn so the faders feel live; the source
        // switch is deferred because re-routing the codec mid-edit pops.
        const UIMenuPageMix::Strip UIMenuPageMix::STRIP_TABLE[] = {
                {"IN",  "input_gain",      0, 8,             false, nullptr,      METER_IN},
                {"SRC", "input_source",    0, 1,             true,  "mic,line", METER_NONE},
                {"O L", "ch0_codecLvlOut", 0, CODEC_LVL_MAX, false, nullptr,      METER_OUT},
                {"O R", "ch1_codecLvlOut", 0, CODEC_LVL_MAX, false, nullptr,      METER_OUT},
        };

        // input_source has more spellings in the wild than the two the System
        // page lists: SPManager::updateConfiguration accepts mic/line1 as the
        // mono input and line/line2 as the stereo input, and existing SPIFFS
        // configs carry the latter. Map every accepted alias onto the two
        // canonical options so the strip shows the truth and committing it
        // cannot silently re-route the hardware to the wrong input.
        static int inputSourceIndex(const char *s) {
            if (!s) return -1;
            if (strcmp(s, "mic") == 0 || strcmp(s, "line1") == 0) return 0;
            if (strcmp(s, "line") == 0 || strcmp(s, "line2") == 0) return 1;
            return -1;
        }

        void UIMenuPageMix::DebugStateJson(std::string &out) {
            out = "{\"cursor\":" + to_string(cursor)
                  + ",\"edit\":" + (editMode ? "true" : "false")
                  + ",\"values\":[";
            for (int i = 0; i < STRIPS; i++) {
                if (i) out += ",";
                out += to_string(value[i]);
            }
            out += "],\"max\":" + to_string(CODEC_LVL_MAX) + "}";
        }

        void UIMenuPageMix::init() {
            cursor = 0;
            editMode = false;
            encoderAccel = 0;
            lastEncDir = 0;
            lastEncTick = 0;
            for (int i = 0; i < STRIPS; i++) {
                value[i] = STRIP_TABLE[i].min;
                meter[i][0] = 0;
                meter[i][1] = 0;
                hold[i][0] = 0;
                hold[i][1] = 0;
            }
            parseConfig();
        }

        void UIMenuPageMix::deinit() {
            SoundProcessorManager::SetMeteringEnabled(false);
            // Persist whatever is staged (including a deferred source switch
            // the user left in edit mode on).
            applyCurrent(true);
        }

        void UIMenuPageMix::parseConfig() {
            for (int i = 0; i < STRIPS; i++) value[i] = STRIP_TABLE[i].min;

            const char *json = SoundProcessorManager::GetCStrJSONConfiguration();
            if (!json) return;

            Document doc;
            doc.Parse(json);
            if (!doc.IsObject()) return;

            for (int i = 0; i < STRIPS; i++) {
                const Strip &st = STRIP_TABLE[i];
                if (!doc.HasMember(st.id) || !doc[st.id].IsString()) continue;
                const char *s = doc[st.id].GetString();

                if (st.enumOpts) {
                    // input_source needs the alias table; other enums match the
                    // option list literally.
                    if (i == SRC_STRIP) {
                        int idx = inputSourceIndex(s);
                        if (idx >= 0) value[i] = idx;
                        continue;
                    }
                    // Match against the option list; unknown values keep the min.
                    char optCopy[32];
                    snprintf(optCopy, sizeof(optCopy), "%s", st.enumOpts);
                    int idx = 0;
                    const char *p = optCopy;
                    bool matched = false;
                    // walk comma separated options without strtok (no need for
                    // a second pass, keeps index in sync with the stored value)
                    while (p != nullptr && *p != '\0') {
                        const char *comma = strchr(p, ',');
                        size_t len = comma ? (size_t)(comma - p) : strlen(p);
                        if (len == strlen(s) && strncmp(p, s, len) == 0) {
                            matched = true;
                            break;
                        }
                        if (!comma) break;
                        p = comma + 1;
                        idx++;
                    }
                    if (matched) value[i] = idx;
                } else {
                    int v = atoi(s);
                    if (v < st.min) v = st.min;
                    if (v > st.max) v = st.max;
                    value[i] = v;
                }
            }
        }

        void UIMenuPageMix::valueText(int i, char *buf, size_t len) const {
            const Strip &st = STRIP_TABLE[i];
            if (st.enumOpts) {
                char optCopy[32];
                snprintf(optCopy, sizeof(optCopy), "%s", st.enumOpts);
                int idx = 0;
                const char *p = optCopy;
                while (p != nullptr && *p != '\0' && idx < value[i]) {
                    const char *comma = strchr(p, ',');
                    if (!comma) break;
                    p = comma + 1;
                    idx++;
                }
                if (p == nullptr || *p == '\0') snprintf(buf, len, "off");
                else {
                    size_t optLen = strcspn(p, ",");
                    snprintf(buf, len, "%.*s", (int)optLen, p);
                }
            } else {
                snprintf(buf, len, "%d", value[i]);
            }
        }

        void UIMenuPageMix::applyCurrent(bool includeDeferred) {
            const char *fullJson = SoundProcessorManager::GetCStrJSONConfiguration();
            if (!fullJson) return;
            Document doc;
            doc.Parse(fullJson);
            if (!doc.IsObject()) return;

            for (int i = 0; i < STRIPS; i++) {
                const Strip &st = STRIP_TABLE[i];
                if (st.deferred && !includeDeferred) continue;

                char valStr[16];
                if (st.enumOpts) {
                    char tmp[16];
                    valueText(i, tmp, sizeof(tmp));
                    snprintf(valStr, sizeof(valStr), "%s", tmp);
                } else {
                    snprintf(valStr, sizeof(valStr), "%d", value[i]);
                }

                Value v(valStr, doc.GetAllocator());
                if (doc.HasMember(st.id)) {
                    doc[st.id].Swap(v);
                } else {
                    Value key(st.id, doc.GetAllocator());
                    doc.AddMember(key, v, doc.GetAllocator());
                }
            }

            StringBuffer buf;
            Writer<StringBuffer> writer(buf);
            doc.Accept(writer);
            SoundProcessorManager::SetConfigurationFromJSON(buf.GetString());
        }

        void UIMenuPageMix::refreshMeters() {
            for (int i = 0; i < STRIPS; i++) {
                MeterSrc src = STRIP_TABLE[i].meter;
                for (int ch = 0; ch < 2; ch++) {
                    int raw = 0;
                    if (src == METER_IN) {
                        raw = (int)SoundProcessorManager::GetVUPeak(ch); // 0=L, 1=R
                    } else if (src == METER_OUT) {
                        raw = (int)SoundProcessorManager::GetVUPeak(ch + 2); // 2=outL, 3=outR
                    }
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

        void UIMenuPageMix::drawFader(int i) {
            const Strip &st = STRIP_TABLE[i];
            int x = STRIP_X0 + i * STRIP_W + 3;
            // track
            Display::DrawRect(x, FADER_Y, FADER_W, FADER_H, false, true);
            // value height, min 1px so a zero setting still shows the cap
            int range = st.max - st.min;
            int fillH = range > 0 ? ((value[i] - st.min) * (FADER_H - 2)) / range : 0;
            if (fillH < 1) fillH = 1;
            if (fillH > FADER_H - 2) fillH = FADER_H - 2;
            Display::DrawRect(x + 1, FADER_Y + FADER_H - 1 - fillH, FADER_W - 2, fillH, true, true);
            if (i == cursor && editMode) Display::InvertRect(x - 1, FADER_Y - 1, FADER_W + 2, FADER_H + 2);
        }

        void UIMenuPageMix::drawMeters(int i) {
            if (STRIP_TABLE[i].meter == METER_NONE) return;
            int bx = STRIP_X0 + i * STRIP_W + 13;
            for (int ch = 0; ch < 2; ch++) {
                float lvl = (float)meter[i][ch] / 1000.0f;
                Display::DrawVUMeterV(bx + ch * (VU_W + 2), FADER_Y, VU_W, VU_H, lvl);
            }
        }

        void UIMenuPageMix::drawStrip(int i) {
            const Strip &st = STRIP_TABLE[i];
            int x = STRIP_X0 + i * STRIP_W;

            Display::DrawString(x, LABEL_Y, st.label, Display::FONT_5X7);
            if (i == cursor && !editMode) {
                // whole-strip selection highlight, meters stay readable
                Display::DrawRect(x - 1, LABEL_Y - 1, STRIP_W - 2, FONT_H + 2, false, true);
            }

            drawFader(i);
            drawMeters(i);

            char val[16];
            valueText(i, val, sizeof(val));
            Display::DrawString(x, VALUE_Y, val, Display::FONT_5X7);
        }

        void UIMenuPageMix::doRedraw() {
            Display::Clear();
            Display::DrawString(0, 0, "MIX", Display::FONT_5X7);
            // source + edit hint on the right of the header
            char val[16];
            valueText(1, val, sizeof(val));
            Display::DrawStringRight(127, 0, editMode ? "EDIT" : val, Display::FONT_5X7);
            for (int i = 0; i < STRIPS; i++) drawStrip(i);
            Display::Flush();
        }

        void UIMenuPageMix::onTick() {
            // Metering is armed here rather than in init(): UIMenu::Init()
            // initialises *every* page at boot, so enabling in init() would
            // leave the output peak pass running from boot until the user
            // visits MIX and navigates away. onTick() only fires while this
            // page is the active PANEL_IN page.
            SoundProcessorManager::SetMeteringEnabled(true);
            // Pumped by UIMenu at ~4 Hz while PANEL_IN. Re-sample the peaks and
            // repaint so the meters move even with the encoder idle.
            refreshMeters();
            doRedraw();
        }

        void UIMenuPageMix::onEncoder(int delta) {
            if (delta == 0) return;

            if (editMode) {
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
                value[cursor] = v;

                if (!st.deferred) applyCurrent(false);
            } else {
                int nc = cursor + delta;
                if (nc < 0) nc = 0;
                if (nc >= STRIPS) nc = STRIPS - 1;
                if (nc != cursor) {
                    encoderAccel = 0;
                    lastEncDir = 0;
                }
                cursor = nc;
            }
            doRedraw();
        }

        void UIMenuPageMix::onButton(int btnId, bool longPress) {
            if (btnId == 2 && !longPress) {
                bool wasEditing = editMode;
                editMode = !editMode;
                encoderAccel = 0;
                lastEncDir = 0;
                if (wasEditing) applyCurrent(true);
            }
            doRedraw();
        }

        bool UIMenuPageMix::onBack() {
            if (editMode) {
                editMode = false;
                encoderAccel = 0;
                lastEncDir = 0;
                applyCurrent(true);
                doRedraw();
                return true;
            }
            // Returning to ROOT. deinit() is not called here (it only runs on a
            // panel change), so drop metering explicitly — otherwise the audio
            // task keeps paying for output peak capture while MIX is off-screen.
            SoundProcessorManager::SetMeteringEnabled(false);
            return false;
        }
    }
}
