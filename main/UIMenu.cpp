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
#include "esp_log.h"
static const char *TAG = "UIMenu";

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
            ESP_LOGI(TAG, "Init: creating pages...");
            pages[PANEL_HOME] = new UIMenuPageHome();
            pages[PANEL_MIX] = new UIMenuPageMix();
            pages[PANEL_TAPE] = new UIMenuPageTape();
            pages[PANEL_PARAMS] = new UIMenuPageParams();
#if CONFIG_BT_ENABLED
            pages[PANEL_BT] = new UIMenuPageBtMidi();
#endif
            ESP_LOGI(TAG, "Init: calling init on pages...");
            for (int i = 0; i < PANEL_COUNT; i++) pages[i]->init();
            navState = ROOT;
            redrawNeeded = true;
            panelBarTimer = 50; // ~1s
            ESP_LOGI(TAG, "Init: done");
            // task created in main.cpp
        }

        void UIMenu::drawPanelBar() {
            if (panelBarTimer <= 0) return;

            // Norns-style indicator bar at top: 20px wide rectangles
            int segWidth = 128 / PANEL_COUNT;
            int rectW = 20;
            for (int i = 0; i < PANEL_COUNT; i++) {
                int rx = i * segWidth + (segWidth - rectW) / 2;
                if (i == currentPanel) {
                    // Active: filled rectangle
                    Display::DrawRect(rx, 0, rectW, 3, true, true);
                } else {
                    // Inactive: 1px border rectangle
                    Display::DrawRect(rx, 0, rectW, 3, false, true);
                }
            }

            Display::Flush();
            panelBarTimer--;
        }

        void UIMenu::TaskFunction(void *) {
            ESP_LOGI(TAG, "TaskFunction: initializing UserInput...");
            UserInput::Init();
            ESP_LOGI(TAG, "TaskFunction: entering main loop");
            int loopCount = 0;
            while (1) {
                loopCount++;
                if (loopCount % 50 == 0) ESP_LOGD(TAG, "alive, loop=%d", loopCount);
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
