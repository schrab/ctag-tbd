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
        UIMenu::NavState UIMenu::navState = UIMenu::ROOT;
        bool UIMenu::redrawNeeded = true;
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
            navState = ROOT;
            redrawNeeded = true;
            panelBarTimer = 50; // ~1s
            // task created in main.cpp
        }

        void UIMenu::drawPanelBar() {
            if (panelBarTimer <= 0) return;

            // Dashed horizontal line across top
            for (int x = 0; x < 128; x += 3) {
                Display::DrawPixel(x, 0, true);
            }

            // Segment indicators per panel
            int segWidth = 128 / PANEL_COUNT;
            for (int i = 0; i < PANEL_COUNT; i++) {
                int cx = i * segWidth + segWidth / 2;
                if (i == currentPanel) {
                    // Active: 4px bar
                    for (int dx = -2; dx <= 2; dx++)
                        Display::DrawPixel(cx + dx, 0, true);
                } else {
                    // Inactive: single dot
                    Display::DrawPixel(cx, 0, true);
                }
            }

            Display::Flush();
            panelBarTimer--;
        }

        void UIMenu::TaskFunction(void *) {
            UserInput::Init();
            while (1) {
                InputEvent ev;
                bool gotEv = UserInput::GetEvent(ev, 20);

                if (gotEv) {
                    if (navState == ROOT) {
                        if (ev.type == InputEvent::ENC_DELTA) {
                            int steps = ev.delta;
                            // acceleration: fast rotation = bigger jumps
                            if (steps > 1) steps /= 2;
                            if (steps < -1) steps /= 2;
                            if (steps == 0) steps = (ev.delta > 0) ? 1 : -1;
                            int prev = currentPanel;
                            currentPanel = (Panel)((currentPanel + steps + PANEL_COUNT) % PANEL_COUNT);
                            if (currentPanel != prev) {
                                pages[prev]->deinit();
                                pages[currentPanel]->init();
                            }
                            panelBarTimer = 50;
                            redrawNeeded = true;
                        } else if (ev.type == InputEvent::BTN2_SHORT) {
                            navState = PANEL_IN;
                            redrawNeeded = true;
                        }
                        // BTN1_SHORT/LONG in ROOT = no-op (future use)
                    } else {
                        // PANEL_IN
                        if (ev.type == InputEvent::ENC_DELTA) {
                            pages[currentPanel]->onEncoder(ev.delta);
                        } else if (ev.type == InputEvent::BTN2_SHORT) {
                            pages[currentPanel]->onButton(2, false);
                        } else if (ev.type == InputEvent::BTN2_LONG) {
                            pages[currentPanel]->onButton(2, true);
                        } else if (ev.type == InputEvent::BTN1_SHORT) {
                            if (!pages[currentPanel]->onBack()) {
                                navState = ROOT;
                                redrawNeeded = true;
                                panelBarTimer = 50;
                            }
                        } else if (ev.type == InputEvent::BTN1_LONG) {
                            pages[currentPanel]->onButton(1, true);
                        }
                    }
                }

                // Redraw if needed
                if (redrawNeeded) {
                    if (navState == ROOT) {
                        pages[currentPanel]->doRedraw();
                        drawPanelBar();
                    } else {
                        pages[currentPanel]->doRedraw();
                    }
                    redrawNeeded = false;
                }

                // Decrement panel bar timer each tick when visible
                if (navState == ROOT && panelBarTimer > 0) {
                    panelBarTimer--;
                }

                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }
}
