/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2020 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

***************/

#include "UIMenuPageMidi.hpp"
#include "../Display.hpp"
#include "../DspProxy.hpp"
#include <cstdio>

using namespace CTAG::DRIVERS;

namespace CTAG {
    namespace CTRL {
        namespace {
            constexpr int kItems = 2;
            constexpr int kY0 = 10;
            constexpr int kLineH = 10;
        }

        void UIMenuPageMidi::init() {
            subPage = SP_MAIN;
            cursor = 0;
            // Pull the DSP's current enables (reply updates the cache and
            // the next redraw reflects it).
            DspProxy::RefreshMidiFlags();
        }

        void UIMenuPageMidi::deinit() {}

        void UIMenuPageMidi::onEncoder(int delta) {
            cursor += delta;
            if (cursor < 0) cursor = 0;
            if (cursor >= kItems) cursor = kItems - 1;
            doRedraw();
        }

        void UIMenuPageMidi::onButton(int btnId, bool longPress) {
            if (btnId == 2 && !longPress) {
                if (cursor == 0)
                    DspProxy::SetMidiFlags(!DspProxy::IsMidiUartEnabled(),
                                           DspProxy::IsMidiBleEnabled());
                else if (cursor == 1)
                    DspProxy::SetMidiFlags(DspProxy::IsMidiUartEnabled(),
                                           !DspProxy::IsMidiBleEnabled());
            }
            doRedraw();
        }

        void UIMenuPageMidi::doRedraw() {
            redrawMain();
        }

        void UIMenuPageMidi::redrawMain() {
            Display::Clear();
            char buf[32];
            snprintf(buf, sizeof(buf), "TRS In  [%s]",
                     DspProxy::IsMidiUartEnabled() ? "ON" : "OFF");
            Display::DrawString(0, kY0, buf, Display::FONT_5X7);
            snprintf(buf, sizeof(buf), "BLE  In [%s]",
                     DspProxy::IsMidiBleEnabled() ? "ON" : "OFF");
            Display::DrawString(0, kY0 + kLineH, buf, Display::FONT_5X7);
            Display::InvertRect(0, kY0 + cursor * kLineH, 128, 8);
            Display::Flush();
        }

        void UIMenuPageMidi::DebugStateJson(std::string &out) {
            out += "{\"midiUart\":";
            out += DspProxy::IsMidiUartEnabled() ? "1" : "0";
            out += ",\"midiBle\":";
            out += DspProxy::IsMidiBleEnabled() ? "1" : "0";
            out += ",\"cursor\":";
            out += std::to_string(cursor);
            out += "}";
        }
    }
}
