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

#include "UIMenuPageSystem.hpp"
#include "Display.hpp"
#include "SPManager.hpp"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <cstring>
#include <cstdio>

using namespace CTAG::DRIVERS;
using namespace CTAG::AUDIO;
using namespace rapidjson;

namespace CTAG {
    namespace CTRL {
        void UIMenuPageSystem::init() {
            cursor = 0;
            scrollOffset = 0;
            itemCount = 0;
            editMode = false;
            parseConfig();
        }

        void UIMenuPageSystem::deinit() {
            if (itemCount > 0) applyCurrent();
        }

        void UIMenuPageSystem::parseConfig() {
            itemCount = 0;
            const char *json = SoundProcessorManager::GetCStrJSONConfiguration();
            if (!json) return;

            Document doc;
            doc.Parse(json);
            if (!doc.IsObject()) return;

            // ng_config
            if (itemCount < MAX_ITEMS) {
                ConfigItem &it = items[itemCount];
                snprintf(it.id, sizeof(it.id), "ng_config");
                snprintf(it.name, sizeof(it.name), "Noise Gate");
                snprintf(it.type, sizeof(it.type), "enum");
                snprintf(it.options, sizeof(it.options), "off,dual,ch0,ch1");
                it.min = 0; it.max = 3; it.valInt = 0;
                if (doc.HasMember("ng_config") && doc["ng_config"].IsString()) {
                    snprintf(it.value, sizeof(it.value), "%s", doc["ng_config"].GetString());
                    const char *opts[] = {"off","dual","ch0","ch1"};
                    for (int j = 0; j < 4; j++) {
                        if (strcmp(it.value, opts[j]) == 0) { it.valInt = j; break; }
                    }
                }
                itemCount++;
            }

            // ch01_daisy
            if (itemCount < MAX_ITEMS) {
                ConfigItem &it = items[itemCount];
                snprintf(it.id, sizeof(it.id), "ch01_daisy");
                snprintf(it.name, sizeof(it.name), "Ch 0+1 Daisy");
                snprintf(it.type, sizeof(it.type), "bool");
                snprintf(it.options, sizeof(it.options), "off,on");
                it.min = 0; it.max = 1; it.valInt = 0;
                if (doc.HasMember("ch01_daisy") && doc["ch01_daisy"].IsString()) {
                    snprintf(it.value, sizeof(it.value), "%s", doc["ch01_daisy"].GetString());
                    it.valInt = strcmp(it.value, "on") == 0 ? 1 : 0;
                }
                itemCount++;
            }

            // ch0_toStereo
            if (itemCount < MAX_ITEMS) {
                ConfigItem &it = items[itemCount];
                snprintf(it.id, sizeof(it.id), "ch0_toStereo");
                snprintf(it.name, sizeof(it.name), "Ch0→Stereo");
                snprintf(it.type, sizeof(it.type), "enum");
                snprintf(it.options, sizeof(it.options), "off,on,mix");
                it.min = 0; it.max = 2; it.valInt = 0;
                if (doc.HasMember("ch0_toStereo") && doc["ch0_toStereo"].IsString()) {
                    snprintf(it.value, sizeof(it.value), "%s", doc["ch0_toStereo"].GetString());
                    const char *opts[] = {"off","on","mix"};
                    for (int j = 0; j < 3; j++) {
                        if (strcmp(it.value, opts[j]) == 0) { it.valInt = j; break; }
                    }
                }
                itemCount++;
            }

            // ch1_toStereo
            if (itemCount < MAX_ITEMS) {
                ConfigItem &it = items[itemCount];
                snprintf(it.id, sizeof(it.id), "ch1_toStereo");
                snprintf(it.name, sizeof(it.name), "Ch1→Stereo");
                snprintf(it.type, sizeof(it.type), "enum");
                snprintf(it.options, sizeof(it.options), "off,on,mix");
                it.min = 0; it.max = 2; it.valInt = 0;
                if (doc.HasMember("ch1_toStereo") && doc["ch1_toStereo"].IsString()) {
                    snprintf(it.value, sizeof(it.value), "%s", doc["ch1_toStereo"].GetString());
                    const char *opts[] = {"off","on","mix"};
                    for (int j = 0; j < 3; j++) {
                        if (strcmp(it.value, opts[j]) == 0) { it.valInt = j; break; }
                    }
                }
                itemCount++;
            }

            // ch0_outputSoftClip
            if (itemCount < MAX_ITEMS) {
                ConfigItem &it = items[itemCount];
                snprintf(it.id, sizeof(it.id), "ch0_outputSoftClip");
                snprintf(it.name, sizeof(it.name), "Ch0 Soft Clip");
                snprintf(it.type, sizeof(it.type), "bool");
                snprintf(it.options, sizeof(it.options), "off,on");
                it.min = 0; it.max = 1; it.valInt = 0;
                if (doc.HasMember("ch0_outputSoftClip") && doc["ch0_outputSoftClip"].IsString()) {
                    snprintf(it.value, sizeof(it.value), "%s", doc["ch0_outputSoftClip"].GetString());
                    it.valInt = strcmp(it.value, "on") == 0 ? 1 : 0;
                }
                itemCount++;
            }

            // ch1_outputSoftClip
            if (itemCount < MAX_ITEMS) {
                ConfigItem &it = items[itemCount];
                snprintf(it.id, sizeof(it.id), "ch1_outputSoftClip");
                snprintf(it.name, sizeof(it.name), "Ch1 Soft Clip");
                snprintf(it.type, sizeof(it.type), "bool");
                snprintf(it.options, sizeof(it.options), "off,on");
                it.min = 0; it.max = 1; it.valInt = 0;
                if (doc.HasMember("ch1_outputSoftClip") && doc["ch1_outputSoftClip"].IsString()) {
                    snprintf(it.value, sizeof(it.value), "%s", doc["ch1_outputSoftClip"].GetString());
                    it.valInt = strcmp(it.value, "on") == 0 ? 1 : 0;
                }
                itemCount++;
            }

            // ch0_codecLvlOut
            if (itemCount < MAX_ITEMS) {
                ConfigItem &it = items[itemCount];
                snprintf(it.id, sizeof(it.id), "ch0_codecLvlOut");
                snprintf(it.name, sizeof(it.name), "Ch0 Out Level");
                snprintf(it.type, sizeof(it.type), "int");
                it.options[0] = '\0';
                it.min = 0; it.max = 63; it.valInt = 58;
                if (doc.HasMember("ch0_codecLvlOut") && doc["ch0_codecLvlOut"].IsString()) {
                    snprintf(it.value, sizeof(it.value), "%s", doc["ch0_codecLvlOut"].GetString());
                    it.valInt = atoi(it.value);
                }
                itemCount++;
            }

            // ch1_codecLvlOut
            if (itemCount < MAX_ITEMS) {
                ConfigItem &it = items[itemCount];
                snprintf(it.id, sizeof(it.id), "ch1_codecLvlOut");
                snprintf(it.name, sizeof(it.name), "Ch1 Out Level");
                snprintf(it.type, sizeof(it.type), "int");
                it.options[0] = '\0';
                it.min = 0; it.max = 63; it.valInt = 58;
                if (doc.HasMember("ch1_codecLvlOut") && doc["ch1_codecLvlOut"].IsString()) {
                    snprintf(it.value, sizeof(it.value), "%s", doc["ch1_codecLvlOut"].GetString());
                    it.valInt = atoi(it.value);
                }
                itemCount++;
            }

            // input_source
            if (itemCount < MAX_ITEMS) {
                ConfigItem &it = items[itemCount];
                snprintf(it.id, sizeof(it.id), "input_source");
                snprintf(it.name, sizeof(it.name), "Input Source");
                snprintf(it.type, sizeof(it.type), "enum");
                snprintf(it.options, sizeof(it.options), "line1,line2,line1_diff,line2_diff");
                it.min = 0; it.max = 3; it.valInt = 1; it.deferred = true;
                if (doc.HasMember("input_source") && doc["input_source"].IsString()) {
                    snprintf(it.value, sizeof(it.value), "%s", doc["input_source"].GetString());
                    const char *opts[] = {"line1","line2","line1_diff","line2_diff"};
                    for (int j = 0; j < 4; j++) {
                        if (strcmp(it.value, opts[j]) == 0) { it.valInt = j; break; }
                    }
                }
                itemCount++;
            }

            // input_gain
            if (itemCount < MAX_ITEMS) {
                ConfigItem &it = items[itemCount];
                snprintf(it.id, sizeof(it.id), "input_gain");
                snprintf(it.name, sizeof(it.name), "Input Gain");
                snprintf(it.type, sizeof(it.type), "int");
                it.options[0] = '\0';
                it.min = 0; it.max = 8; it.valInt = 0; it.deferred = true;
                if (doc.HasMember("input_gain") && doc["input_gain"].IsString()) {
                    snprintf(it.value, sizeof(it.value), "%s", doc["input_gain"].GetString());
                    it.valInt = atoi(it.value);
                }
                itemCount++;
            }

            // output_source
            if (itemCount < MAX_ITEMS) {
                ConfigItem &it = items[itemCount];
                snprintf(it.id, sizeof(it.id), "output_source");
                snprintf(it.name, sizeof(it.name), "Output Route");
                snprintf(it.type, sizeof(it.type), "enum");
                snprintf(it.options, sizeof(it.options), "hp,amp,all");
                it.min = 0; it.max = 2; it.valInt = 2; it.deferred = true;
                if (doc.HasMember("output_source") && doc["output_source"].IsString()) {
                    snprintf(it.value, sizeof(it.value), "%s", doc["output_source"].GetString());
                    const char *opts[] = {"hp","amp","all"};
                    for (int j = 0; j < 3; j++) {
                        if (strcmp(it.value, opts[j]) == 0) { it.valInt = j; break; }
                    }
                }
                itemCount++;
            }

            // mixer_mode
            if (itemCount < MAX_ITEMS) {
                ConfigItem &it = items[itemCount];
                snprintf(it.id, sizeof(it.id), "mixer_mode");
                snprintf(it.name, sizeof(it.name), "Mixer Mode");
                snprintf(it.type, sizeof(it.type), "enum");
                snprintf(it.options, sizeof(it.options), "dac,bypass,mix");
                it.min = 0; it.max = 2; it.valInt = 0; it.deferred = true;
                if (doc.HasMember("mixer_mode") && doc["mixer_mode"].IsString()) {
                    snprintf(it.value, sizeof(it.value), "%s", doc["mixer_mode"].GetString());
                    const char *opts[] = {"dac","bypass","mix"};
                    for (int j = 0; j < 3; j++) {
                        if (strcmp(it.value, opts[j]) == 0) { it.valInt = j; break; }
                    }
                }
                itemCount++;
            }

            // oled_brightness
            if (itemCount < MAX_ITEMS) {
                ConfigItem &it = items[itemCount];
                snprintf(it.id, sizeof(it.id), "oled_brightness");
                snprintf(it.name, sizeof(it.name), "OLED Brightness");
                snprintf(it.type, sizeof(it.type), "int");
                it.options[0] = '\0';
                it.min = 0; it.max = 255; it.valInt = 255; it.deferred = true;
                if (doc.HasMember("oled_brightness") && doc["oled_brightness"].IsString()) {
                    snprintf(it.value, sizeof(it.value), "%s", doc["oled_brightness"].GetString());
                    it.valInt = atoi(it.value);
                }
                itemCount++;
            }
        }

