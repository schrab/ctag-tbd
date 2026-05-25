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

#include "UIMenuPageHome.hpp"
#include "Display.hpp"
#include "SPManager.hpp"
#include "rapidjson/document.h"
#include "fs.hpp"
#include <cstring>

using namespace CTAG::DRIVERS;
using namespace CTAG::AUDIO;
using namespace rapidjson;

namespace CTAG {
    namespace CTRL {
        void UIMenuPageHome::init() {
            subPage = SP_MAIN;
            cursor = 0;
            scrollOffset = 0;
            pluginCount = 0;
        }

        void UIMenuPageHome::deinit() {}

        void UIMenuPageHome::parsePlugins() {
            pluginCount = 0;
            const char *json = SoundProcessorManager::GetCStrJSONSoundProcessors();
            if (!json) return;

            Document doc;
            doc.Parse(json);
            if (!doc.IsArray()) return;

            for (SizeType i = 0; i < doc.Size() && pluginCount < MAX_PLUGINS; i++) {
                const Value &p = doc[i];
                if (!p.HasMember("id")) continue;
                PluginEntry &e = plugins[pluginCount];
                snprintf(e.id, sizeof(e.id), "%s", p["id"].GetString());
                snprintf(e.name, sizeof(e.name), "%s",
                         p.HasMember("name") ? p["name"].GetString() : p["id"].GetString());
                e.isStereo = p.HasMember("isStereo") && p["isStereo"].GetBool();
                pluginCount++;
            }
        }

        void UIMenuPageHome::onEncoder(int delta) {
            if (subPage == SP_MAIN) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 5) cursor = 5;
            } else if (subPage == SP_SELECT) {
                // browse plugin list
                int newCursor = cursor + delta;
                if (newCursor < 0) newCursor = 0;
                if (newCursor >= pluginCount) newCursor = pluginCount - 1;
                int oldScroll = scrollOffset;
                cursor = newCursor;
                if (cursor - scrollOffset < 0) scrollOffset = cursor;
                if (cursor - scrollOffset >= 6) scrollOffset = cursor - 5;
                if (scrollOffset > pluginCount - 6) scrollOffset = pluginCount - 6;
                if (scrollOffset < 0) scrollOffset = 0;
            }
            doRedraw();
        }

        void UIMenuPageHome::onButton(int btnId, bool longPress) {
            if (subPage == SP_MAIN) {
                if (btnId == 2 && longPress) {
                    // enter subpage
                    if (cursor == 0) { // SELECT
                        subPage = SP_SELECT;
                        cursor = 0;
                        scrollOffset = 0;
                        parsePlugins();
                    }
                    // SLEEP, SYSTEM etc — future
                } else if (btnId == 2 && !longPress) {
                    // back — handled by UIMenu as panel switch
                }
            } else if (subPage == SP_SELECT) {
                if (btnId == 2 && !longPress) {
                    // back to main menu
                    subPage = SP_MAIN;
                    cursor = 0;
                } else if (btnId == 2 && longPress && pluginCount > 0 && cursor < pluginCount) {
                    // load plugin on channel 0
                    const PluginEntry &e = plugins[cursor];
                    SoundProcessorManager::SetSoundProcessorChannel(0, e.id);
                    subPage = SP_MAIN;
                    cursor = 0;
                }
            }
            doRedraw();
        }

        void UIMenuPageHome::doRedraw() {
            if (subPage == SP_MAIN) {
                redrawMain();
            } else if (subPage == SP_SELECT) {
                redrawSelect();
            }
        }

        void UIMenuPageHome::redrawMain() {
            Display::Clear();
            const char *items[] = {"SELECT", "SYSTEM", "FAVORITES", "SD CARD", "SLEEP"};
            for (int i = 0; i < 5; i++)
                Display::DrawString(0, 12 + i * 10, items[i], Display::FONT_5X7);
            Display::InvertRect(0, 12 + cursor * 10, 128, 8);
            Display::Flush();
        }

        void UIMenuPageHome::redrawSelect() {
            Display::Clear();
            if (pluginCount == 0) {
                Display::DrawString(0, 24, "No plugins found", Display::FONT_5X7);
                Display::Flush();
                return;
            }
            int visible = pluginCount - scrollOffset;
            if (visible > 6) visible = 6;
            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                const PluginEntry &e = plugins[idx];
                int y = 10 + i * 9;
                Display::DrawString(0, y, e.name, Display::FONT_5X7);
                if (e.isStereo)
                    Display::DrawString(110, y, "S", Display::FONT_5X7);
            }
            int cy = 10 + (cursor - scrollOffset) * 9;
            Display::InvertRect(0, cy, 128, 8);
            if (pluginCount > 6)
                Display::DrawScrollbar(126, 10, 54, pluginCount, cursor);
            Display::Flush();
        }
    }
}
