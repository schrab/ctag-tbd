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
#include "DspProxy.hpp"
#include <cstring>
#include <cstdio>
#include <strings.h>

using namespace CTAG::DRIVERS;

namespace CTAG {
    namespace CTRL {
        void UIMenuPageTape::init() {
            subPage = SP_MAIN;
            cursor = 0;
            scrollOffset = 0;
            fileCount = 0;
            strcpy(sdPath, "/sd");
        }

        void UIMenuPageTape::deinit() {
            // Deliberately empty: playback/recording keep running in the
            // background across panel switches (the DSP owns the transport);
            // re-entering shows the live state.
        }

        void UIMenuPageTape::scanFiles() {
            fileCount = 0;
            if (!DspProxy::IsSDMounted()) return;
            // Listing comes over the link (SD proxy, SDD §4.2). Directories
            // are navigable; files must be .wav to be playable.
            bool atRoot = (strcmp(sdPath, "/sd") == 0);
            if (!atRoot) {
                strcpy(fileNames[0], "..");
                isDir[0] = true;
                fileCount = 1;
            }
            char names[MAX_FILES][32];
            bool dirs[MAX_FILES];
            int n = DspProxy::ListSdDir(sdPath, names, dirs, MAX_FILES - fileCount);
            for (int i = 0; i < n && fileCount < MAX_FILES; i++) {
                const char *name = names[i];
                int len = strlen(name);
                // Hide the Windows metadata folder and dot entries — pure
                // noise on a musical instrument's card.
                if (strcasecmp(name, "System Volume Information") == 0 || name[0] == '.') continue;
                bool isWav = len > 4 && strcasecmp(name + len - 4, ".wav") == 0;
                if (!dirs[i] && !isWav) continue;
                size_t copyLen = len < 31 ? len : 31;
                memcpy(fileNames[fileCount], name, copyLen);
                fileNames[fileCount][copyLen] = '\0';
                isDir[fileCount] = dirs[i];
                fileCount++;
            }
        }

        void UIMenuPageTape::goUpDir() {
            char *lastSlash = strrchr(sdPath, '/');
            if (lastSlash && lastSlash != sdPath) *lastSlash = '\0';
            scanFiles();
            cursor = 0;
            scrollOffset = 0;
        }

        // Find the first free tape_NNN.wav name on /sd (case-insensitive
        // match against existing entries). Single UI-task caller, so the
        // occupancy bitmap can be static.
        void UIMenuPageTape::computeRecName() {
            recNameOk = false;
            recName[0] = '\0';
            if (!DspProxy::IsSDMounted()) return;
            static bool taken[1000];
            memset(taken, 0, sizeof(taken));
            char names[MAX_FILES][32];
            bool dirs[MAX_FILES];
            int n = DspProxy::ListSdDir("/sd", names, dirs, MAX_FILES);
            for (int i = 0; i < n; i++) {
                if (dirs[i]) continue;
                int a = 0, b = 0, c = 0;
                char ext[8] = "";
                if (sscanf(names[i], "tape_%1d%1d%1d.%3s", &a, &b, &c, ext) == 4 &&
                    strcasecmp(ext, "wav") == 0) {
                    taken[a * 100 + b * 10 + c] = true;
                }
            }
            for (int i = 1; i <= 999; i++) {
                if (!taken[i]) {
                    snprintf(recName, sizeof(recName), "tape_%03d.wav", i);
                    recNameOk = true;
                    return;
                }
            }
        }

