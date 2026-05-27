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

#include "UIMenuPageTape.hpp"
#include "Display.hpp"
#include "SDAudio.hpp"
#include "fs.hpp"
#include <cstring>
#include <cstdio>
#include <dirent.h>

using namespace CTAG::DRIVERS;
using namespace CTAG::AUDIO;

namespace CTAG {
    namespace CTRL {
        void UIMenuPageTape::init() {
            subPage = SP_MAIN;
            cursor = 0;
            scrollOffset = 0;
            fileCount = 0;
        }

        void UIMenuPageTape::deinit() {}

        void UIMenuPageTape::scanFiles() {
            fileCount = 0;
            if (!FileSystem::IsSDMounted()) return;
            DIR *dir = opendir("/sd");
            if (!dir) return;
            struct dirent *ent;
            while ((ent = readdir(dir)) != nullptr && fileCount < MAX_FILES) {
                const char *name = ent->d_name;
                int len = strlen(name);
                if (len > 4 && (strcasecmp(name + len - 4, ".wav") == 0)) {
                    size_t copyLen = len < 31 ? len : 31;
                    memcpy(fileNames[fileCount], name, copyLen);
                    fileNames[fileCount][copyLen] = '\0';
                    fileCount++;
                }
            }
            closedir(dir);
        }

        void UIMenuPageTape::onEncoder(int delta) {
            if (subPage == SP_MAIN) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 2) cursor = 2;
            } else if (subPage == SP_FILELIST) {
                int nc = cursor + delta;
                if (nc < 0) nc = 0;
                if (nc >= fileCount) nc = fileCount - 1;
                cursor = nc;
                if (cursor - scrollOffset < 0) scrollOffset = cursor;
                if (cursor - scrollOffset >= 6) scrollOffset = cursor - 5;
                if (scrollOffset < 0) scrollOffset = 0;
                if (fileCount > 6 && scrollOffset > fileCount - 6) scrollOffset = fileCount - 6;
                if (scrollOffset < 0) scrollOffset = 0;
            } else if (subPage == SP_RECORD) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 1) cursor = 1;
            }
            doRedraw();
        }

        void UIMenuPageTape::onButton(int btnId, bool longPress) {
            if (subPage == SP_MAIN) {
                if (btnId == 2 && !longPress) {
                    if (cursor == 0) {
                        // Playback: browse files
                        scanFiles();
                        subPage = SP_FILELIST;
                        cursor = 0;
                        scrollOffset = 0;
                    } else if (cursor == 1) {
                        // Record
                        subPage = SP_RECORD;
                        cursor = 0;
                    } else if (cursor == 2) {
                        // Stop
                        SDAudio::Stop();
                    }
                }
            } else if (subPage == SP_FILELIST) {
                if (btnId == 2 && !longPress && fileCount > 0 && cursor < fileCount) {
                    char path[64];
                    snprintf(path, sizeof(path), "/sd/%s", fileNames[cursor]);
                    SDAudio::StartPlayback(path);
                    subPage = SP_MAIN;
                    cursor = 0;
                }
            } else if (subPage == SP_RECORD) {
                if (btnId == 2 && !longPress) {
                    if (cursor == 0) {
                        SDAudio::StartRecording("/sd/record.wav");
                    } else {
                        SDAudio::Stop();
                    }
                    subPage = SP_MAIN;
                    cursor = 0;
                }
            }
            doRedraw();
        }

        bool UIMenuPageTape::onBack() {
            if (subPage == SP_RECORD) {
                subPage = SP_MAIN;
                cursor = 0;
                doRedraw();
                return true;
            }
            if (subPage == SP_FILELIST) {
                subPage = SP_MAIN;
                cursor = 0;
                doRedraw();
                return true;
            }
            return false;
        }

        void UIMenuPageTape::doRedraw() {
            if (subPage == SP_MAIN) redrawMain();
            else if (subPage == SP_FILELIST) redrawFileList();
            else if (subPage == SP_RECORD) redrawRecord();
        }

        void UIMenuPageTape::redrawMain() {
            Display::Clear();
            int y = 5;
            if (SDAudio::IsPlaying()) {
                Display::DrawString(0, y, "PLAYING", Display::FONT_5X7);
                if (SDAudio::GetTotalFrames() > 0) {
                    char buf[24];
                    uint32_t sec = SDAudio::GetPositionFrames() / 44100;
                    uint32_t total = SDAudio::GetTotalFrames() / 44100;
                    snprintf(buf, sizeof(buf), "%02u:%02u / %02u:%02u",
                             (unsigned)(sec / 60), (unsigned)(sec % 60), (unsigned)(total / 60), (unsigned)(total % 60));
                    y += 9;
                    Display::DrawString(0, y, buf, Display::FONT_5X7);
                }
            } else if (SDAudio::IsRecording()) {
                Display::DrawString(0, y, "RECORDING", Display::FONT_5X7);
                char buf[24];
                uint32_t sec = SDAudio::GetPositionFrames() / 44100;
                snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
                y += 9;
                Display::DrawString(0, y, buf, Display::FONT_5X7);
            } else {
                Display::DrawString(0, y, "PLAY FILE", Display::FONT_5X7);
                y += 9;
                Display::DrawString(0, y, "RECORD", Display::FONT_5X7);
                y += 9;
                Display::DrawString(0, y, "STOP", Display::FONT_5X7);
                y += 9;
            }
            int highlightY = 5 + cursor * 9;
            Display::InvertRect(0, highlightY, 128, 8);
            Display::Flush();
        }

        void UIMenuPageTape::redrawFileList() {
            Display::Clear();
            if (!FileSystem::IsSDMounted()) {
                Display::DrawString(0, 24, "No SD card", Display::FONT_5X7);
                Display::Flush();
                return;
            }
            if (fileCount == 0) {
                Display::DrawString(0, 24, "No WAV files", Display::FONT_5X7);
                Display::Flush();
                return;
            }
            int visible = fileCount - scrollOffset;
            if (visible > 6) visible = 6;
            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                Display::DrawString(0, 5 + i * 9, fileNames[idx], Display::FONT_5X7);
            }
            int cy = 5 + (cursor - scrollOffset) * 9;
            Display::InvertRect(0, cy, 128, 8);
            if (fileCount > 6) Display::DrawScrollbar(126, 5, 54, fileCount, cursor);
            Display::Flush();
        }

        void UIMenuPageTape::redrawRecord() {
            Display::Clear();
            Display::DrawString(0, 5, "START REC", Display::FONT_5X7);
            Display::DrawString(0, 14, "CANCEL", Display::FONT_5X7);
            Display::InvertRect(0, 5 + cursor * 9, 128, 8);
            Display::Flush();
        }
    }
}