        void UIMenuPageSystem::applyCurrent(bool includeDeferred) {
            // Read the full existing config, overlay only our managed keys
            const char *fullJson = SoundProcessorManager::GetCStrJSONConfiguration();
            if (!fullJson) return;
            Document doc;
            doc.Parse(fullJson);
            if (!doc.IsObject()) return;

            for (int i = 0; i < itemCount; i++) {
                const ConfigItem &it = items[i];
                if (it.deferred && !includeDeferred) continue;
                Value key(it.id, doc.GetAllocator());
                char valStr[16];
                if (strcmp(it.type, "int") == 0) {
                    snprintf(valStr, sizeof(valStr), "%d", it.valInt);
                    Value v(valStr, doc.GetAllocator());
                    doc[it.id].Swap(v);
                } else {
                    char optCopy[64];
                    snprintf(optCopy, sizeof(optCopy), "%s", it.options);
                    char *p = strtok(optCopy, ",");
                    int idx = it.valInt;
                    while (p && idx > 0) { p = strtok(nullptr, ","); idx--; }
                    Value v(p ? p : "off", doc.GetAllocator());
                    doc[it.id].Swap(v);
                }
            }

            StringBuffer buf;
            Writer<StringBuffer> writer(buf);
            doc.Accept(writer);
            SoundProcessorManager::SetConfigurationFromJSON(buf.GetString());
        }

