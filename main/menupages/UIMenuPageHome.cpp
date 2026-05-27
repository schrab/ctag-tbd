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
#include "UIMenuPageSystem.hpp"
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
            systemPage = new UIMenuPageSystem();
        }

        void UIMenuPageHome::deinit() {
            if (subPage == SP_SYSTEM) systemPage->deinit();
        }

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
            if (subPage == SP_SYSTEM) {
                systemPage->onEncoder(delta);
            } else if (subPage == SP_MAIN) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 5) cursor = 5;
            } else if (subPage == SP_SELECT) {
                int newCursor = cursor + delta;
                if (newCursor < 0) newCursor = 0;
                if (newCursor >= pluginCount) newCursor = pluginCount - 1;
                cursor = newCursor;
                if (cursor - scrollOffset < 0) scrollOffset = cursor;
                if (cursor - scrollOffset >= 6) scrollOffset = cursor - 5;
                if (scrollOffset > pluginCount - 6) scrollOffset = pluginCount - 6;
                if (scrollOffset < 0) scrollOffset = 0;
            } else if (subPage == SP_SELECT_CH) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 2) cursor = 2;
            }
            doRedraw();
        }

        void UIMenuPageHome::onButton(int btnId, bool longPress) {
            if (subPage == SP_SYSTEM) {
                systemPage->onButton(btnId, longPress);
            } else if (subPage == SP_MAIN) {
                if (btnId == 2 && !longPress) {
                    // enter subpage
                    if (cursor == 0) { // SELECT
                        subPage = SP_SELECT;
                        cursor = 0;
                        scrollOffset = 0;
                        parsePlugins();
                    } else if (cursor == 1) { // SYSTEM
                        subPage = SP_SYSTEM;
                        systemPage->init();
                    }
                }
            } else if (subPage == SP_SELECT) {
                if (btnId == 2 && !longPress) {
                    // select plugin
                    if (pluginCount > 0 && cursor < pluginCount) {
                        const PluginEntry &e = plugins[cursor];
                        if (e.isStereo) {
                            // stereo: load to ch0 directly
                            SoundProcessorManager::SetSoundProcessorChannel(0, e.id);
                            subPage = SP_MAIN;
                            cursor = 0;
                        } else {
                            // mono: show channel picker
                            selectedPlugin = cursor;
                            subPage = SP_SELECT_CH;
                            cursor = 0;
                        }
                    }
                } else if (btnId == 2 && longPress) {
                    // future: long press action on plugin
                }
            } else if (subPage == SP_SELECT_CH) {
                if (btnId == 2 && !longPress) {
                    const PluginEntry &e = plugins[selectedPlugin];
                    if (cursor == 0) {
                        SoundProcessorManager::SetSoundProcessorChannel(0, e.id);
                    } else if (cursor == 1) {
                        SoundProcessorManager::SetSoundProcessorChannel(1, e.id);
                    } else {
                        SoundProcessorManager::SetSoundProcessorChannel(0, e.id);
                        SoundProcessorManager::SetSoundProcessorChannel(1, e.id);
                    }
                    subPage = SP_MAIN;
                    cursor = 0;
                }
            }
            doRedraw();
        }

        bool UIMenuPageHome::onBack() {
            if (subPage == SP_SYSTEM) {
                if (systemPage->onBack()) return true;
                systemPage->deinit();
                subPage = SP_MAIN;
                cursor = 0;
                doRedraw();
                return true;
            }
            if (subPage == SP_SELECT_CH) {
                subPage = SP_SELECT;
                cursor = selectedPlugin;
                scrollOffset = 0;
                if (cursor > 5) scrollOffset = cursor - 5;
                doRedraw();
                return true;
            }
            if (subPage == SP_SELECT) {
                subPage = SP_MAIN;
                cursor = 0;
                doRedraw();
                return true;
            }
            return false;
        }

        void UIMenuPageHome::doRedraw() {
            if (subPage == SP_SYSTEM) {
                systemPage->doRedraw();
            } else if (subPage == SP_MAIN) {
                redrawMain();
            } else if (subPage == SP_SELECT) {
                redrawSelect();
            } else if (subPage == SP_SELECT_CH) {
                redrawSelectCh();
            }
        }

        void UIMenuPageHome::redrawMain() {
            Display::Clear();
            const char *items[] = {"SELECT", "SYSTEM", "FAVORITES", "SD CARD", "SLEEP", ""};
            for (int i = 0; i < 6; i++)
                Display::DrawString(0, 5 + i * 9, items[i], Display::FONT_5X7);
            Display::InvertRect(0, 5 + cursor * 9, 128, 8);
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
                int y = 5 + i * 9;
                Display::DrawString(0, y, e.name, Display::FONT_5X7);
                // type indicator right-aligned
                Display::DrawString(120, y, e.isStereo ? "S" : "M", Display::FONT_5X7);
            }
            int cy = 5 + (cursor - scrollOffset) * 9;
            Display::InvertRect(0, cy, 128, 8);
            if (pluginCount > 6)
                Display::DrawScrollbar(126, 5, 54, pluginCount, cursor);
            Display::Flush();
        }

        void UIMenuPageHome::redrawSelectCh() {
            Display::Clear();
            const PluginEntry &e = plugins[selectedPlugin];
            char buf[64];
            snprintf(buf, sizeof(buf), "%s >", e.name);
            Display::DrawString(0, 5, buf, Display::FONT_5X7);
            const char *opts[] = {"Ch 0", "Ch 1", "Both"};
            for (int i = 0; i < 3; i++)
                Display::DrawString(0, 20 + i * 10, opts[i], Display::FONT_5X7);
            Display::InvertRect(0, 20 + cursor * 10, 128, 8);            Display::Flush();
        }
    }
}
