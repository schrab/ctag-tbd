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
#include "input/UserInput.hpp"
#include "Display.hpp"
#include "DspProxy.hpp"
#include "menupages/UIMenuPageHome.hpp"
#include "menupages/UIMenuPageMix.hpp"
#include "menupages/UIMenuPageParams.hpp"
#include "menupages/UIMenuPageTape.hpp"
#include "menupages/UIMenuPageMod.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
static const char *TAG = "UIMenu";

using namespace CTAG::DRIVERS;

namespace CTAG {
    namespace CTRL {
        UIMenu::Panel UIMenu::currentPanel = UIMenu::PANEL_HOME;
        UIMenuPage *UIMenu::pages[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        UIMenu::NavState UIMenu::navState = UIMenu::ROOT;
        bool UIMenu::redrawNeeded = true;
        int UIMenu::screensaverTimer = 0;
        int UIMenu::pageTickCounter = 0;
        volatile bool UIMenu::fbShotPending = false;
        volatile bool UIMenu::fbShotReady = false;
        uint8_t UIMenu::fbShotBuf[CTAG::DRIVERS::Display::FRAMEBUFFER_SIZE];
        volatile int UIMenu::glyphTestPage = -1;

        void UIMenu::SetGlyphTestScreen(int page) {
            glyphTestPage = page;
            if (page < 0) redrawNeeded = true;
        }
        UIMenu::DisplayState UIMenu::displayState = UIMenu::AWAKE;
        bool UIMenu::screensaverEnabled = true; // 30 s idle -> panel off;
                                                // any input wakes (TaskFunction)
        volatile int UIMenu::pendingPanelRequest = -1;
        int UIMenu::pendingDeepHint = 0;

        void UIMenu::RequestPanelEdit(int panel, int deepHint) {
            if (panel < 0 || panel >= PANEL_COUNT) return;
            pendingPanelRequest = panel;
            pendingDeepHint = deepHint;
        }

        void UIMenu::Init() {
            ESP_LOGI(TAG, "Init: creating pages...");
            pages[PANEL_HOME] = new UIMenuPageHome();
            pages[PANEL_MIX] = new UIMenuPageMix();
            pages[PANEL_TAPE] = new UIMenuPageTape();
            pages[PANEL_MOD] = new UIMenuPageMod();
            pages[PANEL_PARAMS] = new UIMenuPageParams();
            ESP_LOGI(TAG, "Init: calling init on pages...");
            for (int i = 0; i < PANEL_COUNT; i++) pages[i]->init();
            navState = ROOT;
            redrawNeeded = true;
            // First paint happens in the task after the boot splash phase
            // (splash holds the screen until the DSP link handshakes).
            ESP_LOGI(TAG, "Init: done");
            // task created in main.cpp
            // UserInput::EnableISR() called after task creation in main.cpp
        }

        void UIMenu::GetDebugState(int &navOut, int &panelOut) {
            navOut = (int)navState;
            panelOut = (int)currentPanel;
        }

        bool UIMenu::IsPanelEdit() { return navState == PANEL_IN; }

        void UIMenu::RequestFramebufferShot() {
            fbShotReady = false;
            fbShotPending = true;
        }

        bool UIMenu::CopyFramebufferShot(uint8_t *dst, uint32_t timeoutMs) {
            if (dst == nullptr) return false;
            RequestFramebufferShot();
            uint32_t start = xTaskGetTickCount();
            while (!fbShotReady) {
                if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(timeoutMs)) return false;
                vTaskDelay(pdMS_TO_TICKS(2));
            }
            memcpy(dst, fbShotBuf, CTAG::DRIVERS::Display::FRAMEBUFFER_SIZE);
            return true;
        }

