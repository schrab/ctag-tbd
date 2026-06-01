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

#include "UIMenuPageParams.hpp"
#include "Display.hpp"
#include "SPManager.hpp"
#include "CVSlotNames.hpp"
#include "rapidjson/document.h"
#include "esp_heap_caps.h"
#include <cstring>
#include <cstdlib>

using namespace CTAG::DRIVERS;
using namespace CTAG::AUDIO;
using namespace rapidjson;

namespace CTAG {
    namespace CTRL {
        void UIMenuPageParams::init() {
            if (!params) {
                params = (ParamInfo*)heap_caps_malloc(MAX_PARAMS * sizeof(ParamInfo), MALLOC_CAP_SPIRAM);
                groups = (GroupInfo*)heap_caps_malloc(MAX_GROUPS * sizeof(GroupInfo), MALLOC_CAP_SPIRAM);
            }
            cursor = 0;
            scrollOffset = 0;
            mode = MODE_SELECT;
            paramCount = 0;
            groupCount = 0;
            currentGroup = -1;
            mapParamIdx = 0;
            mapEditSlot = -1;
            presetCount = 0;
            presetChan = 0;
            chan = 0;
            string id0 = SoundProcessorManager::GetStringID(0);
            string id1 = SoundProcessorManager::GetStringID(1);
            bool stereoCh0 = SoundProcessorManager::IsPluginStereo(id0);
            hasDualCh = !stereoCh0 && !id0.empty() && !id1.empty() && id0 != id1;
            parseParams();
        }

        void UIMenuPageParams::deinit() {
            if (params) { heap_caps_free(params); params = nullptr; }
            if (groups) { heap_caps_free(groups); groups = nullptr; }
        }

        void UIMenuPageParams::parseParams() {
            paramCount = 0;
            groupCount = 0;
            currentGroup = -1;
            const char *json = SoundProcessorManager::GetCStrJSONActivePluginParams(chan);
            if (!json) return;

            Document doc;
            doc.Parse(json);
            if (!doc.HasMember("params") || !doc["params"].IsArray()) return;

            const Value &arr = doc["params"];

            // First pass: standalone leaf params (not in groups)
            int standaloneStart = paramCount;
            for (SizeType i = 0; i < arr.Size() && paramCount < MAX_PARAMS; i++) {
                const Value &p = arr[i];
                if (!p.HasMember("id") || !p.HasMember("name") || !p.HasMember("type")) continue;
                if (strcmp(p["type"].GetString(), "group") == 0) continue;
                ParamInfo &pi = params[paramCount];
                snprintf(pi.id, sizeof(pi.id), "%s", p["id"].GetString());
                snprintf(pi.name, sizeof(pi.name), "%s", p["name"].GetString());
                snprintf(pi.type, sizeof(pi.type), "%s", p["type"].GetString());
                pi.min = p.HasMember("min") ? p["min"].GetInt() : 0;
                pi.max = p.HasMember("max") ? p["max"].GetInt() : 1;
                pi.current = p.HasMember("current") ? p["current"].GetInt() : 0;
                pi.cv = p.HasMember("cv") ? p["cv"].GetInt() : -1;
                paramCount++;
            }
            int standaloneCount = paramCount - standaloneStart;
            if (standaloneCount > 0 && groupCount < MAX_GROUPS) {
                GroupInfo &g = groups[groupCount++];
                snprintf(g.name, sizeof(g.name), "General");
                g.firstParamIdx = standaloneStart;
                g.paramCount = standaloneCount;
            }

            // Second pass: recurse into groups
            for (SizeType i = 0; i < arr.Size(); i++) {
                const Value &p = arr[i];
                if (!p.HasMember("type") || strcmp(p["type"].GetString(), "group") != 0) continue;
                if (!p.HasMember("params") || !p["params"].IsArray()) continue;
                if (groupCount >= MAX_GROUPS) break;

                GroupInfo &g = groups[groupCount];
                snprintf(g.name, sizeof(g.name), "%s", p["name"].GetString());
                g.firstParamIdx = paramCount;

                const Value &garr = p["params"];
                int count = 0;
                for (SizeType j = 0; j < garr.Size() && paramCount < MAX_PARAMS; j++) {
                    const Value &leaf = garr[j];
                    if (!leaf.HasMember("id") || !leaf.HasMember("name") || !leaf.HasMember("type")) continue;
                    ParamInfo &pi = params[paramCount];
                    snprintf(pi.id, sizeof(pi.id), "%s", leaf["id"].GetString());
                    snprintf(pi.name, sizeof(pi.name), "%s", leaf["name"].GetString());
                    snprintf(pi.type, sizeof(pi.type), "%s", leaf["type"].GetString());
                    pi.min = leaf.HasMember("min") ? leaf["min"].GetInt() : 0;
                    pi.max = leaf.HasMember("max") ? leaf["max"].GetInt() : 1;
                    pi.current = leaf.HasMember("current") ? leaf["current"].GetInt() : 0;
                    pi.cv = leaf.HasMember("cv") ? leaf["cv"].GetInt() : -1;
                    paramCount++;
                    count++;
                }
                g.paramCount = count;
                groupCount++;
            }
        }

