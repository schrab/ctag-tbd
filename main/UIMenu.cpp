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

#include "UIMenu.hpp"
#include "UserInput.hpp"
#include "Display.hpp"
#include "menupages/UIMenuPageHome.hpp"
#include "menupages/UIMenuPageMix.hpp"
#include "menupages/UIMenuPageParams.hpp"
#include "menupages/UIMenuPageTape.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using namespace CTAG::DRIVERS;

namespace CTAG {
    namespace CTRL {
        UIMenu::Panel UIMenu::currentPanel = UIMenu::PANEL_HOME;
        UIMenuPage *UIMenu::pages[4] = {nullptr, nullptr, nullptr, nullptr};
        bool UIMenu::inMenu = false;
        bool UIMenu::alt = false;
        int UIMenu::panelBarTimer = 0;

        void UIMenu::Init() {
            pages[PANEL_HOME] = new UIMenuPageHome();
            pages[PANEL_MIX] = new UIMenuPageMix();
            pages[PANEL_TAPE] = new UIMenuPageTape();
            pages[PANEL_PARAMS] = new UIMenuPageParams();
            for (auto &p : pages) p->init();
            // task created in main.cpp
        }

        void UIMenu::drawPanelBar() {
            if (panelBarTimer <= 0) return;
            for (int i = 0; i < 4; i++) {
                int x = i * 32;
                for (int px = 0; px < 31; px++)
                    Display::DrawPixel(x + px, 0, true);
                Display::DrawString(x + 2, 0, panelNames[i], Display::FONT_5X7);
            }
            // highlight active panel
            int ax = currentPanel * 32;
            Display::InvertRect(ax, 0, 32, 8);
            Display::Flush();
            panelBarTimer--;
        }

        void UIMenu::TaskFunction(void *) {
            UserInput::Init();
            while (1) {
                InputEvent ev;
                bool gotEv = UserInput::GetEvent(ev, 20);

                if (gotEv) {
                    if (ev.type == InputEvent::BTN1_SHORT) {
                        inMenu = !inMenu;
                        alt = false;
                        panelBarTimer = 20; // show bar for ~400ms
                        if (inMenu) {
                            pages[currentPanel]->doRedraw();
                        } else {
                            Display::Clear();
                        }
                    } else if (ev.type == InputEvent::BTN1_LONG) {
                        alt = true;
                    } else if (ev.type == InputEvent::ENC_DELTA && inMenu) {
                        int d = ev.delta;
                        int prevPanel = currentPanel;
                        if (d > 0) {
                            currentPanel = (Panel)((currentPanel + d) % 4);
                        } else if (d < 0) {
                            currentPanel = (Panel)((currentPanel + d + 4) % 4);
                        }
                        if (currentPanel != prevPanel) {
                            pages[prevPanel]->deinit();
                            pages[currentPanel]->init();
                            panelBarTimer = 20;
                        }
                        if (inMenu) pages[currentPanel]->doRedraw();
                        panelBarTimer = 20;
                    }
                }

                // redraw panel bar if timer active
                if (inMenu && panelBarTimer > 0) {
                    drawPanelBar();
                }

                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }
}