        void UIMenu::drawPanelBar() {
            // 1px norns bar: one 1x1 dot per panel, centered in equal
            // slots; the active panel is a 1px line of the classic bar
            // width. Slot centers sit on (128*(2i+1))/(2*SLOTS) so the
            // last item reaches toward the right edge without cropping.
            for (int i = 0; i < PANEL_COUNT; i++) {
                int cx = (128 * (2 * i + 1)) / (2 * PANEL_COUNT);
                if (i == currentPanel) {
                    Display::DrawRect(cx - 10, 0, 20, 1, true, true);
                } else {
                    Display::DrawRect(cx, 0, 1, 1, true, true);
                }
            }

            // CPU overload indicator at top-right corner: 1x1 dot, lit
            // only on overload.
            Display::DrawRect(124, 0, 1, 1, true,
                              CTAG::CTRL::DspProxy::GetCPUOverload());
            Display::Flush();
        }

        void UIMenu::drawGlyphTestScreen(int page) {
            // Bench glyph chart of the single UI font (norns 6x7): 14
            // columns x 8 rows at fixed 9x8 cells. Page 0 = ASCII
            // 0x20..0x7F; pages 1..3 = the extended range (138 glyphs,
            // 56 per page), drawn through DrawString's UTF-8 path so the
            // chart exercises exactly what the UI uses.
            Display::Clear();
            char buf[8];
            int total = Display::GetExtGlyphCount();
            for (int cell = 0; cell < 112; ++cell) {
                int col = cell % 14, row = cell / 14;
                if (page == 0) {
                    int c = 0x20 + cell;
                    if (c > 0x7F) break;
                    buf[0] = (char)c;
                    buf[1] = '\0';
                } else {
                    int idx = (page - 1) * 112 + cell;
                    if (idx >= total) break;
                    uint16_t cp = Display::GetExtGlyphCodepoint(idx);
                    // UTF-8 encode (ext range spans 1..3 byte forms up to
                    // the private-use 0xED64)
                    int n = 0;
                    if (cp < 0x80) {
                        buf[n++] = (char)cp;
                    } else if (cp < 0x800) {
                        buf[n++] = (char)(0xC0 | (cp >> 6));
                        buf[n++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        buf[n++] = (char)(0xE0 | (cp >> 12));
                        buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        buf[n++] = (char)(0x80 | (cp & 0x3F));
                    }
                    buf[n] = '\0';
                }
                Display::DrawString(col * 9, row * 8, buf,
                                    Display::FONT_NORNS_6X7);
            }
            Display::Flush();
        }

        void UIMenu::TaskFunction(void *) {
            ESP_LOGI(TAG, "TaskFunction: starting main loop");
            UserInput::Init();
            // Boot splash: hold title + build number + progress bar until
            // the DSP link handshakes (no-op on the stub build, which is
            // always online). The fill sweeps 0..100% and wraps; input
            // events are drained so boot-time encoder noise cannot queue
            // ahead of the first menu frame.
            if (!DspProxy::IsOnline()) {
                ESP_LOGI(TAG, "splash: waiting for DSP handshake");
                const TickType_t t0 = xTaskGetTickCount();
                constexpr uint32_t sweepMs = 1200;
                while (!DspProxy::IsOnline()) {
                    uint32_t ms = (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS;
                    Display::ShowSplash((int)((ms % sweepMs) * 100 / sweepMs));
                    InputEvent ev;
                    while (UserInput::GetEvent(ev, 0)) {}
                    vTaskDelay(pdMS_TO_TICKS(30));
                }
                ESP_LOGI(TAG, "splash: DSP online after %u ms",
                         (unsigned)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS));
                redrawNeeded = true;
            }
            bool dspLostShown = false;
            while (1) {
                // Framebuffer snapshot request (debug screenshot command).
                // Runs unconditionally every iteration and BEFORE the
                // link-lost shortcut below, so bench screenshots work while
                // the popup is on screen too.
                if (fbShotPending) {
                    Display::CopyFramebuffer(fbShotBuf);
                    fbShotPending = false;
                    fbShotReady = true;
                }

                // Glyph-chart hold (bench /debug/drawGlyphs): draw and stay
                // here until released (page < 0). Sits before every other
                // path so the chart is never overwritten by a redraw.
                if (glyphTestPage >= 0) {
                    drawGlyphTestScreen(glyphTestPage);
                    vTaskDelay(pdMS_TO_TICKS(20));
                    continue;
                }

                // DSP link watch: modal popup over the current screen
                // content while the link is down (link client flips
                // offline after 10 s without a complete DSP frame). Input
                // is drained so nothing stale fires after recovery; the
                // first frame back repaints the page via redrawNeeded.
                if (!DspProxy::IsOnline()) {
                    if (displayState == ASLEEP) {
                        Display::Wake();
                        displayState = AWAKE;
                        screensaverTimer = 0;
                        dspLostShown = false; // repaint popup after wake
                    }
                    if (!dspLostShown) {
                        dspLostShown = true;
                        ESP_LOGW(TAG, "DSP link lost");
                        Display::ShowDspLostPopup();
                    }
                    InputEvent ev;
                    while (UserInput::GetEvent(ev, 0)) {}
                    vTaskDelay(pdMS_TO_TICKS(20));
                    continue;
                }
                if (dspLostShown) {
                    dspLostShown = false;
                    redrawNeeded = true;
                }

                // Configured OLED brightness (from the DSP config push):
                // applied HERE, on the display task — the bit-banged panel
                // SPI has no cross-task lock. Also fires after a System
                // page commit pushes a new config back.
                int contrast = DspProxy::TakePendingOledContrast();
                if (contrast >= 0) {
                    // Boot floor: the stored value survives reboots, and a
                    // stored 0 (panel off) is a trap — the user could never
                    // see the menu to fix it. The panel always comes up
                    // visible; turning it to 0 stays a live-session choice
                    // (recoverable by turning the knob).
                    if (contrast < 10) contrast = 10;
                    Display::Contrast(contrast);
                }

                // Plugin-load overlay: while a load is in flight show the
                // boot-style loading bar INSTEAD of page content — the
                // previous plugin's params must never flash on screen
                // between the load command and the new params push. Clears
                // when the new table lands, on refusal, or after 5 s.
                if (DspProxy::IsLoadPending()) {
                    uint32_t ms = xTaskGetTickCount() * portTICK_PERIOD_MS
                                  - DspProxy::LoadPendingStartMs();
                    constexpr uint32_t sweepMs = 1200;
                    // Plugin load: the bare bar only — the full splash with
                    // title/version stays on the BOARD boot.
                    Display::ShowSplash((int)((ms % sweepMs) * 100 / sweepMs),
                                        /*withText=*/false);
                    InputEvent ev;
                    while (UserInput::GetEvent(ev, 0)) {} // no stale input past the load
                    redrawNeeded = true; // repaint the page the moment it lands
                    vTaskDelay(pdMS_TO_TICKS(20));
                    continue;
                }

                InputEvent ev;
                bool gotEv = UserInput::GetEvent(ev, 20);
                uint32_t now = xTaskGetTickCount();

                if (gotEv) {
                    if (displayState == ASLEEP) {
                        Display::Wake();
                        displayState = AWAKE;
                        screensaverTimer = 0;
                        redrawNeeded = true;
                    } else {
                        screensaverTimer = 0;
                        if (navState == ROOT) {
                            // Panel switching: E2 (ENC2_DELTA) per the C3 input
                            // split (SDD §5.1). E1 (ENC_DELTA) is also accepted
                            // here so host scripts written for the A1S single-
                            // encoder bench keep working.
                            if (ev.type == InputEvent::ENC_DELTA || ev.type == InputEvent::ENC2_DELTA) {
                                int steps = ev.delta;
                                if (steps > 1) steps /= 2;
                                if (steps < -1) steps /= 2;
                                if (steps == 0) steps = (ev.delta > 0) ? 1 : -1;
                                // Finite scroll: clamp at the ends instead of
                                // wrapping — scrolling left on MIX stays on
                                // MIX, right on the last panel stays there.
                                int prev = currentPanel;
                                int target = (int)currentPanel + steps;
                                if (target < 0) target = 0;
                                if (target > PANEL_COUNT - 1) target = PANEL_COUNT - 1;
                                currentPanel = (Panel)target;
                                if (currentPanel != prev) {
                                    pages[prev]->deinit();
                                    pages[currentPanel]->init();
                                }
                                redrawNeeded = true;
                            } else if (ev.type == InputEvent::BTN2_SHORT) {
                                navState = PANEL_IN;
                                redrawNeeded = true;
                            }
                        } else {
                            // Two-encoder split (physical row under the OLED:
                            // BACK, OK, then encoders left→right). Hardware
                            // naming: E1 "(value)" is the RIGHT encoder,
                            // E2 "(panel)" the LEFT one:
                            // ENC2_DELTA = LEFT encoder  (E2) = list scroll
                            // ENC_DELTA  = RIGHT encoder (E1) = live value edit
                            if (ev.type == InputEvent::ENC2_DELTA) {
                                pages[currentPanel]->onEncoder(ev.delta);
                            } else if (ev.type == InputEvent::ENC_DELTA) {
                                pages[currentPanel]->onValueEncoder(ev.delta);
                            } else if (ev.type == InputEvent::BTN1_SHORT || ev.type == InputEvent::BTN1_LONG) {
                                if (!pages[currentPanel]->onBack()) {
                                    navState = ROOT;
                                    redrawNeeded = true;
                                }
                            } else if (ev.type == InputEvent::BTN2_SHORT) {
                                pages[currentPanel]->onButton(2, false);
                            } else if (ev.type == InputEvent::BTN2_LONG) {
                                pages[currentPanel]->onButton(2, true);
                            }
                        }
                    }
                } else {
                    screensaverTimer++;
                    // Bring-up: the screensaver is disabled by default — it
                    // kept masquerading as a dead panel. Re-enable via the
                    // bench once the hardware is proven.
                    if (screensaverEnabled && displayState == AWAKE &&
                        screensaverTimer >= SCREENSAVER_TIMEOUT) {
                        Display::Sleep();
                        displayState = ASLEEP;
                    }
                }

                // Apply a page-requested panel switch (HOME jumps to PARAMS
                // after a plugin/favorite load). deinit/init runs here, off
                // the page's own event handler, so dispatch never touches a
                // page mid-teardown; the redraw below then paints the new
                // page in the same iteration.
                if (pendingPanelRequest >= 0) {
                    int target = pendingPanelRequest;
                    pendingPanelRequest = -1;
                    if (target != currentPanel) {
                        pages[currentPanel]->deinit();
                        currentPanel = (Panel)target;
                        pages[currentPanel]->init();
                    }
                    navState = PANEL_IN;
                    pages[currentPanel]->onDeepEnter(pendingDeepHint);
                    redrawNeeded = true;
                }

                // Redraw if needed
                if (displayState != ASLEEP && redrawNeeded) {
                    if (navState == ROOT) {
                        // Same single-flush composition as the tick path
                        // below: page content first (flush held), then the
                        // nav bar on top, one physical refresh.
                        Display::HoldFlush(true);
                        pages[currentPanel]->doRedraw();
                        Display::HoldFlush(false);
                        drawPanelBar();
                    } else {
                        pages[currentPanel]->doRedraw();
                    }
                    redrawNeeded = false;
                }

                // Periodic page refresh. Every page that is on screen gets
                // ticked — at ROOT the selected panel's content is visible
                // too, so a live page (VU meters, Marbles indicators, link
                // state) must not freeze just because it was not entered.
                // Pages opt in via wantsTick() and change-detect internally
                // so an idle screen costs no SPI traffic (Flush is
                // dirty-page gated).
                if (displayState == AWAKE && pages[currentPanel]->wantsTick()) {
                    if (++pageTickCounter >= TICK_DIVIDER) {
                        pageTickCounter = 0;
                        if (navState == ROOT) {
                            // Compose one frame: the page's own flush is held
                            // so its Clear() doesn't put a bar-less frame on
                            // the panel before the bar is drawn on top.
                            // Without this the nav bar flickers at the tick
                            // rate on live pages (MIX/MOD).
                            Display::HoldFlush(true);
                            pages[currentPanel]->onTick();
                            Display::HoldFlush(false);
                            drawPanelBar();
                        } else {
                            pages[currentPanel]->onTick();
                        }
                    }
                } else {
                    pageTickCounter = 0;
                }

                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }
}
