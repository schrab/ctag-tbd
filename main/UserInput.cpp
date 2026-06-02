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

#include "UserInput.hpp"
#include "encoder.hpp"
#include "driver/adc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define DEBOUNCE_MS 20
#define LONG_PRESS_MS 500
#define POLL_MS 10

namespace CTAG {
    namespace CTRL {
        static QueueHandle_t evQueue = nullptr;

        enum BtnADCState { BTN_NONE, BTN_SW1, BTN_SW2, BTN_BOTH };

        static BtnADCState currentADCState = BTN_NONE;

        static BtnADCState readADCState(BtnADCState prev, int adc) {
            switch (prev) {
                case BTN_NONE:
                    if (adc < 600) return BTN_SW1;
                    if (adc < 950) return BTN_BOTH;
                    if (adc < 1540) return BTN_SW2;
                    if (adc < 2600) return BTN_SW1;
                    return BTN_NONE;
                case BTN_SW1:
                    if (adc > 2600) return BTN_NONE;
                    if (adc < 950) return BTN_BOTH;
                    if (adc < 1540) return BTN_SW2;
                    return BTN_SW1;
                case BTN_SW2:
                    if (adc > 1700) return BTN_NONE;
                    if (adc < 600) return BTN_SW1;
                    if (adc < 950) return BTN_BOTH;
                    return BTN_SW2;
                case BTN_BOTH:
                    if (adc > 2600) return BTN_NONE;
                    if (adc > 1500) return BTN_SW1;
                    if (adc > 988) return BTN_SW2;
                    return BTN_BOTH;
            }
            return BTN_NONE;
        }

        static void inputTask(void *) {
            CTAG::DRIVERS::encoder enc;
            uint32_t lastDebounceTs = 0;
            uint32_t sw1PressTs = 0;
            uint32_t sw2PressTs = 0;

            while (true) {
                uint32_t now = xTaskGetTickCount();

                // read encoder
                int delta = enc.ReadDelta();
                if (delta != 0) {
                    InputEvent ev;
                    ev.type = InputEvent::ENC_DELTA;
                    ev.delta = (int16_t)delta;
                    xQueueSend(evQueue, &ev, 0);
                }

                // read ADC buttons with 4-sample averaging
                int sum = 0;
                for (int i = 0; i < 4; i++) {
                    sum += adc1_get_raw(ADC1_CHANNEL_0);
                }
                int adc = sum / 4;
                BtnADCState newState = readADCState(currentADCState, adc);


                if (newState != currentADCState && (now - lastDebounceTs) >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
                    BtnADCState old = currentADCState;
                    InputEvent ev;
                    ev.delta = 0;

                    // Both -> None: no events
                    if (old == BTN_BOTH && newState == BTN_NONE) {
                        // nothing
                    }
                    // Both -> SW1: fresh timer for remaining SW1
                    else if (old == BTN_BOTH && newState == BTN_SW1) {
                        sw1PressTs = now;
                    }
                    // Both -> SW2: fresh timer for remaining SW2
                    else if (old == BTN_BOTH && newState == BTN_SW2) {
                        sw2PressTs = now;
                    }
                    // -> Both: no events, fresh timers
                    else if (newState == BTN_BOTH) {
                        if (old == BTN_SW1) {
                            sw2PressTs = now;
                        } else if (old == BTN_SW2) {
                            sw1PressTs = now;
                        } else {
                            sw1PressTs = now;
                            sw2PressTs = now;
                        }
                    }
                    // SW1 released (physical OK)
                    else if (old == BTN_SW1 && newState == BTN_NONE) {
                        ev.type = (now - sw1PressTs >= pdMS_TO_TICKS(LONG_PRESS_MS))
                            ? InputEvent::BTN2_LONG : InputEvent::BTN2_SHORT;
                        xQueueSend(evQueue, &ev, 0);
                    }
                    // SW1 pressed (physical OK)
                    else if (old == BTN_NONE && newState == BTN_SW1) {
                        sw1PressTs = now;
                    }
                    // SW1 -> SW2 (release OK, press BACK)
                    else if (old == BTN_SW1 && newState == BTN_SW2) {
                        ev.type = (now - sw1PressTs >= pdMS_TO_TICKS(LONG_PRESS_MS))
                            ? InputEvent::BTN2_LONG : InputEvent::BTN2_SHORT;
                        xQueueSend(evQueue, &ev, 0);
                        sw2PressTs = now;
                    }
                    // SW2 released (physical BACK)
                    else if (old == BTN_SW2 && newState == BTN_NONE) {
                        ev.type = (now - sw2PressTs >= pdMS_TO_TICKS(LONG_PRESS_MS))
                            ? InputEvent::BTN1_LONG : InputEvent::BTN1_SHORT;
                        xQueueSend(evQueue, &ev, 0);
                    }
                    // SW2 pressed (physical BACK)
                    else if (old == BTN_NONE && newState == BTN_SW2) {
                        sw2PressTs = now;
                    }
                    // SW2 -> SW1 (release BACK, press OK)
                    else if (old == BTN_SW2 && newState == BTN_SW1) {
                        ev.type = (now - sw2PressTs >= pdMS_TO_TICKS(LONG_PRESS_MS))
                            ? InputEvent::BTN1_LONG : InputEvent::BTN1_SHORT;
                        xQueueSend(evQueue, &ev, 0);
                        sw1PressTs = now;
                    }

                    currentADCState = newState;
                    lastDebounceTs = now;
                }

                vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            }
        }

        void UserInput::Init() {
            evQueue = xQueueCreate(64, sizeof(InputEvent));

            // Configure ADC1 channel 0 (GPIO36) for button sensing
            adc1_config_width(ADC_WIDTH_BIT_12);
            adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);

            xTaskCreatePinnedToCore(inputTask, "input_task", 2048, nullptr, tskIDLE_PRIORITY + 3, nullptr, 0);
        }

        // ISR not needed with ADC polling — GPIO36 is ADC only
        void UserInput::EnableISR() {}

        bool UserInput::GetEvent(InputEvent& ev, uint32_t timeoutMs) {
            return xQueueReceive(evQueue, &ev, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
        }

        bool UserInput::PeekEvent(InputEvent& ev) {
            return xQueuePeek(evQueue, &ev, 0) == pdTRUE;
        }
    }
}
