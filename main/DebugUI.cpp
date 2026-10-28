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

// P6 cutover: the A1S has no local UI and no UART bench channel anymore —
// the C3 panel's DebugLink is the console, and DSP /debug commands arrive
// over the panel link. DebugUI::handleCommand remains as the dispatcher
// LinkEndpoint routes /debug/* to; everything UI-side (framebuffer,
// navigation state, event injection) lives on the C3 now.

#include "DebugUI.hpp"
#include "SPManager.hpp"
#include "LinkEndpoint.hpp"
#include "codec.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/uart.h"
#include <cstdio>
#include <cstring>
#include <cstdarg>

using namespace std;
using namespace rapidjson;

// Mbeads debug hooks: grain-cap override/getters.
extern "C" int mbeads_debug_set_total_cap(int cap);
extern "C" int mbeads_debug_get_total_cap();
extern "C" int mbeads_debug_set_helper_cap(int cap);
extern "C" int mbeads_debug_get_helper_cap();
extern "C" int mbeads_debug_get_active_grains();
extern "C" int mbeads_debug_get_helper_active_grains();
extern "C" int mbeads_debug_get_helper_cycles();
extern "C" int mbeads_debug_get_helper_overload();
extern "C" int mbeads_debug_get_helper_stack_high_water();
extern "C" int mbeads_debug_get_core_cap();

// Elements second-core helper stats (distinct keys from Mbeads).
extern "C" int elements_debug_get_helper_cycles();
extern "C" int elements_debug_get_helper_max_cycles();
extern "C" int elements_debug_get_helper_overload();
extern "C" int elements_debug_get_helper_stack_high_water();
extern "C" int elements_debug_get_helper_resolution();
extern "C" int elements_debug_get_helper_misses();
extern "C" int elements_debug_get_mode_override();
extern "C" int elements_debug_set_mode_override(int modes);

namespace {
    const char *TAG = "DebugUI";
}

