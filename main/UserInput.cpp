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
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define BTN1_GPIO 36

#define DEBOUNCE_MS 20
#define LONG_PRESS_MS 500
#define POLL_MS 10

namespace CTAG {
    namespace CTRL {
        static QueueHandle_t evQueue = nullptr;

        static void IRAM_ATTR btnIsr(void *arg) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            uint32_t gpio = (uint32_t)arg;
            xQueueSendFromISR(evQueue, &gpio, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
        }

        static void inputTask(void *) {
            CTAG::DRIVERS::encoder enc;
            uint32_t lastTs = 0;
            uint32_t pressTs = 0;
            bool wasPressed = false;
            uint32_t lastShortTs = 0;
            bool pendingShort = false;

            while (true) {
                uint32_t now = xTaskGetTickCount();

                // check pending short press timeout -> single click
                if (pendingShort && (now - lastShortTs) >= pdMS_TO_TICKS(300)) {
                    pendingShort = false;
                    InputEvent ev;
                    ev.type = InputEvent::BTN1_SHORT;
                    ev.delta = 0;
                    xQueueSend(evQueue, &ev, 0);
                }

                // read encoder (suppress during 300ms double-click window
                // so stale events don't queue ahead of the pending BTN)
                int delta = enc.ReadDelta();
                if (delta != 0 && !pendingShort) {
                    InputEvent ev;
                    ev.type = InputEvent::ENC_DELTA;
                    ev.delta = (int16_t)delta;
                    xQueueSend(evQueue, &ev, 0);
                }

                // process debounced button state (BTN1 only, GPIO0 is I2S MCLK)
                bool pressed = (gpio_get_level((gpio_num_t)BTN1_GPIO) == 0);
                if (pressed && !wasPressed) {
                    if (now - lastTs >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
                        pressTs = now;
                        wasPressed = true;
                    }
                } else if (!pressed && wasPressed) {
                    uint32_t held = now - pressTs;
                    if (held >= pdMS_TO_TICKS(LONG_PRESS_MS)) {
                        pendingShort = false;
                        InputEvent ev;
                        ev.type = InputEvent::BTN1_LONG;
                        ev.delta = 0;
                        xQueueSend(evQueue, &ev, 0);
                    } else if (held >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
                        if (pendingShort && (now - lastShortTs) < pdMS_TO_TICKS(300)) {
                            pendingShort = false;
                            InputEvent ev;
                            ev.type = InputEvent::BTN1_DOUBLE;
                            ev.delta = 0;
                            xQueueSend(evQueue, &ev, 0);
                        } else {
                            pendingShort = true;
                            lastShortTs = now;
                        }
                    }
                    lastTs = now;
                    wasPressed = false;
                }

                vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            }
        }

        void UserInput::Init() {
            evQueue = xQueueCreate(64, sizeof(InputEvent));

            gpio_config_t io_conf = {};
            io_conf.pin_bit_mask = (1ULL << BTN1_GPIO);
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.intr_type = GPIO_INTR_DISABLE; // no ISR yet, installed after audio init
            gpio_config(&io_conf);

            // do NOT configure GPIO0 (BTN2) — it's the I2S MCLK output on BBA

            xTaskCreatePinnedToCore(inputTask, "input_task", 2048, nullptr, tskIDLE_PRIORITY + 3, nullptr, 0);
        }

        void UserInput::EnableISR() {
            // Install GPIO ISR service and handlers — must be done after audio init
            // to avoid ISR firing inside I2S MCLK spinlock on ESP32 Rev3
            gpio_install_isr_service(0);
            gpio_isr_handler_add((gpio_num_t)BTN1_GPIO, btnIsr, (void*)BTN1_GPIO);
        }

        bool UserInput::GetEvent(InputEvent& ev, uint32_t timeoutMs) {
            return xQueueReceive(evQueue, &ev, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
        }

        bool UserInput::PeekEvent(InputEvent& ev) {
            return xQueuePeek(evQueue, &ev, 0) == pdTRUE;
        }
    }
}
