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
#include "rapidjson/document.h"
#include <cstring>
#include <cstdlib>

using namespace CTAG::DRIVERS;
using namespace CTAG::AUDIO;
using namespace rapidjson;

namespace CTAG {
    namespace CTRL {
        void UIMenuPageParams::init() {
            cursor = 0;
            scrollOffset = 0;
            mode = MODE_SELECT;
            paramCount = 0;
            presetCount = 0;
            presetChan = 0;
            parseParams();
        }

        void UIMenuPageParams::deinit() {}

        void UIMenuPageParams::parseParams() {
            paramCount = 0;
            const char *json = SoundProcessorManager::GetCStrJSONActivePluginParams(0);
            if (!json) return;

            Document doc;
            doc.Parse(json);
            if (!doc.HasMember("params") || !doc["params"].IsArray()) return;

            const Value &arr = doc["params"];
            for (SizeType i = 0; i < arr.Size() && paramCount < MAX_PARAMS; i++) {
                const Value &p = arr[i];
                if (!p.HasMember("id") || !p.HasMember("name") || !p.HasMember("type")) continue;
                ParamInfo &pi = params[paramCount];
                snprintf(pi.id, sizeof(pi.id), "%s", p["id"].GetString());
                snprintf(pi.name, sizeof(pi.name), "%s", p["name"].GetString());
                snprintf(pi.type, sizeof(pi.type), "%s", p["type"].GetString());
                pi.min = p.HasMember("min") ? p["min"].GetInt() : 0;
                pi.max = p.HasMember("max") ? p["max"].GetInt() : 1;
                pi.current = p.HasMember("current") ? p["current"].GetInt() : 0;
                // skip groups for now — treat as single item
                if (strcmp(pi.type, "group") == 0) continue;
                paramCount++;
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

        void UIMenuPageParams::onEncoder(int delta) {
            if (mode == MODE_SELECT) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 3) cursor = 3;
            } else if (mode == MODE_EDIT) {
                if (paramCount == 0) return;
                int newCursor = cursor + delta;
                if (newCursor < 0) newCursor = 0;
                if (newCursor >= paramCount) newCursor = paramCount - 1;
                cursor = newCursor;

                // auto-scroll
                int scr = paramIndexToScreen(cursor);
                if (scr < 0) scrollOffset += scr;
                if (scr >= 6) scrollOffset += (scr - 5);
                if (scrollOffset > paramCount - 6) scrollOffset = paramCount - 6;
                if (scrollOffset < 0) scrollOffset = 0;
            } else if (mode == MODE_VALUEEDIT) {
                if (paramCount == 0 || cursor >= paramCount) return;
                ParamInfo &pi = params[cursor];
                int val = pi.current + delta;
                if (val < pi.min) val = pi.min;
                if (val > pi.max) val = pi.max;
                pi.current = val;
                SoundProcessorManager::SetChannelParamValue(0, pi.id, "current", val);
            } else if (mode == MODE_PRESETS) {
                if (presetCount == 0) return;
                int newCursor = cursor + delta;
                if (newCursor < 0) newCursor = 0;
                if (newCursor >= presetCount) newCursor = presetCount - 1;
                cursor = newCursor;
                // auto-scroll
                if (cursor - scrollOffset < 0) scrollOffset = cursor;
                if (cursor - scrollOffset >= 6) scrollOffset = cursor - 5;
                if (scrollOffset > presetCount - 6) scrollOffset = presetCount - 6;
                if (scrollOffset < 0) scrollOffset = 0;
            }
            doRedraw();
        }

        void UIMenuPageParams::onButton(int btnId, bool longPress) {
            if (mode == MODE_SELECT) {
                if (btnId == 2 && !longPress) {
                    // enter sub-mode
                    if (cursor == 0) { mode = MODE_EDIT; cursor = 0; scrollOffset = 0; }
                    else if (cursor == 1) { mode = MODE_MAP; }
                    else if (cursor == 2) { mode = MODE_PSET; }
                    else if (cursor == 3) {
                        mode = MODE_PRESETS;
                        cursor = 0;
                        scrollOffset = 0;
                        parsePresets(0);
                    }
                }
            } else if (mode == MODE_EDIT) {
                if (btnId == 2 && !longPress && paramCount > 0) {
                    // enter value edit for selected param
                    mode = MODE_VALUEEDIT;
                } else if (btnId == 2 && longPress && paramCount > 0) {
                    // future: long press on param — mapping options
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
                mode = MODE_SELECT;
                cursor = 0;
                doRedraw();
                return true;
            }
            if (mode == MODE_MAP || mode == MODE_PSET) {
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
            } else if (mode == MODE_EDIT) {
                redrawEdit();
            } else if (mode == MODE_VALUEEDIT) {
                redrawEdit(); // same layout, cursor highlight acts as indicator
            } else if (mode == MODE_PRESETS) {
                redrawPresets();
            } else {
                // MAP / PSET — simple placeholder
                Display::Clear();
                Display::DrawString(0, 16, "Not implemented", Display::FONT_5X7);
                Display::Flush();
            }
        }

        void UIMenuPageParams::redrawSelect() {
            Display::Clear();
            Display::DrawString(0, 5, "EDIT", Display::FONT_5X7);
            Display::DrawString(0, 14, "MAP", Display::FONT_5X7);
            Display::DrawString(0, 23, "PSET", Display::FONT_5X7);
            Display::DrawString(0, 32, "PRESETS", Display::FONT_5X7);
            Display::InvertRect(0, 5 + cursor * 9, 128, 8);
            Display::Flush();
        }

        void UIMenuPageParams::redrawEdit() {
            Display::Clear();
            if (paramCount == 0) {
                Display::DrawString(0, 24, "No params", Display::FONT_5X7);
                Display::Flush();
                return;
            }
            // visible range: scrollOffset to scrollOffset+5 (6 items)
            int visible = paramCount - scrollOffset;
            if (visible > 6) visible = 6;

            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                const ParamInfo &pi = params[idx];
                int y = 5 + i * 9;
                // name left
                Display::DrawString(0, y, pi.name, Display::FONT_5X7);
                // value right
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
            // highlight cursor row — full width
            int cursorY = 5 + paramIndexToScreen(cursor) * 9;
            Display::InvertRect(0, cursorY, 128, 8);
            // scrollbar
            if (paramCount > 6)
                Display::DrawScrollbar(126, 5, 54, paramCount, cursor);
            Display::Flush();
        }

        void UIMenuPageParams::redrawPresets() {
            Display::Clear();
            if (presetCount == 0) {
                Display::DrawString(0, 24, "No presets", Display::FONT_5X7);
                Display::Flush();
                return;
            }
            int visible = presetCount - scrollOffset;
            if (visible > 6) visible = 6;
            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                const PresetInfo &pi = presets[idx];
                int y = 5 + i * 9;
                char buf[64];
                snprintf(buf, sizeof(buf), "%s", pi.name);
                Display::DrawString(0, y, buf, Display::FONT_5X7);
            }
            int cy = 5 + (cursor - scrollOffset) * 9;
            Display::InvertRect(0, cy, 128, 8);
            if (presetCount > 6)
                Display::DrawScrollbar(126, 5, 54, presetCount, cursor);
            Display::Flush();
        }
    }
}
