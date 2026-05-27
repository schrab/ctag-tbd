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

#include "UIMenuPageBtMidi.hpp"
#include "Display.hpp"
#include "BtMidiReceiver.hpp"
#include <cstring>
#include <cstdio>

using namespace CTAG::DRIVERS;

namespace CTAG {
    namespace CTRL {
        void UIMenuPageBtMidi::init() {
            subPage = SP_MAIN;
            cursor = 0;
            scrollOffset = 0;
        }

        void UIMenuPageBtMidi::deinit() {}

        void UIMenuPageBtMidi::onEncoder(int delta) {
            if (subPage == SP_MAIN) {
                cursor += delta;
                if (cursor < 0) cursor = 0;
                if (cursor > 2) cursor = 2;
            } else if (subPage == SP_SCAN) {
                int n = BtMidiReceiver::GetDeviceCount();
                int newCursor = cursor + delta;
                if (newCursor < 0) newCursor = 0;
                if (newCursor >= n) newCursor = (n > 0) ? n - 1 : 0;
                cursor = newCursor;
                if (cursor - scrollOffset < 0) scrollOffset = cursor;
                if (cursor - scrollOffset >= 6) scrollOffset = cursor - 5;
                if (scrollOffset < 0) scrollOffset = 0;
                if (n > 0 && scrollOffset > n - 6) scrollOffset = n - 6;
                if (scrollOffset < 0) scrollOffset = 0;
            }
            doRedraw();
        }

        void UIMenuPageBtMidi::onButton(int btnId, bool longPress) {
            if (subPage == SP_MAIN) {
                if (btnId == 2 && longPress) {
                    if (cursor == 0) {
                        BtMidiReceiver::StartScan();
                        subPage = SP_SCAN;
                        cursor = 0;
                        scrollOffset = 0;
                    } else if (cursor == 1 && BtMidiReceiver::IsConnected()) {
                        BtMidiReceiver::Disconnect();
                    }
                } else if (btnId == 2 && !longPress) {
                    // handled by UIMenu as panel enter
                }
            } else if (subPage == SP_SCAN) {
                if (btnId == 2 && !longPress && !BtMidiReceiver::IsScanning()) {
                    int n = BtMidiReceiver::GetDeviceCount();
                    if (n > 0 && cursor < n) {
                        BtMidiReceiver::Connect(cursor);
                    }
                    subPage = SP_MAIN;
                    cursor = 0;
                }
            }
            doRedraw();
        }

        bool UIMenuPageBtMidi::onBack() {
            if (subPage == SP_SCAN) {
                if (BtMidiReceiver::IsScanning()) BtMidiReceiver::StopScan();
                subPage = SP_MAIN;
                cursor = 0;
                doRedraw();
                return true;
            }
            return false;
        }

        void UIMenuPageBtMidi::doRedraw() {
            if (subPage == SP_MAIN) redrawMain();
            else if (subPage == SP_SCAN) redrawScan();
        }

        void UIMenuPageBtMidi::redrawMain() {
            Display::Clear();
            char buf[32];
            int y = 3;

            snprintf(buf, sizeof(buf), "SCAN%s",
                     BtMidiReceiver::IsScanning() ? " (running)" : "");
            Display::DrawString(0, y, buf, Display::FONT_5X7);
            y += 9;

            snprintf(buf, sizeof(buf), "Disconnect%s",
                     BtMidiReceiver::IsConnected() ? "" : " (none)");
            Display::DrawString(0, y, buf, Display::FONT_5X7);
            y += 9;

            snprintf(buf, sizeof(buf), "Status: %s",
                     BtMidiReceiver::IsConnected() ? "CONNECTED" : "IDLE");
            Display::DrawString(0, y, buf, Display::FONT_5X7);

            Display::InvertRect(0, 3 + cursor * 9, 128, 8);
            Display::Flush();
        }

        void UIMenuPageBtMidi::redrawScan() {
            Display::Clear();
            int n = BtMidiReceiver::GetDeviceCount();

            if (BtMidiReceiver::IsScanning()) {
                Display::DrawString(0, 3, "Scanning...", Display::FONT_5X7);
            } else {
                Display::DrawString(0, 3, "Scan complete", Display::FONT_5X7);
            }

            if (n == 0) {
                Display::DrawString(0, 24, "No devices found", Display::FONT_5X7);
            } else {
                int visible = n - scrollOffset;
                if (visible > 6) visible = 6;
                for (int i = 0; i < visible; i++) {
                    int idx = scrollOffset + i;
                    const BtDeviceInfo *d = BtMidiReceiver::GetDevice(idx);
                    if (!d) continue;
                    int y = 3 + i * 9;
                    Display::DrawString(0, y, d->name[0] ? d->name : "(unnamed)", Display::FONT_5X7);
                }
                int cy = 3 + (cursor - scrollOffset) * 9;
                Display::InvertRect(0, cy, 128, 8);
                if (n > 6)
                    Display::DrawScrollbar(126, 3, 54, n, cursor);
            }

            Display::Flush();
        }
    }
}
