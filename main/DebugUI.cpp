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

#include "DebugUI.hpp"
#include "Display.hpp"
#include "UIMenu.hpp"
#include "UserInput.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rapidjson/document.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/uart.h"
#include <cstdio>
#include <cstring>
#include <cstdarg>

using namespace std;
using namespace rapidjson;

namespace {
    const char *TAG = "DebugUI";
    const char STX = 0x02;
    const char ETX = 0x03;
    // Guard against a runaway host sending an unbounded document. The framebuffer
    // reply is ~2.1 KB, so this must stay comfortably above that.
    constexpr size_t MAX_DOC = 4096;
    // FREERTOS_HZ is 100, so pdMS_TO_TICKS(2) rounds to ZERO ticks -- vTaskDelay(0)
    // only yields, and this task would spin and starve IDLE into task_wdt. Always
    // block for at least one full tick.
    constexpr TickType_t POLL_TICKS = 1;
}

namespace CTAG {
    namespace CTRL {
        static TaskHandle_t hDebugTask = nullptr;
        static bool loggingRewired = false;

        // After the UART0 driver is installed, the ROM console and the driver
        // both fight over the same peripheral. Point ESP_LOG at the driver so
        // logging keeps working over the same wire the host uses.
        static int uartLogWrite(const char *format, va_list args) {
            static char line[256];
            int len = vsnprintf(line, sizeof(line), format, args);
            if (len > 0) uart_write_bytes(UART_NUM_0, line, (size_t)len);
            return len;
        }

        static const char *panelName(int panel) {
            switch (panel) {
                case 0: return "MIX";
                case 1: return "TAPE";
                case 2: return "HOME";
                case 3: return "MOD";
                case 4: return "PARAMS";
                case 5: return "MIDI";
                default: return "?";
            }
        }

        // uart_write_bytes() busy-waits on TX ring space WITHOUT yielding, so a
        // partially-full ring plus a 2 KB reply spins at our priority and starves
        // IDLE -> task_wdt. Wait for the ring to drain (this blocks on event bits
        // and therefore yields properly), then push the whole frame in one call:
        // the 4 KB TX ring was sized to hold it, so that call cannot block.
        static void writeFramed(const string &payload) {
            string frame;
            frame.reserve(payload.size() + 2);
            frame.push_back(STX);
            frame.append(payload);
            frame.push_back(ETX);

            while (uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(20)) != ESP_OK) {
                vTaskDelay(POLL_TICKS);
            }
            uart_write_bytes(UART_NUM_0, frame.data(), frame.size());
        }

