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
#if CONFIG_BT_ENABLED
#include "menupages/UIMenuPageBtMidi.hpp"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using namespace CTAG::DRIVERS;

namespace CTAG {
    namespace CTRL {
        UIMenu::Panel UIMenu::currentPanel = UIMenu::PANEL_HOME;
#if CONFIG_BT_ENABLED
        UIMenuPage *UIMenu::pages[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
#else
        UIMenuPage *UIMenu::pages[4] = {nullptr, nullptr, nullptr, nullptr};
#endif
        bool UIMenu::inMenu = false;
        bool UIMenu::alt = false;
        int UIMenu::panelBarTimer = 0;

        void UIMenu::Init() {
            pages[PANEL_HOME] = new UIMenuPageHome();
            pages[PANEL_MIX] = new UIMenuPageMix();
            pages[PANEL_TAPE] = new UIMenuPageTape();
            pages[PANEL_PARAMS] = new UIMenuPageParams();
#if CONFIG_BT_ENABLED
            pages[PANEL_BT] = new UIMenuPageBtMidi();
#endif
            for (int i = 0; i < PANEL_COUNT; i++) pages[i]->init();
            // task created in main.cpp
        }

        void UIMenu::drawPanelBar() {
            if (panelBarTimer <= 0) return;
            for (int i = 0; i < PANEL_COUNT; i++) {
                int x = i * (128 / PANEL_COUNT);
                for (int px = 0; px < (128 / PANEL_COUNT) - 1; px++)
                    Display::DrawPixel(x + px, 0, true);
                Display::DrawString(x + 2, 0, panelNames[i], Display::FONT_5X7);
            }
            // highlight active panel
            int ax = currentPanel * (128 / PANEL_COUNT);
            Display::InvertRect(ax, 0, 128 / PANEL_COUNT, 8);
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
                        panelBarTimer = 20;
                        if (inMenu) {
                            pages[currentPanel]->doRedraw();
                        } else {
                            Display::Clear();
                        }
                    } else if (ev.type == InputEvent::BTN1_LONG) {
                        alt = true;
                    } else if (ev.type == InputEvent::BTN2_SHORT && inMenu) {
                        pages[currentPanel]->onButton(2, false);
                    } else if (ev.type == InputEvent::BTN2_LONG && inMenu) {
                        pages[currentPanel]->onButton(2, true);
                    } else if (ev.type == InputEvent::ENC_DELTA) {
                        if (inMenu) {
                            // small deltas → page, large deltas → panel switch
                            int d = ev.delta;
                            if (d < -1 || d > 1) {
                                int prevPanel = currentPanel;
                                int dir = (d > 0) ? 1 : -1;
                                currentPanel = (Panel)((currentPanel + dir + PANEL_COUNT) % PANEL_COUNT);
                                if (currentPanel != prevPanel) {
                                    pages[prevPanel]->deinit();
                                    pages[currentPanel]->init();
                                }
                                panelBarTimer = 20;
                            } else {
                                pages[currentPanel]->onEncoder(d);
                            }
                        }
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
