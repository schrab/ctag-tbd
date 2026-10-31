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
#include "DspProxy.hpp"
#include "UIMenu.hpp"
#include "rapidjson/document.h"
#include "UIMenuPageSystem.hpp"
#include "UIMenuPageMidi.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdio>

using namespace CTAG::DRIVERS;
using namespace rapidjson;

namespace CTAG {
    namespace CTRL {
        void UIMenuPageHome::init() {
            subPage = SP_MAIN;
            cursor = 0;
            scrollOffset = 0;
            pluginCount = 0;
            systemPage = nullptr;
            midiPage = nullptr;
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
            if (midiPage) {
                if (subPage == SP_MIDI) midiPage->deinit();
                delete midiPage;
                midiPage = nullptr;
            }
        }

        void UIMenuPageHome::parsePlugins() {
            pluginCount = 0;
            const char *json = DspProxy::GetCStrJSONSoundProcessors();
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
            favActiveId = DspProxy::GetActiveFav();
            const std::string jsonStr = DspProxy::GetAllFavorites();
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
            } else if (subPage == SP_MIDI) {
                midiPage->onEncoder(delta);
            } else if (subPage == SP_MAIN) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 6) cursor = 6;
            } else if (subPage == SP_SELECT) {
                int newCursor = cursor + delta;
                if (newCursor < 0) newCursor = 0;
                if (newCursor >= pluginCount) newCursor = pluginCount - 1;
                cursor = newCursor;
                if (cursor - scrollOffset < 0) scrollOffset = cursor;
                if (cursor - scrollOffset >= VISIBLE_LINES) scrollOffset = cursor - (VISIBLE_LINES - 1);
                if (scrollOffset > pluginCount - VISIBLE_LINES) scrollOffset = pluginCount - VISIBLE_LINES;
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
                if (cursor - scrollOffset >= VISIBLE_LINES) scrollOffset = cursor - (VISIBLE_LINES - 1);
                if (scrollOffset > sdFileCount - VISIBLE_LINES) scrollOffset = sdFileCount - VISIBLE_LINES;
                if (scrollOffset < 0) scrollOffset = 0;
            } else if (subPage == SP_SD_INFO) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 1) cursor = 1; // DELETE / CANCEL
            } else if (subPage == SP_FAVORITES) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > MAX_FAVORITES - 1) cursor = MAX_FAVORITES - 1;
                if (cursor - scrollOffset < 0) scrollOffset = cursor;
                if (cursor - scrollOffset >= VISIBLE_LINES) scrollOffset = cursor - (VISIBLE_LINES - 1);
                if (scrollOffset > MAX_FAVORITES - VISIBLE_LINES) scrollOffset = MAX_FAVORITES - VISIBLE_LINES;
                if (scrollOffset < 0) scrollOffset = 0;
            }
            doRedraw();
        }

        // Right encoder reaches the embedded system page (its live value
        // edit); the other Home subpages have no value dimension.
        void UIMenuPageHome::onValueEncoder(int delta) {
            if (subPage == SP_SYSTEM) {
                systemPage->onValueEncoder(delta);
            } else if (subPage == SP_MIDI) {
                midiPage->onValueEncoder(delta);
            }
        }

        void UIMenuPageHome::onButton(int btnId, bool longPress) {
            if (subPage == SP_SYSTEM) {
                systemPage->onButton(btnId, longPress);
            } else if (subPage == SP_MIDI) {
                midiPage->onButton(btnId, longPress);
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
                    } else if (cursor == 2) { // MIDI
                        if (!midiPage) midiPage = new UIMenuPageMidi();
                        subPage = SP_MIDI;
                        midiPage->init();
                    } else if (cursor == 3) { // FAVORITES
                        subPage = SP_FAVORITES;
                        cursor = 0;
                        scrollOffset = 0;
                        parseFavorites();
                    } else if (cursor == 4) { // SD CARD
                        subPage = SP_SD_CARD;
                        cursor = 0;
                        scrollOffset = 0;
                        strcpy(sdCurrentPath, "/sd");
                        scanSdFiles();
                    } else if (cursor == 5) { // UNMOUNT / MOUNT (card hot-swap)
                        if (DspProxy::IsSDMounted()) {
                            if (!(DspProxy::SdIsPlaying() || DspProxy::SdIsRecording())) {
                                DspProxy::SdUnmount();
                                // Wait briefly for the io-caps push to flip
                                // the cached state; the label follows it.
                                for (int i = 0; i < 15 && DspProxy::IsSDMounted(); i++) {
                                    vTaskDelay(pdMS_TO_TICKS(100));
                                }
                            }
                        } else {
                            DspProxy::SdMount();
                            for (int i = 0; i < 20 && !DspProxy::IsSDMounted(); i++) {
                                vTaskDelay(pdMS_TO_TICKS(100));
                            }
                        }
                    }
                }
            } else if (subPage == SP_SELECT) {
                if (btnId == 2 && !longPress) {
                    // select plugin
                    if (pluginCount > 0 && cursor < pluginCount) {
                        const PluginEntry &e = plugins[cursor];
                        if (e.isStereo) {
                            // stereo: load to ch0 directly
                            DspProxy::SetSoundProcessorChannel(0, e.id);
                            subPage = SP_MAIN;
                            cursor = 0;
                            UIMenu::RequestPanelEdit(UIMenu::PANEL_PARAMS, 0);
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
                    int chHint = 0;
                    if (cursor == 0) {
                        DspProxy::SetSoundProcessorChannel(0, e.id);
                    } else if (cursor == 1) {
                        DspProxy::SetSoundProcessorChannel(1, e.id);
                        chHint = 1;
                    } else {
                        DspProxy::SetSoundProcessorChannel(0, e.id);
                        DspProxy::SetSoundProcessorChannel(1, e.id);
                    }
                    subPage = SP_MAIN;
                    cursor = 0;
                    UIMenu::RequestPanelEdit(UIMenu::PANEL_PARAMS, chHint);
                }
            } else if (subPage == SP_FAVORITES) {
                if (btnId == 2 && !longPress) {
                    // activate selected favorite
                    if (cursor >= 0 && cursor < MAX_FAVORITES) {
                        DspProxy::ActivateFavorite(cursor);
                        favActiveId = cursor;
                        subPage = SP_MAIN;
                        cursor = 0;
                        UIMenu::RequestPanelEdit(UIMenu::PANEL_PARAMS, -1);
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
                    } else if (cursor >= 0 && cursor < sdFileCount && !sdIsDir[cursor]) {
                        // File: open the info/delete view.
                        snprintf(sdInfoPath, sizeof(sdInfoPath), "%s/%s",
                                 sdCurrentPath, sdEntries[cursor]);
                        sdInfoOk = DspProxy::SdGetFileInfo(sdInfoPath, &sdInfo);
                        subPage = SP_SD_INFO;
                        cursor = 0;
                    }
                }
            } else if (subPage == SP_SD_INFO) {
                if (btnId == 2 && !longPress) {
                    // DELETE works regardless of the info probe result: a
                    // broken-header file (truncated recording) shows
                    // "NOT A WAV" but is exactly the one to remove, and the
                    // DSP-side unlink only needs the path.
                    if (cursor == 0) {
                        if (DspProxy::SdDelete(sdInfoPath)) {
                            scanSdFiles();
                            subPage = SP_SD_CARD;
                        }
                        // Failed delete stays on the info view (file busy /
                        // unlink error); the user can CANCEL out.
                    } else {
                        subPage = SP_SD_CARD;
                    }
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
                cursor = 1;
                doRedraw();
                return true;
            }
            if (subPage == SP_MIDI) {
                if (midiPage->onBack()) return true;
                midiPage->deinit();
                subPage = SP_MAIN;
                cursor = 2;
                doRedraw();
                return true;
            }
            if (subPage == SP_SELECT_CH) {
                subPage = SP_SELECT;
                cursor = selectedPlugin;
                scrollOffset = 0;
                if (cursor > VISIBLE_LINES - 1) scrollOffset = cursor - (VISIBLE_LINES - 1);
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
            if (subPage == SP_SD_INFO) {
                subPage = SP_SD_CARD;
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
            } else if (subPage == SP_MIDI) {
                midiPage->doRedraw();
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
            } else if (subPage == SP_SD_INFO) {
                redrawSdInfo();
            }
        }

        void UIMenuPageHome::redrawMain() {
            Display::Clear();
            lastDrawnOnline = DspProxy::IsOnline() ? 1 : 0;
            const char *items[] = {"SELECT", "SYSTEM", "MIDI", "FAVORITES", "SD CARD",
                                   DspProxy::IsSDMounted() ? "UNMOUNT" : "MOUNT", "SLEEP"};
            for (int i = 0; i < 7; i++)
                Display::DrawString(0, ROW(i), items[i], FONT_UI);
            if (favActiveId >= 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "Fav%d", favActiveId);
                Display::DrawString(70, ROW(3), buf, FONT_UI);
            }
            // Link state (SDD §4): the panel keeps cached values when the DSP
            // dies; make the offline state visible on the HOME panel.
            if (!DspProxy::IsOnline()) {
                Display::DrawString(62, ROW(0), "DSP OFF", FONT_UI);
            }
            Display::InvertRect(0, ROW(cursor) - 1, 128, 8);
            Display::Flush();
        }

        void UIMenuPageHome::onTick() {
            if (subPage == SP_MIDI) {
                // Live scan updates / BLE status while the MIDI submenu is
                // up (the Midi page change-detects internally).
                midiPage->onTick();
                return;
            }
            // MAIN is the only view with live content (the DSP OFF label):
            // repaint when the link state flips while the page idles on
            // screen, instead of waiting for the next input event.
            if (subPage == SP_MAIN &&
                lastDrawnOnline != (DspProxy::IsOnline() ? 1 : 0)) {
                doRedraw();
            }
        }

        void UIMenuPageHome::redrawFavorites() {
            Display::Clear();
            if (MAX_FAVORITES == 0) {
                Display::DrawString(0, 24, "No favorites", FONT_UI);
                Display::Flush();
                return;
            }
            int visible = ClampVisible(MAX_FAVORITES, scrollOffset, VISIBLE_ITEMS);
            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                const FavEntry &f = favorites[idx];
                int y = ROW(i);
                char buf[64];
                if (idx == favActiveId) {
                    snprintf(buf, sizeof(buf), "*%s", f.name);
                } else {
                    snprintf(buf, sizeof(buf), " %s", f.name);
                }
                Display::DrawString(0, y, buf, FONT_UI);
            }
            int cy = ROW(cursor - scrollOffset);
            Display::InvertRect(0, cy - 1, 128, 8);
            if (MAX_FAVORITES > VISIBLE_ITEMS)
                Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0, VISIBLE_ITEMS * LINE_H, MAX_FAVORITES, cursor);
            Display::Flush();
        }

        void UIMenuPageHome::scanSdFiles() {
            sdFileCount = 0;
            if (!DspProxy::IsSDMounted()) return;

            bool atRoot = (strcmp(sdCurrentPath, "/sd") == 0);
            if (!atRoot) {
                strcpy(sdEntries[0], "..");
                sdIsDir[0] = true;
                sdFileCount = 1;
            }

            // Directory contents come over the link (SD proxy, SDD §4.2);
            // entries have no "." or "..". The Windows metadata folder and
            // dot entries are hidden — pure noise on a musical instrument.
            // Separate read/write slots: the write slot may not advance on a
            // filtered entry (single-index compaction re-read the SAME slot
            // forever — with "System Volume Information" first in the FAT
            // root, every entry got filtered and the page showed
            // "No card or empty").
            int n = DspProxy::ListSdDir(sdCurrentPath, sdEntries + sdFileCount,
                                        sdIsDir + sdFileCount,
                                        MAX_SD_FILES - sdFileCount);
            int w = sdFileCount; // write slot (kept entries compact forward)
            for (int i = 0; i < n; i++) {
                const int r = sdFileCount + i; // read slot
                const char *name = sdEntries[r];
                if (strcasecmp(name, "System Volume Information") == 0 || name[0] == '.') continue;
                if (w != r) {
                    strncpy(sdEntries[w], sdEntries[r], 31);
                    sdEntries[w][31] = '\0';
                    sdIsDir[w] = sdIsDir[r];
                }
                w++;
            }
            sdFileCount = w;
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
            if (!DspProxy::IsSDMounted()) {
                // Unmounted: distinguish a swapped-out card from an empty
                // slot (CD pin). Remount lives in the HOME menu (UNMOUNT/
                // MOUNT item).
                Display::DrawString(0, 24,
                                    DspProxy::IsSDPresent() ? "UNMOUNTED" : "NO CARD",
                                    FONT_UI);
                Display::Flush();
                return;
            }
            if (sdFileCount == 0) {
                Display::DrawString(0, 24, "No card or empty", FONT_UI);
                Display::Flush();
                return;
            }
            int visible = ClampVisible(sdFileCount, scrollOffset, VISIBLE_ITEMS);
            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                int y = ROW(i);
                char buf[64];
                if (sdIsDir[idx]) {
                    snprintf(buf, sizeof(buf), " [%s]", sdEntries[idx]);
                } else {
                    snprintf(buf, sizeof(buf), "  %s", sdEntries[idx]);
                }
                Display::DrawString(0, y, buf, FONT_UI);
            }
            int cy = ROW(cursor - scrollOffset);
            Display::InvertRect(0, cy - 1, 128, 8);
            if (sdFileCount > VISIBLE_ITEMS)
                Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0, VISIBLE_ITEMS * LINE_H, sdFileCount, cursor);
            Display::Flush();
        }

        void UIMenuPageHome::redrawSdInfo() {
            Display::Clear();
            // File name: the path's basename, clipped to the display width.
            const char *base = strrchr(sdInfoPath, '/');
            base = base ? base + 1 : sdInfoPath;
            Display::DrawString(0, ROW(0), base, FONT_UI);
            char buf[24];
            if (sdInfoOk) {
                if (sdInfo.sampleRate > 0) {
                    uint32_t sec = sdInfo.frames / sdInfo.sampleRate;
                    snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
                } else {
                    snprintf(buf, sizeof(buf), "--:--");
                }
                Display::DrawString(0, ROW(1), buf, FONT_UI);
                // Size: KB under 1 MB, MB from there.
                uint32_t size = sdInfo.sizeBytes;
                if (size >= 1024u * 1024u) {
                    snprintf(buf, sizeof(buf), "%lu.%lu MB", (unsigned long)(size / (1024u * 1024u)),
                             (unsigned long)((size % (1024u * 1024u)) * 10u / (1024u * 1024u)));
                } else {
                    snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(size / 1024u));
                }
                Display::DrawString(0, ROW(2), buf, FONT_UI);
                snprintf(buf, sizeof(buf), "%luk %2uch %2ub", (unsigned long)sdInfo.sampleRate / 1000u,
                         sdInfo.channels, sdInfo.bits);
                Display::DrawString(0, ROW(3), buf, FONT_UI);
            } else {
                // The DSP refuses the info round trip while the transport is
                // active (SD/file concurrency) — say so instead of blaming
                // the file.
                if (DspProxy::SdIsPlaying() || DspProxy::SdIsRecording())
                    Display::DrawString(0, ROW(1), "TAPE BUSY", FONT_UI);
                else
                    Display::DrawString(0, ROW(1), "NOT A WAV", FONT_UI);
            }
            Display::DrawString(0, ROW(5), "DELETE", FONT_UI);
            Display::DrawString(0, ROW(6), "CANCEL", FONT_UI);
            Display::InvertRect(0, ROW(5 + cursor) - 1, 128, 8);
            Display::Flush();
        }

        void UIMenuPageHome::redrawSelect() {
            Display::Clear();
            if (pluginCount == 0) {
                Display::DrawString(0, 24, "No plugins found", FONT_UI);
                Display::Flush();
                return;
            }
            int visible = ClampVisible(pluginCount, scrollOffset, VISIBLE_ITEMS);
            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                const PluginEntry &e = plugins[idx];
                int y = ROW(i);
                Display::DrawString(0, y, e.name, FONT_UI);
                Display::DrawString(120, y, e.isStereo ? "S" : "M", FONT_UI);
            }
            int cy = ROW(cursor - scrollOffset);
            Display::InvertRect(0, cy - 1, 128, 8);
            if (pluginCount > VISIBLE_ITEMS)
                Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0, VISIBLE_ITEMS * LINE_H, pluginCount, cursor);
            Display::Flush();
        }

        void UIMenuPageHome::redrawSelectCh() {
            Display::Clear();
            const PluginEntry &e = plugins[selectedPlugin];
            char buf[64];
            snprintf(buf, sizeof(buf), "%s >", e.name);
            Display::DrawString(0, ROW(0), buf, FONT_UI);
            const int optGap = LINE_H + 2;
            const char *opts[] = {"Ch 0", "Ch 1", "Both"};
            for (int i = 0; i < 3; i++)
                Display::DrawString(0, ROW_HDR(1) + i * optGap, opts[i], FONT_UI);
            Display::InvertRect(0, ROW_HDR(1) + cursor * optGap - 1, 128, 8);
            Display::Flush();
        }
    }
}
