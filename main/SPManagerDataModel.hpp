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


#pragma  once

#include <string>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "ctagDataModelBase.hpp"
#include <iostream>
#include <memory>
#include <vector>

using namespace std;
using namespace rapidjson;

namespace CTAG {
    namespace AUDIO {
        class SPManagerDataModel final : public CTAG::SP::ctagDataModelBase {
        public:
            SPManagerDataModel();

            ~SPManagerDataModel();

            const char *GetCStrJSONSoundProcessors();

            const char *GetCStrJSONConfiguration();

            const char *GetCStrJSONSoundProcessorPresets(const string &id);

            void SetCStrJSONSoundProcessorPreset(const char *id, const char* data);

            string GetActiveProcessorID(const int chan);

            void SetConfigurationFromJSON(const string &data);

            string GetConfigurationData(const string &id);

            string GetNetworkConfigurationData(const string &which);

            void ResetNetworkConfiguration();

            void SetActivePluginID(const string &id, const int chan);

            int GetActivePatchNum(const int chan);

            void SetActivePatchNum(const int patchNum, const int chan);

            bool IsStereo(const string &id);

            void PrintSelf();

            bool HasPluginID(string const &id);

            // Drop a plugin from the available list and delete its descriptor
            // + preset files from SPIFFS (used by the removeProcessor API).
            bool RemovePlugin(const string &id);

            // Deferred model persistence: the model JSON lives on the same
            // flash as the code, and a synchronous storeJSON stalls both
            // cores' caches (~300 ms — the I2S DMA underruns and the audio
            // mutes). Runtime changes mark the store pending; the link
            // tick flushes once 5 s have passed without a change, so a
            // burst of settings changes costs ONE stall, well after the
            // user left the menu. Boot-time stores stay immediate.
            void MarkStorePending();
            void FlushStoreIfDue();

        private:
            void getSoundProcessors();

            void validateActiveProcessors();

            void validatePatches();

            Document m;
            bool processorsDirty = true;
            bool configDirty = true;
            bool storePending = false;
            uint32_t lastStoreRequestMs = 0;
            string cachedProcessorsJson;
            string cachedConfigJson;
#ifndef TBD_SIM
            const string MODELJSONFN = "/spiffs/data/spm-config.jsn";
#else
            const string MODELJSONFN = "../../spiffs_image/data/spm-config.jsn";
#endif
        };
    }
}

