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
#include "Favorites.hpp"
#include "rapidjson/document.h"
#include "fs.hpp"
#include "UIMenuPageSystem.hpp"
#include <cstring>
#include <cstdio>
#include <dirent.h>

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
            systemPage = nullptr;
            favActiveId = -1;
            sdFileCount = 0;
            strcpy(sdCurrentPath, "/sd");
        }

        void UIMenuPageHome::deinit() {
            if (systemPage) {
                if (subPage == SP_SYSTEM) systemPage->deinit();
                delete systemPage;
                systemPage = nullptr;
            }
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

        void UIMenuPageHome::parseFavorites() {
            favActiveId = CTAG::FAV::Favorites::GetActiveFav();
            const std::string jsonStr = CTAG::FAV::Favorites::GetAllFavorites();
            if (jsonStr.empty()) return;

            Document doc;
            doc.Parse(jsonStr.c_str());
            if (!doc.IsArray()) return;

            for (SizeType i = 0; i < doc.Size() && i < MAX_FAVORITES; i++) {
                const Value &f = doc[i];
                FavEntry &e = favorites[i];
                snprintf(e.name, sizeof(e.name), "%s",
                         f.HasMember("name") ? f["name"].GetString() : "---");
                // build info: "ch0:Plug ch1:Plug"
                const char *p0 = f.HasMember("plug_0") ? f["plug_0"].GetString() : "?";
                const char *p1 = f.HasMember("plug_1") ? f["plug_1"].GetString() : "?";
                snprintf(e.name + strlen(e.name), sizeof(e.name) - strlen(e.name), "  %s %s", p0, p1);
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
            } else if (subPage == SP_SD_CARD) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor >= sdFileCount) cursor = sdFileCount - 1;
                if (cursor - scrollOffset < 0) scrollOffset = cursor;
                if (cursor - scrollOffset >= 6) scrollOffset = cursor - 5;
                if (scrollOffset > sdFileCount - 6) scrollOffset = sdFileCount - 6;
                if (scrollOffset < 0) scrollOffset = 0;
            } else if (subPage == SP_FAVORITES) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > MAX_FAVORITES - 1) cursor = MAX_FAVORITES - 1;
                if (cursor - scrollOffset < 0) scrollOffset = cursor;
                if (cursor - scrollOffset >= 6) scrollOffset = cursor - 5;
                if (scrollOffset > MAX_FAVORITES - 6) scrollOffset = MAX_FAVORITES - 6;
                if (scrollOffset < 0) scrollOffset = 0;
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
                        if (!systemPage) systemPage = new UIMenuPageSystem();
                        subPage = SP_SYSTEM;
                        systemPage->init();
                    } else if (cursor == 2) { // FAVORITES
                        subPage = SP_FAVORITES;
                        cursor = 0;
                        scrollOffset = 0;
                        parseFavorites();
                    } else if (cursor == 3) { // SD CARD
                        subPage = SP_SD_CARD;
                        cursor = 0;
                        scrollOffset = 0;
                        strcpy(sdCurrentPath, "/sd");
                        scanSdFiles();
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
            } else if (subPage == SP_FAVORITES) {
                if (btnId == 2 && !longPress) {
                    // activate selected favorite
                    if (cursor >= 0 && cursor < MAX_FAVORITES) {
                        CTAG::FAV::Favorites::ActivateFavorite(cursor);
                        favActiveId = cursor;
                        subPage = SP_MAIN;
                        cursor = 0;
                    }
                }
            } else if (subPage == SP_SD_CARD) {
                if (btnId == 2 && !longPress) {
                    if (cursor >= 0 && cursor < sdFileCount && sdIsDir[cursor]) {
                        if (strcmp(sdEntries[cursor], "..") == 0) {
                            goUpSdDir();
                        } else {
                            size_t curLen = strlen(sdCurrentPath);
                            snprintf(sdCurrentPath + curLen, sizeof(sdCurrentPath) - curLen, "/%s", sdEntries[cursor]);
                            scanSdFiles();
                            cursor = 0;
                            scrollOffset = 0;
                        }
                    }
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
            if (subPage == SP_FAVORITES) {
                subPage = SP_MAIN;
                cursor = 0;
                doRedraw();
                return true;
            }
            if (subPage == SP_SD_CARD) {
                if (strcmp(sdCurrentPath, "/sd") != 0) {
                    goUpSdDir();
                } else {
                    subPage = SP_MAIN;
                    cursor = 0;
                }
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
            } else if (subPage == SP_FAVORITES) {
                redrawFavorites();
            } else if (subPage == SP_SD_CARD) {
                redrawSdCard();
            }
        }

        void UIMenuPageHome::redrawMain() {
            Display::Clear();
            const char *items[] = {"SELECT", "SYSTEM", "FAVORITES", "SD CARD", "SLEEP", ""};
            for (int i = 0; i < 6; i++)
                Display::DrawString(0, 5 + i * 9, items[i], Display::FONT_5X7);
            // show active favorite indicator
            if (favActiveId >= 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "Fav%d", favActiveId);
                Display::DrawString(70, 5 + 2 * 9, buf, Display::FONT_5X7);
            }
            Display::InvertRect(0, 5 + cursor * 9, 128, 8);
            Display::Flush();
        }

        void UIMenuPageHome::redrawFavorites() {
            Display::Clear();
            if (MAX_FAVORITES == 0) {
                Display::DrawString(0, 24, "No favorites", Display::FONT_5X7);
                Display::Flush();
                return;
            }
            // show all 10 favorites (no scroll, fits on ~2 screens, but we show 6 at a time)
            int visible = MAX_FAVORITES - scrollOffset;
            if (visible > 6) visible = 6;
            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                const FavEntry &f = favorites[idx];
                int y = 5 + i * 9;
                char buf[64];
                if (idx == favActiveId) {
                    snprintf(buf, sizeof(buf), "*%s", f.name);
                } else {
                    snprintf(buf, sizeof(buf), " %s", f.name);
                }
                Display::DrawString(0, y, buf, Display::FONT_5X7);
            }
            int cy = 5 + (cursor - scrollOffset) * 9;
            Display::InvertRect(0, cy, 128, 8);
            if (MAX_FAVORITES > 6)
                Display::DrawScrollbar(126, 5, 54, MAX_FAVORITES, cursor);
            Display::Flush();
        }

        void UIMenuPageHome::scanSdFiles() {
            sdFileCount = 0;
            if (!FileSystem::IsSDMounted()) return;

            bool atRoot = (strcmp(sdCurrentPath, "/sd") == 0);
            if (!atRoot) {
                strcpy(sdEntries[0], "..");
                sdIsDir[0] = true;
                sdFileCount = 1;
            }

            DIR *dir = opendir(sdCurrentPath);
            if (!dir) return;
            struct dirent *ent;
            while ((ent = readdir(dir)) != nullptr && sdFileCount < MAX_SD_FILES) {
                const char *name = ent->d_name;
                if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
                size_t len = strlen(name);
                size_t copyLen = len < 31 ? len : 31;
                memcpy(sdEntries[sdFileCount], name, copyLen);
                sdEntries[sdFileCount][copyLen] = '\0';
                sdIsDir[sdFileCount] = (ent->d_type == DT_DIR);
                sdFileCount++;
            }
            closedir(dir);
        }

        void UIMenuPageHome::goUpSdDir() {
            char *lastSlash = strrchr(sdCurrentPath, '/');
            if (lastSlash && lastSlash != sdCurrentPath) {
                *lastSlash = '\0';
            }
            scanSdFiles();
            cursor = 0;
            scrollOffset = 0;
        }

        void UIMenuPageHome::redrawSdCard() {
            Display::Clear();
            if (sdFileCount == 0) {
                Display::DrawString(0, 24, "No card or empty", Display::FONT_5X7);
                Display::Flush();
                return;
            }
            int visible = sdFileCount - scrollOffset;
            if (visible > 6) visible = 6;
            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                int y = 5 + i * 9;
                char buf[64];
                if (sdIsDir[idx]) {
                    snprintf(buf, sizeof(buf), " [%s]", sdEntries[idx]);
                } else {
                    snprintf(buf, sizeof(buf), "  %s", sdEntries[idx]);
                }
                Display::DrawString(0, y, buf, Display::FONT_5X7);
            }
            int cy = 5 + (cursor - scrollOffset) * 9;
            Display::InvertRect(0, cy, 128, 8);
            if (sdFileCount > 6)
                Display::DrawScrollbar(126, 5, 54, sdFileCount, cursor);
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
            Display::InvertRect(0, 20 + cursor * 10, 128, 8);
            Display::Flush();
        }
    }
}
