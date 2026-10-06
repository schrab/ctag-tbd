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

#pragma once

#include <string>

namespace CTAG {
    namespace CTRL {
        // Bring-up/debug channel for the physical UI, independent of which
        // product UI is configured (WIFI_UI or SERIAL_UI).
        //
        // Speaks the same STX/ETX JSON framing as SerialAPI on UART0, but goes
        // through the console VFS (STDIN/STDOUT) instead of installing a UART
        // driver. That matters: SerialAPI calls uart_driver_install(UART_NUM_0)
        // which is why the Kconfig makes WIFI_UI and SERIAL_UI mutually
        // exclusive — the console already owns that UART for logging. Using the
        // VFS lets this coexist with ESP_LOG and the REST server.
        //
        // Commands (see bin/dev_ui.py for the host driver):
        //   {"cmd":"/debug/getDisplayFramebuffer"}
        //   {"cmd":"/debug/injectEvent","action":"ok"|"mod"|"back"|"enc",
        //                  "delta":n,"count":n}
        class DebugUI final {
        public:
            DebugUI() = delete;

            // Spawn the polling task. Safe to call once; no-op afterwards.
            static void Start();

            // Execute one command document. Returns true if it was a /debug
            // command, in which case `response` holds the reply. Shared with
            // SerialAPI so there is only one implementation.
            static bool handleCommand(const std::string &doc, std::string &response);
        };
    }
}
