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
        // P6 cutover: the DSP-side /debug/* DISPATCHER. The panel's DebugLink
        // is the bench console now (and LinkEndpoint routes the panel's
        // /debug commands here), so the old UART0 task is gone. Kept
        // commands are the DSP-side set: getAudioStats, setActivePlugin,
        // getPluginParams, setPluginParam, getConfig, setConfigValue,
        // setElementsMode, setGrainCap/setHelperGrainCap, getCodecReg,
        // setLinkBaud. UI-side commands (getUiState, framebuffer,
        // injectEvent) live on the C3's DebugLink.
        class DebugUI final {
        public:
            DebugUI() = delete;

            // Execute one command document. Returns true if it was a /debug
            // command, in which case `response` holds the reply.
            static bool handleCommand(const std::string &doc, std::string &response);
        };
    }
}
