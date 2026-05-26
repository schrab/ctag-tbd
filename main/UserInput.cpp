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
#define BTN2_GPIO 0

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
            uint32_t lastTs[2] = {0, 0};
            uint32_t pressTs[2] = {0, 0};
            bool wasPressed[2] = {false, false};

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

                // process debounced button state
                for (int i = 0; i < 2; i++) {
                    int gpio = (i == 0) ? BTN1_GPIO : BTN2_GPIO;
                    bool pressed = (gpio_get_level((gpio_num_t)gpio) == 0);

                    if (pressed && !wasPressed[i]) {
                        if (now - lastTs[i] >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
                            pressTs[i] = now;
                            wasPressed[i] = true;
                        }
                    } else if (!pressed && wasPressed[i]) {
                        uint32_t held = now - pressTs[i];
                        if (held >= pdMS_TO_TICKS(LONG_PRESS_MS)) {
                            InputEvent ev;
                            ev.type = (i == 0) ? InputEvent::BTN1_LONG : InputEvent::BTN2_LONG;
                            ev.delta = 0;
                            xQueueSend(evQueue, &ev, 0);
                        } else if (held >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
                            InputEvent ev;
                            ev.type = (i == 0) ? InputEvent::BTN1_SHORT : InputEvent::BTN2_SHORT;
                            ev.delta = 0;
                            xQueueSend(evQueue, &ev, 0);
                        }
                        lastTs[i] = now;
                        wasPressed[i] = false;
                    }
                }

                vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            }
        }

        void UserInput::Init() {
            evQueue = xQueueCreate(64, sizeof(InputEvent));

            gpio_config_t io_conf = {};
            io_conf.pin_bit_mask = (1ULL << BTN1_GPIO) | (1ULL << BTN2_GPIO);
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.intr_type = GPIO_INTR_DISABLE; // no ISR yet, installed after audio init
            gpio_config(&io_conf);

            // enable internal pull-up on GPIO0 (BOOT button has external pull but safe)
            gpio_set_pull_mode((gpio_num_t)BTN2_GPIO, GPIO_PULLUP_ONLY);

            xTaskCreatePinnedToCore(inputTask, "input_task", 2048, nullptr, tskIDLE_PRIORITY + 3, nullptr, 0);
        }

        void UserInput::EnableISR() {
            // Install GPIO ISR service and handlers — must be done after audio init
            // to avoid ISR firing inside I2S MCLK spinlock on ESP32 Rev3
            gpio_install_isr_service(0);
            gpio_isr_handler_add((gpio_num_t)BTN1_GPIO, btnIsr, (void*)BTN1_GPIO);
            gpio_isr_handler_add((gpio_num_t)BTN2_GPIO, btnIsr, (void*)BTN2_GPIO);
        }

        bool UserInput::GetEvent(InputEvent& ev, uint32_t timeoutMs) {
            return xQueueReceive(evQueue, &ev, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
        }
    }
}