        void UIMenuPageParams::parsePresets(int chan) {
            presetCount = 0;
            presetChan = chan;
            const char *json = SoundProcessorManager::GetCStrJSONGetPresets(chan);
            if (!json) return;

            Document doc;
            doc.Parse(json);
            if (!doc.HasMember("presets") || !doc["presets"].IsArray()) return;

            const Value &arr = doc["presets"];
            for (SizeType i = 0; i < arr.Size() && presetCount < MAX_PRESETS; i++) {
                const Value &p = arr[i];
                if (!p.HasMember("name")) continue;
                PresetInfo &pi = presets[presetCount];
                snprintf(pi.name, sizeof(pi.name), "%s", p["name"].GetString());
                pi.number = p.HasMember("number") ? p["number"].GetInt() : i;
                presetCount++;
            }
        }

        int UIMenuPageParams::paramIndexToScreen(int idx) const {
            return idx - scrollOffset;
        }

        int UIMenuPageParams::groupEditEnd() const {
            return (currentGroup >= 0)
                ? groups[currentGroup].firstParamIdx + groups[currentGroup].paramCount
                : paramCount;
        }

        void UIMenuPageParams::onEncoder(int delta) {
            if (mode == MODE_SELECT) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 3) cursor = 3;
            } else if (mode == MODE_GROUP) {
                if (groupCount == 0) return;
                int newCursor = cursor + delta;
                if (newCursor < 0) newCursor = 0;
                if (newCursor >= groupCount) newCursor = groupCount - 1;
                cursor = newCursor;
                ClampScroll(cursor, scrollOffset, groupCount, VISIBLE_ITEMS);
            } else if (mode == MODE_EDIT) {
                if (paramCount == 0) return;
                int endIdx = groupEditEnd();
                int newCursor = cursor + delta;
                int startIdx = (currentGroup >= 0) ? groups[currentGroup].firstParamIdx : 0;
                if (newCursor < startIdx) newCursor = startIdx;
                if (newCursor >= endIdx) newCursor = endIdx - 1;
                cursor = newCursor;

                // auto-scroll
                int scr = paramIndexToScreen(cursor);
                if (scr < 0) scrollOffset += scr;
                if (scr >= VISIBLE_ITEMS) scrollOffset += (scr - (VISIBLE_ITEMS - 1));
                if (scrollOffset > endIdx - VISIBLE_ITEMS) scrollOffset = endIdx - VISIBLE_ITEMS;
                if (scrollOffset < startIdx) scrollOffset = startIdx;
            } else if (mode == MODE_MAP) {
                if (paramCount == 0) return;
                int newSlot = mapEditSlot + delta;
                if (newSlot < -1) newSlot = -1;
                if (newSlot >= N_CVS) newSlot = N_CVS - 1;
                mapEditSlot = newSlot;
            } else if (mode == MODE_VALUEEDIT) {
                if (paramCount == 0 || cursor >= paramCount) return;
                ParamInfo &pi = params[cursor];
                int val = pi.current + delta;
                if (val < pi.min) val = pi.min;
                if (val > pi.max) val = pi.max;
                pi.current = val;
                SoundProcessorManager::SetChannelParamValue(chan, pi.id, "current", val);
            } else if (mode == MODE_PRESETS) {
                if (presetCount == 0) return;
                int newCursor = cursor + delta;
                if (newCursor < 0) newCursor = 0;
                if (newCursor >= presetCount) newCursor = presetCount - 1;
                cursor = newCursor;
                // auto-scroll
                ClampScroll(cursor, scrollOffset, presetCount, VISIBLE_ITEMS);
            }
            doRedraw();
        }

        void UIMenuPageParams::onButton(int btnId, bool longPress) {
            if (mode == MODE_SELECT) {
                if (btnId == 2 && !longPress) {
                    // enter sub-mode
                    if (cursor == 0) {
                        chan = (hasDualCh ? 0 : 0);
                        parseParams();
                        if (groupCount == 1) {
                            currentGroup = 0;
                            cursor = groups[0].firstParamIdx;
                            scrollOffset = groups[0].firstParamIdx;
                            mode = MODE_EDIT;
                        } else if (groupCount > 1) {
                            mode = MODE_GROUP;
                            cursor = 0;
                            scrollOffset = 0;
                        } else {
                            mode = MODE_EDIT;
                            cursor = 0;
                            scrollOffset = 0;
                            currentGroup = -1;
                        }
                    }
                    else if (cursor == 1) {
                        if (hasDualCh) {
                            chan = 1;
                            parseParams();
                            if (groupCount == 1) {
                                currentGroup = 0;
                                cursor = groups[0].firstParamIdx;
                                scrollOffset = groups[0].firstParamIdx;
                                mode = MODE_EDIT;
                            } else if (groupCount > 1) {
                                mode = MODE_GROUP;
                                cursor = 0;
                                scrollOffset = 0;
                            } else {
                                mode = MODE_EDIT;
                                cursor = 0;
                                scrollOffset = 0;
                                currentGroup = -1;
                            }
                        } else {
                            mode = MODE_MAP;
                        }
                    }
                    else if (cursor == 2) { mode = MODE_PSET; }
                    else if (cursor == 3) {
                        mode = MODE_PRESETS;
                        cursor = 0;
                        scrollOffset = 0;
                        parsePresets(chan);
                    }
                }
            } else if (mode == MODE_GROUP) {
                if (btnId == 2 && !longPress && groupCount > 0) {
                    currentGroup = cursor;
                    cursor = groups[currentGroup].firstParamIdx;
                    scrollOffset = groups[currentGroup].firstParamIdx;
                    mode = MODE_EDIT;
                }
            } else if (mode == MODE_EDIT) {
                if (btnId == 2 && !longPress && paramCount > 0) {
                    // enter value edit for selected param
                    mode = MODE_VALUEEDIT;
                } else if (btnId == 2 && longPress && paramCount > 0) {
                    // long press: enter CV mapping for this param
                    mapParamIdx = cursor;
                    mapEditSlot = params[cursor].cv;
                    mode = MODE_MAP;
                }
            } else if (mode == MODE_MAP) {
                if (btnId == 2 && !longPress) {
                    params[mapParamIdx].cv = mapEditSlot;
                    SoundProcessorManager::SetChannelParamValue(chan, params[mapParamIdx].id, "cv", mapEditSlot);
                    mode = MODE_EDIT;
                }
            } else if (mode == MODE_VALUEEDIT) {
                if (btnId == 2 && !longPress) {
                    mode = MODE_EDIT;
                }
                // BTN2_LONG / BTN1 handled by UIMenu/TaskFunction
            } else if (mode == MODE_PRESETS) {
                if (btnId == 2 && !longPress) {
                    // load selected preset
                    if (presetCount > 0 && cursor >= 0 && cursor < presetCount) {
                        SoundProcessorManager::ChannelLoadPreset(presetChan, presets[cursor].number);
                        mode = MODE_SELECT;
                        cursor = 0;
                    }
                } else if (btnId == 2 && longPress) {
                    // save current as new preset
                    if (presetCount > 0) {
                        int nextNum = presets[presetCount - 1].number + 1;
                        SoundProcessorManager::ChannelSavePreset(presetChan, "User", nextNum);
                        // re-parse to show updated list
                        parsePresets(presetChan);
                    }
                }
            }
            doRedraw();
        }

        bool UIMenuPageParams::onBack() {
            if (mode == MODE_VALUEEDIT) {
                mode = MODE_EDIT;
                doRedraw();
                return true;
            }
            if (mode == MODE_EDIT) {
                if (currentGroup >= 0) {
                    mode = MODE_GROUP;
                    cursor = currentGroup;
                    scrollOffset = 0;
                    currentGroup = -1;
                } else {
                    mode = MODE_SELECT;
                    cursor = 0;
                }
                doRedraw();
                return true;
            }
            if (mode == MODE_GROUP) {
                mode = MODE_SELECT;
                cursor = 0;
                doRedraw();
                return true;
            }
            if (mode == MODE_MAP) {
                mode = MODE_EDIT;
                doRedraw();
                return true;
            }
            if (mode == MODE_PSET) {
                mode = MODE_SELECT;
                cursor = 0;
                doRedraw();
                return true;
            }
            if (mode == MODE_PRESETS) {
                mode = MODE_SELECT;
                cursor = 0;
                doRedraw();
                return true;
            }
            return false;
        }

        void UIMenuPageParams::doRedraw() {
            if (mode == MODE_SELECT) {
                redrawSelect();
            } else if (mode == MODE_GROUP) {
                redrawGroup();
            } else if (mode == MODE_EDIT) {
                redrawEdit();
            } else if (mode == MODE_VALUEEDIT) {
                redrawEdit(); // same layout, cursor highlight acts as indicator
            } else if (mode == MODE_MAP) {
                redrawMap();
            } else if (mode == MODE_PRESETS) {
                redrawPresets();
            } else {
                // PSET — simple placeholder
                Display::Clear();
                Display::DrawString(0, 16, "Not implemented", Display::FONT_5X7);
                Display::Flush();
            }
        }

        void UIMenuPageParams::redrawSelect() {
            Display::Clear();
            if (hasDualCh) {
                string id0 = SoundProcessorManager::GetStringID(0);
                string id1 = SoundProcessorManager::GetStringID(1);
                char buf[26];
                snprintf(buf, sizeof(buf), "Ch0:%.*s", 21, id0.c_str());
                Display::DrawString(0, ROW(0), buf, Display::FONT_5X7);
                snprintf(buf, sizeof(buf), "Ch1:%.*s", 21, id1.c_str());
                Display::DrawString(0, ROW(1), buf, Display::FONT_5X7);
                Display::DrawString(0, ROW(2), "MAP", Display::FONT_5X7);
                Display::DrawString(0, ROW(3), "PRESETS", Display::FONT_5X7);
            } else {
                Display::DrawString(0, ROW(0), "EDIT", Display::FONT_5X7);
                Display::DrawString(0, ROW(1), "MAP", Display::FONT_5X7);
                Display::DrawString(0, ROW(2), "PSET", Display::FONT_5X7);
                Display::DrawString(0, ROW(3), "PRESETS", Display::FONT_5X7);
            }
            Display::InvertRect(0, ROW(cursor), 128, 8);
            Display::Flush();
        }

        void UIMenuPageParams::redrawGroup() {
            Display::Clear();
            if (groupCount == 0) {
                Display::DrawString(0, 24, "No groups", Display::FONT_5X7);
                Display::Flush();
                return;
            }
            int visible = ClampVisible(groupCount, scrollOffset, VISIBLE_ITEMS);
            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                const GroupInfo &g = groups[idx];
                int y = ROW(i);
                char buf[32];
                snprintf(buf, sizeof(buf), "%s (%d)", g.name, g.paramCount);
                Display::DrawString(0, y, buf, Display::FONT_5X7);
            }
            int cy = ROW(cursor - scrollOffset);
            Display::InvertRect(0, cy, 128, 8);
            if (groupCount > VISIBLE_ITEMS)
                Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0, VISIBLE_ITEMS * LINE_H, groupCount, cursor);
            Display::Flush();
        }

        void UIMenuPageParams::redrawEdit() {
            Display::Clear();
            if (paramCount == 0) {
                Display::DrawString(0, 24, "No params", Display::FONT_5X7);
                Display::Flush();
                return;
            }
            int endIdx = groupEditEnd();
            int startIdx = (currentGroup >= 0) ? groups[currentGroup].firstParamIdx : 0;
            int count = endIdx - startIdx;
            int visible = ClampVisible(count, scrollOffset - startIdx, VISIBLE_ITEMS);

            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                if (idx >= endIdx) break;
                const ParamInfo &pi = params[idx];
                int y = ROW(i);
                Display::DrawString(0, y, pi.name, Display::FONT_5X7);
                char valBuf[16];
                if (strcmp(pi.type, "bool") == 0) {
                    strcpy(valBuf, pi.current ? "ON" : "OFF");
                } else if (strcmp(pi.type, "enum") == 0) {
                    snprintf(valBuf, sizeof(valBuf), "%d", pi.current);
                } else {
                    snprintf(valBuf, sizeof(valBuf), "%d", pi.current);
                }
                Display::DrawStringRight(127, y, valBuf, Display::FONT_5X7);
            }
            int cursorY = ROW(paramIndexToScreen(cursor));
            Display::InvertRect(0, cursorY, 128, 8);
            if (count > VISIBLE_ITEMS)
                Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0, VISIBLE_ITEMS * LINE_H, count, cursor - startIdx);
            Display::Flush();
        }

        void UIMenuPageParams::redrawMap() {
            Display::Clear();
            if (paramCount == 0) return;
            Display::DrawString(0, ROW(0), "MAPPING", Display::FONT_5X7);
            const ParamInfo &pi = params[mapParamIdx];
            Display::DrawString(0, ROW_HDR(0), pi.name, Display::FONT_5X7);
            Display::DrawString(0, ROW_HDR(1), "---", Display::FONT_5X7);
            char buf[32];
            if (mapEditSlot < 0) {
                snprintf(buf, sizeof(buf), "CV: None");
            } else if (mapEditSlot < 100) {
                snprintf(buf, sizeof(buf), "CV: %s", cvSlotDisplayNames[mapEditSlot]);
            } else {
                snprintf(buf, sizeof(buf), "CV: %d", mapEditSlot);
            }
            Display::DrawString(0, ROW_HDR(2), buf, Display::FONT_5X7);
            Display::InvertRect(0, ROW_HDR(2), 128, 8);
            Display::DrawString(0, ROW_HDR(3), "OK=save BACK=exit", Display::FONT_5X7);
            Display::Flush();
        }

        void UIMenuPageParams::redrawPresets() {
            Display::Clear();
            if (presetCount == 0) {
                Display::DrawString(0, 24, "No presets", Display::FONT_5X7);
                Display::Flush();
                return;
            }
            int visible = ClampVisible(presetCount, scrollOffset, VISIBLE_ITEMS);
            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                const PresetInfo &pi = presets[idx];
                int y = ROW(i);
                char buf[64];
                snprintf(buf, sizeof(buf), "%s", pi.name);
                Display::DrawString(0, y, buf, Display::FONT_5X7);
            }
            int cy = ROW(cursor - scrollOffset);
            Display::InvertRect(0, cy, 128, 8);
            if (presetCount > VISIBLE_ITEMS)
                Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0, VISIBLE_ITEMS * LINE_H, presetCount, cursor);
            Display::Flush();
        }
    }
}