        void UIMenuPageTape::onEncoder(int delta) {
            if (subPage == SP_MAIN) {
                // MAIN always has two selectable rows: idle = PLAY FILE /
                // RECORD; active = status line / STOP.
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 1) cursor = 1;
            } else if (subPage == SP_FILELIST) {
                int nc = cursor + delta;
                if (nc < 0) nc = 0;
                if (nc >= fileCount) nc = fileCount - 1;
                cursor = nc;
                ClampScroll(cursor, scrollOffset, fileCount, VISIBLE_ITEMS);
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
                    bool active = DspProxy::SdIsPlaying() || DspProxy::SdIsRecording();
                    if (active) {
                        if (cursor == 1) DspProxy::SdStop(); // STOP row
                    } else {
                        if (cursor == 0) {
                            // Playback: browse files
                            scanFiles();
                            subPage = SP_FILELIST;
                            cursor = 0;
                            scrollOffset = 0;
                        } else if (cursor == 1) {
                            // Record
                            computeRecName();
                            subPage = SP_RECORD;
                            cursor = 0;
                        }
                    }
                }
            } else if (subPage == SP_FILELIST) {
                if (btnId == 2 && !longPress && fileCount > 0 && cursor < fileCount) {
                    if (isDir[cursor]) {
                        if (strcmp(fileNames[cursor], "..") == 0) {
                            goUpDir();
                        } else {
                            char next[112]; // sdPath(64) + '/' + name(31) + NUL
                            snprintf(next, sizeof(next), "%s/%s", sdPath, fileNames[cursor]);
                            strncpy(sdPath, next, sizeof(sdPath) - 1);
                            sdPath[sizeof(sdPath) - 1] = '\0';
                            scanFiles();
                            cursor = 0;
                            scrollOffset = 0;
                        }
                    } else {
                        // sdPath(64) + '/' + name(31) + NUL fits comfortably.
                        char path[112];
                        snprintf(path, sizeof(path), "%s/%s", sdPath, fileNames[cursor]);
                        DspProxy::SdStartPlayback(path);
                        subPage = SP_MAIN;
                        cursor = 0;
                    }
                }
            } else if (subPage == SP_RECORD) {
                if (btnId == 2 && !longPress) {
                    if (cursor == 0) {
                        if (recNameOk) {
                            char path[96];
                            snprintf(path, sizeof(path), "/sd/%s", recName);
                            DspProxy::SdStartRecording(path);
                        }
                    } else {
                        DspProxy::SdStop();
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
                // BACK climbs out of subdirectories before leaving the list.
                if (strcmp(sdPath, "/sd") != 0) {
                    goUpDir();
                    doRedraw();
                    return true;
                }
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
            lastDrawnPlaying = DspProxy::SdIsPlaying();
            lastDrawnRecording = DspProxy::SdIsRecording();
            lastDrawnSecond = DspProxy::SdGetPositionFrames() / 44100;
            int y = ITEM_Y0;
            if (lastDrawnPlaying) {
                // One status line + STOP keeps the cursor map uniform
                // (row = cursor) across all MAIN states.
                if (DspProxy::SdGetTotalFrames() > 0) {
                    char buf[24];
                    uint32_t sec = DspProxy::SdGetPositionFrames() / 44100;
                    uint32_t total = DspProxy::SdGetTotalFrames() / 44100;
                    snprintf(buf, sizeof(buf), "PLAY %02u:%02u/%02u:%02u",
                             (unsigned)(sec / 60), (unsigned)(sec % 60),
                             (unsigned)(total / 60), (unsigned)(total % 60));
                    Display::DrawString(0, y, buf, FONT_UI);
                } else {
                    Display::DrawString(0, y, "PLAYING", FONT_UI);
                }
                y += LINE_H;
                Display::DrawString(0, y, "STOP", FONT_UI);
            } else if (lastDrawnRecording) {
                char buf[24];
                uint32_t sec = DspProxy::SdGetPositionFrames() / 44100;
                snprintf(buf, sizeof(buf), "REC %02u:%02u",
                         (unsigned)(sec / 60), (unsigned)(sec % 60));
                Display::DrawString(0, y, buf, FONT_UI);
                y += LINE_H;
                Display::DrawString(0, y, "STOP", FONT_UI);
            } else {
                Display::DrawString(0, y, "PLAY FILE", FONT_UI);
                y += LINE_H;
                Display::DrawString(0, y, "RECORD", FONT_UI);
            }
            int highlightY = ROW(cursor);
            Display::InvertRect(0, highlightY - 1, 128, 8);
            Display::Flush();
        }

        void UIMenuPageTape::onTick() {
            // MAIN carries the live transport state (playing/recording flag
            // and elapsed timecode); the file list and record views are
            // static. Repaint only when something moved.
            if (subPage != SP_MAIN) return;
            bool playing = DspProxy::SdIsPlaying();
            bool recording = DspProxy::SdIsRecording();
            uint32_t sec = DspProxy::SdGetPositionFrames() / 44100;
            if (playing != lastDrawnPlaying || recording != lastDrawnRecording ||
                sec != lastDrawnSecond) {
                doRedraw();
            }
        }

        void UIMenuPageTape::redrawFileList() {
            Display::Clear();
            if (!DspProxy::IsSDMounted()) {
                Display::DrawString(0, 24, "No SD card", FONT_UI);
                Display::Flush();
                return;
            }
            if (fileCount == 0) {
                Display::DrawString(0, 24, "No WAV files", FONT_UI);
                Display::Flush();
                return;
            }
            int visible = ClampVisible(fileCount, scrollOffset, VISIBLE_ITEMS);
            for (int i = 0; i < visible; i++) {
                int idx = scrollOffset + i;
                char row[36];
                snprintf(row, sizeof(row), "%s%s", isDir[idx] ? "[" : "",
                         fileNames[idx]);
                Display::DrawString(0, ROW(i), row, FONT_UI);
            }
            int cy = ROW(cursor - scrollOffset);
            Display::InvertRect(0, cy - 1, 128, 8);
            if (fileCount > VISIBLE_ITEMS) Display::DrawScrollbar(SCROLLBAR_X, ITEM_Y0, VISIBLE_ITEMS * LINE_H, fileCount, cursor);
            Display::Flush();
        }

        void UIMenuPageTape::redrawRecord() {
            Display::Clear();
            Display::DrawString(0, ROW(0), recNameOk ? recName : "NO FREE NAME", FONT_UI);
            Display::DrawString(0, ROW(1), "START REC", FONT_UI);
            Display::DrawString(0, ROW(2), "CANCEL", FONT_UI);
            // Cursor selects the two actions (rows 1/2); the name row is
            // display-only.
            Display::InvertRect(0, ROW(cursor + 1) - 1, 128, 8);
            Display::Flush();
        }
    }
}