        void UIMenuPageSystem::onEncoder(int delta) {
            if (editMode) {
                if (itemCount == 0 || cursor >= itemCount) return;
                ConfigItem &it = items[cursor];
                int step = 1;
                if (strcmp(it.type, "int") == 0 && (it.max - it.min) > 20) {
                    step = max(1, (it.max - it.min) / 16);
                }
                it.valInt += delta * step;
                if (it.valInt < it.min) it.valInt = it.min;
                if (it.valInt > it.max) it.valInt = it.max;
                // update value string
                if (strcmp(it.type, "int") == 0) {
                    snprintf(it.value, sizeof(it.value), "%d", it.valInt);
                } else {
                    char optCopy[64];
                    snprintf(optCopy, sizeof(optCopy), "%s", it.options);
                    char *p = strtok(optCopy, ",");
                    int idx = it.valInt;
                    while (p && idx > 0) { p = strtok(nullptr, ","); idx--; }
                    snprintf(it.value, sizeof(it.value), "%s", p ? p : "off");
                }
                // real-time items applied immediately; deferred items wait for OK/BACK
                applyCurrent(false);
            } else {
                int nc = cursor + delta;
                if (nc < 0) nc = 0;
                if (nc >= itemCount) nc = itemCount - 1;
                cursor = nc;
                if (cursor - scrollOffset < 0) scrollOffset = cursor;
                if (cursor - scrollOffset >= 7) scrollOffset = cursor - 6;
                if (scrollOffset > itemCount - 7) scrollOffset = itemCount - 7;
                if (scrollOffset < 0) scrollOffset = 0;
            }
            doRedraw();
        }

