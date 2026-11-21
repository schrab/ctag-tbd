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
#include "ctagDataModelBase.hpp"
#include <iostream>
#include <memory>
#include <vector>

using namespace std;
using namespace rapidjson;

namespace CTAG {
    namespace SP {
        class ctagSPDataModel : public ctagDataModelBase {
        public:
            ctagSPDataModel(const string &id, const bool isStereo);

            ~ctagSPDataModel();

            const char *GetCStrJSONParams();

            void LoadPreset(const int num);

            // set a preset value, "which" can be current, cv, trig
            void SetParamValue(const string &id, const string &key, const int val);

            int GetParamValue(const string &id, const string &key);

            // True when the active preset carries an entry for this
            // parameter id — loadPresetInternal skips absent keys so the
            // plugin's C++ defaults survive (servando b69cb179).
            bool HasParam(const string &id);

            // Returns the actual stored index (append indices are clamped),
            // or -1 when the preset model cannot be saved.
            int SavePreset(const string &name, const int number);

            const char *GetCStrJSONPresets(); // all presets
            const char *GetCStrJSONAllPresetData(); // current preset

            std::string GetActivePluginParameters(); // active preset which contains non stored values
            void SetActivePluginParameters(const std::string &preset);

            bool IsParamTrig(const string &id);

            bool IsParamCV(const string &id);

            void PrintSelf();

        private:
            void recursiveFindAndInsert(const Value &paramF, Value &paramI);

            // Append default entries (from the mui spec's "def" member, 0
            // when absent) for registered params the loaded preset does not
            // carry — presets saved before a parameter existed would
            // otherwise silently drop every edit of it.
            void PreFillMissingParams(rapidjson::Value &params,
                                      const rapidjson::Value &specs,
                                      rapidjson::Document::AllocatorType &alloc);

            // merge ui and preset models
            void mergeModels();

            Document mui, mp;
            string mpFileName, muiFileName;
            Document activePreset;
            bool paramsDirty = true;
            bool presetsDirty = true;
            string cachedParamsJson;
            string cachedPresetsJson;
        };
    }
}