        bool DebugUI::handleCommand(const string &doc, string &response) {
            Document d;
            d.Parse(doc);
            if (d.HasParseError() || !d.IsObject() || !d.HasMember("cmd")) return false;

            string s(d["cmd"].GetString());
            if (s.rfind("/debug/", 0) != 0) return false; // not ours

            if (s == "/debug/getDisplayFramebuffer") {
                // Raw OLED framebuffer as hex. 1024 bytes, page-major, bit 0 =
                // top row of the page, byte = (y >> 3) * 128 + x. This is the
                // exact byte layout Display::Flush() hands the SSD1309. The copy
                // is taken inside the UIMenu task so it can never be torn.
                const int fbsz = DRIVERS::Display::FRAMEBUFFER_SIZE;
                uint8_t *fbBuf = (uint8_t *)heap_caps_malloc(fbsz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (fbBuf == nullptr) fbBuf = (uint8_t *)malloc(fbsz);
                if (fbBuf == nullptr) {
                    response = "{\"error\":\"oom\"}";
                    return true;
                }
                if (!UIMenu::CopyFramebufferShot(fbBuf, 500)) {
                    free(fbBuf);
                    response = "{\"error\":\"fb timeout\"}";
                    return true;
                }
                static const char *hexDigits = "0123456789abcdef";
                string out;
                out.reserve(fbsz * 2 + 64);
                out = "{\"screenshot\":1,\"w\":128,\"h\":64,\"data\":\"";
                for (int i = 0; i < fbsz; i++) {
                    out.push_back(hexDigits[fbBuf[i] >> 4]);
                    out.push_back(hexDigits[fbBuf[i] & 0x0f]);
                }
                out += "\"}";
                free(fbBuf);
                response = out.c_str();
                return true;
            }

            if (s == "/debug/getUiState") {
                // Lets host-side scripts assert on real navigation state instead
                // of inferring it from pixels. Important because physical
                // encoder/button input races with injected events.
                int nav = 0, panel = 0;
                UIMenu::GetDebugState(nav, panel);
                response = "{\"nav\":" + to_string(nav)
                           + ",\"panel\":" + to_string(panel)
                           + ",\"panelName\":\"" + panelName(panel) + "\"}";
                return true;
            }

            if (s == "/debug/getMixState") {
                // The MIX page's internal state, so a no-op edit is detectable
                // without reading pixels.
                string pageState;
                UIMenuPage *page = UIMenu::GetPage(0); // PANEL_MIX
                if (page != nullptr) page->DebugStateJson(pageState);
                response = pageState.empty() ? "{\"error\":\"no mix page\"}" : pageState;
                return true;
            }

            if (s == "/debug/injectEvent") {
                // Semantic UI actions rather than raw button transitions, so a
                // host script drives the same paths a real press takes:
                //   ok   -> BTN2_SHORT -> page->onButton(2, false)
                //   mod  -> BTN2_LONG  -> page->onButton(2, true)
                //   back -> BTN1_SHORT -> page->onBack()
                //   enc  -> ENC_DELTA  -> page->onEncoder(delta)
                if (!d.HasMember("action") || !d["action"].IsString()) {
                    response = "{\"error\":\"missing action\"}";
                    return true;
                }
                const char *action = d["action"].GetString();
                int16_t delta = 0;
                int count = 1;
                if (d.HasMember("delta") && d["delta"].IsInt()) delta = (int16_t)d["delta"].GetInt();
                if (d.HasMember("count") && d["count"].IsInt()) count = d["count"].GetInt();
                if (count < 1) count = 1;
                if (count > 64) count = 64;

                InputEvent::Type type;
                if (strcmp(action, "ok") == 0) type = InputEvent::BTN2_SHORT;
                else if (strcmp(action, "mod") == 0) type = InputEvent::BTN2_LONG;
                else if (strcmp(action, "back") == 0) type = InputEvent::BTN1_SHORT;
                else if (strcmp(action, "enc") == 0) type = InputEvent::ENC_DELTA;
                else {
                    response = "{\"error\":\"unknown action\"}";
                    return true;
                }

                int sent = 0;
                for (int i = 0; i < count; i++) {
                    if (!UserInput::InjectEvent(type, delta)) break; // queue full
                    sent++;
                }
                response = "{\"sent\":" + to_string(sent) + "}";
                return true;
            }

            response = "{\"error\":\"unknown debug command\"}";
            return true;
        }

        static void debugTask(void *) {
            string cmd;
            bool receiving = false;
            uint8_t rx[64];

            while (true) {
                // Talk to the UART driver directly rather than read()/write().
                // esp_vfs_read() on the console ignores O_NONBLOCK and blocks
                // inside uart_read() while holding a newlib stdio lock, which
                // starves IDLE and trips task_wdt. The console has already
                // installed a driver on UART0, so uart_get_buffered_data_len()
                // gives us a genuinely non-blocking readiness check and
                // uart_read_bytes() with a 0-tick timeout never blocks.
                size_t avail = 0;
                if (uart_get_buffered_data_len(UART_NUM_0, &avail) != ESP_OK || avail == 0) {
                    vTaskDelay(POLL_TICKS);
                    continue;
                }

                int want = (int)(avail < sizeof(rx) ? avail : sizeof(rx));
                int got = uart_read_bytes(UART_NUM_0, rx, want, 0);
                if (got <= 0) {
                    vTaskDelay(POLL_TICKS);
                    continue;
                }

                for (int i = 0; i < got; i++) {
                    char c = (char)rx[i];
                    if (c == STX) {
                        receiving = true;
                        cmd.clear();
                    } else if (c == ETX) {
                        if (receiving && cmd.size() <= MAX_DOC) {
                            string response;
                            if (DebugUI::handleCommand(cmd, response)) {
                                writeFramed(response);
                            }
                        }
                        receiving = false;
                        cmd.clear();
                    } else if (receiving) {
                        if (cmd.size() < MAX_DOC) cmd.push_back(c);
                    }
                }
            }
        }

        void DebugUI::Start() {
            if (hDebugTask != nullptr) return;

            // A WIFI_UI build uses the ROM console, which is OUTPUT ONLY: no UART
            // driver is installed, so uart_read_bytes() and STDIN reads cannot
            // work at all and there is no way to receive a command. Installing
            // the driver is the only fix, and it is why the Kconfig makes
            // SERIAL_UI exclusive with WIFI_UI (SerialAPI installs it there).
            // We take the same route, then hand ESP_LOG to the driver so log
            // output keeps flowing on the wire.
            if (!loggingRewired) {
                uart_config_t uc;
                uc.baud_rate = 115200;
                uc.data_bits = UART_DATA_8_BITS;
                uc.parity = UART_PARITY_DISABLE;
                uc.stop_bits = UART_STOP_BITS_1;
                uc.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
                uc.rx_flow_ctrl_thresh = 0;
                uc.source_clk = UART_SCLK_DEFAULT;

                esp_err_t err = uart_driver_install(UART_NUM_0, 4096, 4096, 0, nullptr, 0);
                if (err != ESP_OK) {
                    // Already installed (e.g. a SERIAL_UI build where SerialAPI
                    // got there first) is fine; we only need RX to be possible.
                    ESP_LOGW(TAG, "uart_driver_install: %s", esp_err_to_name(err));
                }
                uart_set_rx_timeout(UART_NUM_0, 1); // 1 tick, so a read returns fast
                uart_param_config(UART_NUM_0, &uc);

                size_t buffered = 0;
                if (uart_get_buffered_data_len(UART_NUM_0, &buffered) != ESP_OK) {
                    ESP_LOGE(TAG, "UART0 has no driver; debug channel unavailable");
                    return;
                }
                esp_log_set_vprintf(uartLogWrite);
                loggingRewired = true;
            }

            // 3 KB: the framebuffer reply is built on the heap, but RapidJSON
            // parsing of the incoming document plus framing still needs room.
            if (xTaskCreatePinnedToCore(debugTask, "debug_ui", 3072, nullptr,
                                        tskIDLE_PRIORITY + 1, &hDebugTask, 0) != pdPASS) {
                ESP_LOGW(TAG, "could not start debug UI task");
                hDebugTask = nullptr;
                return;
            }
            ESP_LOGI(TAG, "debug UI channel on UART0 (STX/ETX JSON)");
        }
    }
}