        void UIMenuPageSystem::onButton(int btnId, bool longPress) {
            if (btnId == 2 && !longPress && itemCount > 0) {
                bool wasEditing = editMode;
                editMode = !editMode;
                if (wasEditing) applyCurrent();
            }
            doRedraw();
        }

        bool UIMenuPageSystem::onBack() {
            if (editMode) {
                editMode = false;
                applyCurrent();
                doRedraw();
                return true;
            }
            return false;
        }

        void UIMenuPageSystem::doRedraw() {
            redrawMain();
        }

        void UIMenuPageSystem::redrawMain() {
            Display::Clear();
            if (itemCount == 0) {
                Display::DrawString(0, 24, "No config", Display::FONT_5X7);
                Display::Flush();
                return;
            }
            int visible = itemCount - scrollOffset;
            if (visible > 7) visible = 7;
            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                const ConfigItem &it = items[idx];
                int y = 5 + i * 8;
                Display::DrawString(0, y, it.name, Display::FONT_5X7);
                Display::DrawStringRight(127, y, it.value, Display::FONT_5X7);
            }
            int cy = 5 + (cursor - scrollOffset) * 8;
            if (editMode) {
                // highlight only value area when editing
                Display::InvertRect(104, cy, 24, 8);
            } else {
                Display::InvertRect(0, cy, 128, 8);
            }
            if (itemCount > 7)
                Display::DrawScrollbar(126, 5, 56, itemCount, cursor);
            Display::Flush();
        }
    }
}
