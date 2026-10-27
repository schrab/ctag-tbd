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

// Panel controller entry point (P2 bring-up, SDD §8): SPI display, stub
// DspProxy, menu task, input task, USB bench channel. The link to the DSP
// replaces the stub in P3; UIMenu/UserInput are unchanged by that swap.

#include "Display.hpp"
#include "UIMenu.hpp"
#include "DebugLink.hpp"
#include "DspProxy.hpp"
#include "MidiLink.hpp"
#include "NetRest.hpp"
#include "BtMidiReceiver.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "uimcu";

extern "C" void app_main() {
    ESP_LOGI(TAG, "panel controller boot (C3)");

    CTAG::DRIVERS::Display::Init();
    CTAG::DRIVERS::Display::ShowFWVersion();

    CTAG::CTRL::DspProxy::Init();

    // UIMenu::Init creates the pages, seeds the ROOT view and draws it;
    // the task then starts input (UserInput::Init inside TaskFunction, the
    // same ordering the A1S uses) and runs the 20 ms loop.
    CTAG::CTRL::UIMenu::Init();
    CTAG::CTRL::DebugLink::Start();

    // UI task FIRST: WiFi + NimBLE eat a big chunk of the heap; the 8 KB
    // ui_menu stack must be allocated before those go up.
    if (xTaskCreatePinnedToCore(CTAG::CTRL::UIMenu::TaskFunction, "ui_menu", 8192,
                                nullptr, tskIDLE_PRIORITY + 3, nullptr, 0) != pdPASS) {
        ESP_LOGE(TAG, "could not create ui_menu task");
    }

    // P4: TRS MIDI jacks live on the panel; bytes relay over the link.
    CTAG::CTRL::MidiLink::Start();
    // P5: panel-side WiFi + REST bridge over the DspProxy cache.
    CTAG::CTRL::NetRest::Start();
    // P4: BLE-MIDI central (NimBLE); bytes ride the same midiIn relay.
    CTAG::DRIVERS::BtMidiReceiver::Init();
}