namespace CTAG {
    namespace CTRL {
        bool DebugUI::handleCommand(const string &doc, string &response) {
            Document d;
            d.Parse(doc);
            if (d.HasParseError() || !d.IsObject() || !d.HasMember("cmd")) return false;

            string s(d["cmd"].GetString());
            if (s.rfind("/debug/", 0) != 0) return false; // not ours

            if (s == "/debug/getAudioStats") {
                // Audio-task stack high-water mark plus the most recent
                // per-block CPU-overload flag. stackHighWater is the
                // minimum-ever-free stack in bytes (0 before the task exists).
                response = "{\"stackHighWater\":" +
                           to_string(AUDIO::SoundProcessorManager::GetAudioStackHighWaterBytes()) +
                           ",\"cpuOverload\":" +
                           to_string(AUDIO::SoundProcessorManager::GetCPUOverload() ? 1 : 0) +
                           ",\"audioCycles\":" +
                           to_string(AUDIO::SoundProcessorManager::GetLastAudioTaskCycles()) +
                           ",\"activeGrains\":" +
                           to_string(mbeads_debug_get_active_grains()) +
                           ",\"helperActiveGrains\":" +
                           to_string(mbeads_debug_get_helper_active_grains()) +
                           ",\"helperCycles\":" +
                           to_string(mbeads_debug_get_helper_cycles()) +
                           ",\"helperOverload\":" +
                           to_string(mbeads_debug_get_helper_overload()) +
                           ",\"helperStackHighWater\":" +
                           to_string(mbeads_debug_get_helper_stack_high_water()) +
                           ",\"coreGrainCap\":" +
                           to_string(mbeads_debug_get_core_cap()) +
                           ",\"helperGrainCap\":" +
                           to_string(mbeads_debug_get_helper_cap() >= 0 ? mbeads_debug_get_helper_cap() : -1) +
                           ",\"grainCapOverride\":" +
                           to_string(mbeads_debug_get_total_cap()) +
                           ",\"helperGrainCapOverride\":" +
                           to_string(mbeads_debug_get_helper_cap()) +
                           ",\"elementsHelperCycles\":" +
                           to_string(elements_debug_get_helper_cycles()) +
                           ",\"elementsHelperOverload\":" +
                           to_string(elements_debug_get_helper_overload()) +
                           ",\"elementsHelperStackHighWater\":" +
                           to_string(elements_debug_get_helper_stack_high_water()) +
                           ",\"elementsHelperMaxCycles\":" +
                           to_string(elements_debug_get_helper_max_cycles()) +
                           ",\"elementsHelperResolution\":" +
                           to_string(elements_debug_get_helper_resolution()) +
                           ",\"elementsHelperMisses\":" +
                           to_string(elements_debug_get_helper_misses()) +
                           ",\"elementsHelperModeOverride\":" +
                           to_string(elements_debug_get_mode_override()) +
                           "}";
                return true;
            }

            if (s == "/debug/setGrainCap") {
                if (!d.HasMember("value") || !d["value"].IsInt()) {
                    response = "{\"error\":\"missing value\"}";
                    return true;
                }
                const int old_cap = mbeads_debug_set_total_cap(d["value"].GetInt());
                response = "{\"ok\":1,\"old\":" + to_string(old_cap) +
                           ",\"cap\":" + to_string(mbeads_debug_get_total_cap()) + "}";
                return true;
            }

            if (s == "/debug/setHelperGrainCap") {
                if (!d.HasMember("value") || !d["value"].IsInt()) {
                    response = "{\"error\":\"missing value\"}";
                    return true;
                }
                const int old_cap = mbeads_debug_set_helper_cap(d["value"].GetInt());
                response = "{\"ok\":1,\"old\":" + to_string(old_cap) +
                           ",\"cap\":" + to_string(mbeads_debug_get_helper_cap()) + "}";
                return true;
            }

            if (s == "/debug/setElementsMode") {
                // Debug-only Elements modal-count override, e.g.
                // {"cmd":"/debug/setElementsMode","value":32}.
                // value <= 0 restores the production auto ladder; otherwise it
                // is clamped to [12, 56] by the setter. Persists across plugin
                // reloads so a measurement session survives setActivePlugin.
                if (!d.HasMember("value") || !d["value"].IsInt()) {
                    response = "{\"error\":\"missing value\"}";
                    return true;
                }
                const int old_mode = elements_debug_set_mode_override(d["value"].GetInt());
                response = "{\"ok\":1,\"old\":" + to_string(old_mode) +
                           ",\"mode\":" + to_string(elements_debug_get_mode_override()) + "}";
                return true;
            }

            if (s == "/debug/setLinkBaud") {
                // Debug-only link UART retune, e.g.
                // {"cmd":"/debug/setLinkBaud","value":460800} — baud sweep
                // for the handshake hunt (LinkEndpoint owns UART1).
                if (!d.HasMember("value") || !d["value"].IsInt()) {
                    response = "{\"error\":\"missing value\"}";
                    return true;
                }
                response = LinkEndpoint::SetBaud(d["value"].GetInt())
                               ? "{\"ok\":1}"
                               : "{\"error\":\"bad baud\"}";
                return true;
            }

            if (s == "/debug/setActivePlugin") {
                // Debug-only deterministic plugin load for bring-up.
                if (!d.HasMember("id") || !d["id"].IsString()) {
                    response = "{\"error\":\"missing id\"}";
                    return true;
                }
                int ch = 0;
                if (d.HasMember("ch") && d["ch"].IsInt()) ch = d["ch"].GetInt();
                if (ch < 0 || ch > 1) {
                    response = "{\"error\":\"bad channel\"}";
                    return true;
                }
                AUDIO::SoundProcessorManager::SetSoundProcessorChannel(
                    ch, d["id"].GetString());
                response = "{\"ok\":1}";
                return true;
            }

            if (s == "/debug/getPluginParams") {
                // Exact live parameter values for a channel, as the plugin
                // data model sees them. Used by bring-up scripts instead of
                // inferring values from the OLED.
                int ch = 0;
                if (d.HasMember("ch") && d["ch"].IsInt()) ch = d["ch"].GetInt();
                if (ch < 0 || ch > 1) {
                    response = "{\"error\":\"bad channel\"}";
                    return true;
                }
                const char *params =
                    AUDIO::SoundProcessorManager::GetCStrJSONActivePluginParams(ch);
                response = "{\"ch\":" + to_string(ch) +
                           ",\"params\":" + (params ? params : "{}") + "}";
                return true;
            }

            if (s == "/debug/setPluginParam") {
                // Debug-only equivalent of a UI parameter edit. The DSP reads
                // the same atomics as a real edit, so this is suitable for
                // reproducible CPU/audio sweeps without fighting the encoder.
                if (!d.HasMember("id") || !d["id"].IsString() ||
                    !d.HasMember("value") || !d["value"].IsInt()) {
                    response = "{\"error\":\"missing id/value\"}";
                    return true;
                }
                int ch = 0;
                if (d.HasMember("ch") && d["ch"].IsInt()) ch = d["ch"].GetInt();
                if (ch < 0 || ch > 1) {
                    response = "{\"error\":\"bad channel\"}";
                    return true;
                }
                AUDIO::SoundProcessorManager::SetChannelParamValue(
                    ch, d["id"].GetString(), "current", d["value"].GetInt());
                response = "{\"ok\":1}";
                return true;
            }

            if (s == "/debug/getCodecReg") {
                // Single-register read: fast enough for a live diagnostic even
                // when the I2C bus is busy (unlike the 53-register dump).
                if (!d.HasMember("reg") || !d["reg"].IsInt()) {
                    response = "{\"error\":\"missing reg\"}";
                    return true;
                }
                int reg = d["reg"].GetInt();
                if (reg < 0 || reg > 0xFF) {
                    response = "{\"error\":\"bad reg\"}";
                    return true;
                }
                uint8_t val = 0;
                bool ok = DRIVERS::Codec::DebugReadReg(static_cast<uint8_t>(reg), val);
                response = "{\"reg\":" + to_string(reg)
                           + ",\"value\":" + to_string(val)
                           + ",\"ok\":" + (ok ? "1" : "0") + "}";
                return true;
            }

            if (s == "/debug/getConfig") {
                // The stored configuration object, returned as an escaped JSON
                // *string* so the reply is one flat object; the host unescapes
                // and parses it. The buffer behind the pointer is shared, so
                // serialise it before anything else can overwrite it.
                const char *cfg = AUDIO::SoundProcessorManager::GetCStrJSONConfiguration();
                StringBuffer escaped;
                Writer<StringBuffer> writer(escaped);
                writer.String(cfg ? cfg : "");
                response = "{\"config\":" + string(escaped.GetString()) + "}";
                return true;
            }

            if (s == "/debug/setConfigValue") {
                // Overlay one configuration key and commit it, exactly like a
                // UI page does. Used by host tests to plant legacy stored values
                // (e.g. input_source "line2") and check the migration path.
                if (!d.HasMember("key") || !d["key"].IsString() ||
                    !d.HasMember("value") || !d["value"].IsString()) {
                    response = "{\"error\":\"missing key/value\"}";
                    return true;
                }
                const char *cfg = AUDIO::SoundProcessorManager::GetCStrJSONConfiguration();
                Document cur;
                cur.Parse(cfg ? cfg : "");
                if (!cur.IsObject()) {
                    response = "{\"error\":\"no config\"}";
                    return true;
                }
                const char *key = d["key"].GetString();
                Value v(d["value"].GetString(), cur.GetAllocator());
                if (cur.HasMember(key)) {
                    cur[key].Swap(v);
                } else {
                    Value k(key, cur.GetAllocator());
                    cur.AddMember(k, v, cur.GetAllocator());
                }
                StringBuffer buf;
                Writer<StringBuffer> writer(buf);
                cur.Accept(writer);
                AUDIO::SoundProcessorManager::SetConfigurationFromJSON(buf.GetString());
                response = "{\"ok\":1}";
                return true;
            }

            response = "{\"error\":\"unknown debug command\"}";
            return true;
        }

    }
}
