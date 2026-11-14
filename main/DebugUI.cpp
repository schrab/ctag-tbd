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
#include "esp_system.h" // esp_reset_reason — crash classification over the bench
#include "codec.hpp"
#include "SDAudio.hpp"
#include "perf_monitor.hpp"

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
#include <dirent.h>
#include <sys/stat.h>
#include <vector>
#include "ctagResources.hpp"
#include <cstring>
#include <cstdarg>
#include <unistd.h>

using namespace std;
using namespace rapidjson;

// Mbeads debug hooks: static stubs (Mbeads removed from build).
static inline int mbeads_debug_set_total_cap(int) { return -1; }
static inline int mbeads_debug_get_total_cap() { return -1; }
static inline int mbeads_debug_set_helper_cap(int) { return -1; }
static inline int mbeads_debug_get_helper_cap() { return -1; }
static inline int mbeads_debug_get_active_grains() { return -1; }
static inline int mbeads_debug_get_helper_active_grains() { return -1; }
static inline int mbeads_debug_get_helper_cycles() { return 0; }
static inline int mbeads_debug_get_helper_overload() { return 0; }
static inline int mbeads_debug_get_helper_stack_high_water() { return -1; }
static inline int mbeads_debug_get_core_cap() { return 0; }

// Elements second-core helper stats (distinct keys from Mbeads).
extern "C" int elements_debug_get_helper_cycles();
extern "C" int elements_debug_get_helper_max_cycles();
extern "C" int elements_debug_get_helper_overload();
extern "C" int elements_debug_get_helper_stack_high_water();
extern "C" int elements_debug_get_helper_resolution();
extern "C" int elements_debug_get_helper_misses();
extern "C" int elements_debug_get_mode_override();
extern "C" int elements_debug_set_mode_override(int modes);
extern "C" uint32_t elements_debug_get_panic_count();
extern "C" float elements_debug_get_max_resonator_level();

namespace {
    [[maybe_unused]] const char *TAG = "DebugUI";
}

namespace CTAG {
    namespace CTRL {
        bool DebugUI::handleCommand(const string &doc, string &response) {
            Document d;
            d.Parse(doc);
            if (d.HasParseError() || !d.IsObject() || !d.HasMember("cmd")) return false;

            string s(d["cmd"].GetString());
            if (s.rfind("/debug/", 0) != 0) return false; // not ours

            // Every reply echoes the command: the panel's frame parser
            // drops cmd-less frames.
            const string cmdEcho = "{\"cmd\":\"" + s + "\"";

            if (s == "/debug/getAudioStats") {
                // Audio-task stack high-water mark plus the most recent
                // per-block CPU-overload flag. stackHighWater is the
                // minimum-ever-free stack in bytes (0 before the task exists).
                // rstReason: the CURRENT boot's reset cause (RTC-stored,
                // survives the reboot) — the silent-crash classifier: the
                // ROM banner is lost to USB re-enumeration latency, but the
                // reason is readable any time afterwards. esp_reset_reason_t
                // order (esp_system.h): 0=unknown 1=poweron 2=ext 3=sw
                // 4=panic 5=int_wdt 6=task_wdt 7=wdt 8=deepsleep 9=brownout
                // 10=sdio 11=usb 12=jtag 13=efuse 14=pwr_glitch 15=cpu_lockup.
                // NOTE: esptool RTS resets on the S3 USJ read as 11 (usb) —
                // that is the FLASH-RESET BASELINE, not a crash.
                static const char *kRstNames[] = {
                    "unknown", "poweron", "ext", "sw", "panic", "int_wdt",
                    "task_wdt", "wdt", "deepsleep", "brownout", "sdio", "usb",
                    "jtag", "efuse", "pwr_glitch", "cpu_lockup"};
                int rst = (int)esp_reset_reason();
                const char *rstName =
                    (rst >= 0 && rst < (int)(sizeof(kRstNames) / sizeof(*kRstNames)))
                        ? kRstNames[rst] : "?";
                response = cmdEcho + ",\"rstReason\":" + to_string(rst) +
                           ",\"rstName\":\"" + rstName + "\"," +
                           "\"stackHighWater\":" +
                           to_string(AUDIO::SoundProcessorManager::GetAudioStackHighWaterBytes()) +
                           ",\"cpuOverload\":" +
                           to_string(AUDIO::SoundProcessorManager::GetCPUOverload() ? 1 : 0) +
                           ",\"audioCycles\":" +
                           to_string(AUDIO::SoundProcessorManager::GetLastAudioTaskCycles()) +
                           // Peak metering (0..1000 per block). Only live
                           // while /link/metering armed the peak pass —
                           // bench diagnostics read the same atomics the
                           // MIX page draws.
                           ",\"vuInL\":" +
                           to_string(AUDIO::SoundProcessorManager::GetVUPeak(0)) +
                           ",\"vuInR\":" +
                           to_string(AUDIO::SoundProcessorManager::GetVUPeak(1)) +
                           ",\"vuOutL\":" +
                           to_string(AUDIO::SoundProcessorManager::GetVUPeak(2)) +
                           ",\"vuOutR\":" +
                           to_string(AUDIO::SoundProcessorManager::GetVUPeak(3)) +
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
                           ",\"elementsPanicCount\":" +
                           to_string(elements_debug_get_panic_count()) +
                           ",\"elementsMaxResonatorLevel\":" +
                           to_string(elements_debug_get_max_resonator_level()) +
#if defined(CONFIG_TBD_PLATFORM_BBA)
                           ",\"diagStallRd\":" +
                           to_string(AUDIO::SoundProcessorManager::GetDiagStallRd()) +
                           ",\"diagStallWr\":" +
                           to_string(AUDIO::SoundProcessorManager::GetDiagStallWr()) +
                           ",\"diagLoopOver\":" +
                           to_string(AUDIO::SoundProcessorManager::GetDiagLoopOver()) +
                           ",\"i2sErrR\":" +
                           to_string(DRIVERS::Codec::DebugI2sReadErrors()) +
                           ",\"i2sErrW\":" +
                           to_string(DRIVERS::Codec::DebugI2sWriteErrors()) +
#endif
                           "}";
                return true;
            }

            if (s == "/debug/getLoopHist") {
                // Audio-loop period history, oldest first, in 0.1 ms units
                // (normal pacing ~232 = 23.2 ms). Spikes = the task started
                // late → the I2S DMA starved → codec stutter. Optional
                // {"len":N} truncates the reply (default 256).
                int len = AUDIO::SoundProcessorManager::kLoopHistLen;
                if (d.HasMember("len") && d["len"].IsInt()) {
                    int l = d["len"].GetInt();
                    if (l >= 16 && l <= AUDIO::SoundProcessorManager::kLoopHistLen)
                        len = l;
                }
                uint32_t seq = AUDIO::SoundProcessorManager::GetLoopHistSeq();
                const uint16_t *hist = AUDIO::SoundProcessorManager::loopHist;
                string out = "[";
                for (int k = len; k > 0; k--) {
                    int i = static_cast<int>(
                        (seq - k) & (AUDIO::SoundProcessorManager::kLoopHistLen - 1));
                    out += to_string(hist[i]);
                    if (k > 1) out += ",";
                }
                response = cmdEcho + ",\"seq\":" + to_string(seq) +
                           ",\"hist\":" + out + "]}";
                return true;
            }

            if (s == "/debug/clearFreeze") {
                // Release a frozen click-capture ring and resume rolling.
                AUDIO::SoundProcessorManager::ClearRingFrozen();
                response = cmdEcho + ",\"ok\":1}";
                return true;
            }

            if (s == "/debug/getAudioCapture") {
                // The rolling output ring: the last kCaptureBlocks finished
                // stereo blocks, oldest first, as int16 samples (float x
                // scale, clamped). Optional {"blocks":1..8,"scale":N} to
                // trade resolution against reply size while hunting the
                // link's reply-size ceiling. Written continuously by the
                // audio task — the read may straddle a block overwrite.
                // Reply-path trace: this command's reply never reached the
                // C3 relay (getAudioStats/getPluginParams do) — entry/exit
                // logs on the DSP console bisect where it dies.
                ESP_LOGI("DBG", "capture: entry blocks=%d", [&] {
                    return d.HasMember("blocks") && d["blocks"].IsInt()
                               ? d["blocks"].GetInt()
                               : AUDIO::SoundProcessorManager::kCaptureBlocks;
                }());
                int blocks = AUDIO::SoundProcessorManager::kCaptureBlocks;
                float scale = 8192.f;
                bool withVuHist = true;
                if (d.HasMember("blocks") && d["blocks"].IsInt()) {
                    int b = d["blocks"].GetInt();
                    if (b >= 1 && b <= AUDIO::SoundProcessorManager::kCaptureBlocks)
                        blocks = b;
                }
                if (d.HasMember("scale") && d["scale"].IsInt()) {
                    int sv = d["scale"].GetInt();
                    if (sv >= 64 && sv <= 32768) scale = static_cast<float>(sv);
                }
                if (d.HasMember("vuHist") && d["vuHist"].IsInt()) {
                    // {"vuHist":0} omits the per-block peak history — the
                    // default full dump exceeds the C3 bench relay ceiling,
                    // which blocked output-audio inspection through the panel.
                    withVuHist = d["vuHist"].GetInt() != 0;
                }
                const int kFrames = 32; // one stereo block
                uint32_t endSeq =
                    AUDIO::SoundProcessorManager::GetAudioRingSeq();
                string out = "[";
                for (int blk = blocks - 1; blk >= 0; blk--) {
                    uint32_t slot =
                        (endSeq - 1 - blk) &
                        (AUDIO::SoundProcessorManager::kCaptureBlocks - 1);
                    const float *f =
                        AUDIO::SoundProcessorManager::audioRing[slot];
                    for (int i = 0; i < kFrames * 2; i++) {
                        int q = static_cast<int>(f[i] * scale);
                        if (q > 32000) q = 32000;
                        if (q < -32000) q = -32000;
                        out += to_string(q);
                        if (i < kFrames * 2 - 1) out += ",";
                    }
                    if (blk > 0) out += ",";
                }
                response = cmdEcho + ",\"seq\":" + to_string(endSeq) +
                           ",\"frozen\":" +
                           (AUDIO::SoundProcessorManager::GetRingFrozen() ? "1" : "0") +
                           ",\"resid\":" +
                           to_string(AUDIO::SoundProcessorManager::GetBoundaryResidual()) +
                           ",\"blocks\":" + to_string(blocks) +
                           ",\"muteBlocks\":" +
                           to_string(AUDIO::SoundProcessorManager::GetMuteBlocks()) +
                           ",\"data\":" + out + "]" +
                           (withVuHist ? ",\"vuHist\":[" : "}");
                if (withVuHist) {
                    // Full-coverage per-block output peaks, oldest first.
                    // The ring rotates once filled: start at the oldest
                    // live slot (seq & mask) and wrap.
                    uint32_t histSeq =
                        AUDIO::SoundProcessorManager::GetVUHistSeq();
                    const int kVUHistLen =
                        AUDIO::SoundProcessorManager::kVUHistLen;
                    const uint16_t *hist =
                        AUDIO::SoundProcessorManager::vuHist;
                    int start = static_cast<int>(histSeq & (kVUHistLen - 1));
                    if (histSeq < static_cast<uint32_t>(kVUHistLen)) start = 0;
                    for (int k = 0; k < kVUHistLen; k++) {
                        int i = (start + k) & (kVUHistLen - 1);
                        response += to_string(hist[i]);
                        if (k < kVUHistLen - 1) response += ",";
                    }
                    response += "]";
                }
                response += "}";
                ESP_LOGI("DBG", "capture: reply built, %u B", (unsigned)response.size());
                return true;
            }

            if (s == "/debug/setGrainCap") {
                if (!d.HasMember("value") || !d["value"].IsInt()) {
                    response = cmdEcho + ",\"error\":\"missing value\"}";
                    return true;
                }
                const int old_cap = mbeads_debug_set_total_cap(d["value"].GetInt());
                response = cmdEcho + ",\"ok\":1,\"old\":" + to_string(old_cap) +
                           ",\"cap\":" + to_string(mbeads_debug_get_total_cap()) + "}";
                return true;
            }

            if (s == "/debug/setHelperGrainCap") {
                if (!d.HasMember("value") || !d["value"].IsInt()) {
                    response = cmdEcho + ",\"error\":\"missing value\"}";
                    return true;
                }
                const int old_cap = mbeads_debug_set_helper_cap(d["value"].GetInt());
                response = cmdEcho + ",\"ok\":1,\"old\":" + to_string(old_cap) +
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
                    response = cmdEcho + ",\"error\":\"missing value\"}";
                    return true;
                }
                const int old_mode = elements_debug_set_mode_override(d["value"].GetInt());
                response = cmdEcho + ",\"ok\":1,\"old\":" + to_string(old_mode) +
                           ",\"mode\":" + to_string(elements_debug_get_mode_override()) + "}";
                return true;
            }

            if (s == "/debug/setLinkBaud") {
                // Debug-only link UART retune, e.g.
                // {"cmd":"/debug/setLinkBaud","value":460800} — baud sweep
                // for the handshake hunt (LinkEndpoint owns UART1).
                if (!d.HasMember("value") || !d["value"].IsInt()) {
                    response = cmdEcho + ",\"error\":\"missing value\"}";
                    return true;
                }
                response = LinkEndpoint::SetBaud(d["value"].GetInt())
                               ? cmdEcho + ",\"ok\":1}"
                               : cmdEcho + ",\"error\":\"bad baud\"}";
                return true;
            }

            if (s == "/debug/enablePerf") {
                // Toggle the 5 s PERF logging task on demand. The always-on
                // logger caused a periodic crackle; measurement now defaults
                // to off. PERF sections still accumulate either way.
                if (!d.HasMember("value") || !d["value"].IsInt()) {
                    response = cmdEcho + ",\"error\":\"missing value\"}";
                    return true;
                }
                CTAG::INSTRUMENTATION::PerfMonitor::EnableLogging(
                    d["value"].GetInt() != 0, 5000, 1, 5);
                response = cmdEcho + ",\"ok\":1,\"logging\":" +
                           std::to_string(d["value"].GetInt() != 0 ? 1 : 0) + "}";
                return true;
            }

            if (s == "/debug/setActivePlugin") {
                // Debug-only deterministic plugin load for bring-up.
                if (!d.HasMember("id") || !d["id"].IsString()) {
                    response = cmdEcho + ",\"error\":\"missing id\"}";
                    return true;
                }
                int ch = 0;
                if (d.HasMember("ch") && d["ch"].IsInt()) ch = d["ch"].GetInt();
                if (ch < 0 || ch > 1) {
                    response = cmdEcho + ",\"error\":\"bad channel\"}";
                    return true;
                }
                AUDIO::SoundProcessorManager::SetSoundProcessorChannel(
                    ch, d["id"].GetString());
                response = cmdEcho + ",\"ok\":1}";
                return true;
            }

            if (s == "/debug/getPluginParams") {
                // Exact live parameter values for a channel, as the plugin
                // data model sees them. Used by bring-up scripts instead of
                // inferring values from the OLED. An optional "id" filters
                // the reply to one parameter — the full JSON of a big plugin
                // (~20 KB for Subbotnik's 103 params) exceeds the panel-link
                // bench relay and never arrives.
                int ch = 0;
                if (d.HasMember("ch") && d["ch"].IsInt()) ch = d["ch"].GetInt();
                if (ch < 0 || ch > 1) {
                    response = cmdEcho + ",\"error\":\"bad channel\"}";
                    return true;
                }
                const char *params =
                    AUDIO::SoundProcessorManager::GetCStrJSONActivePluginParams(ch);
                if (d.HasMember("id") && d["id"].IsString()) {
                    // Single-param probe: {"id":"pitch_A","current":N,...}
                    // Sscanf-style scan of the model JSON; no DOM needed
                    // (the model string can be large and this runs on the
                    // link task).
                    string id = d["id"].GetString();
                    string needle = "\"id\":\"" + id + "\"";
                    const char *p = params ? strstr(params, needle.c_str()) : nullptr;
                    if (p == nullptr) {
                        response = cmdEcho + ",\"ch\":" + to_string(ch) +
                                   ",\"error\":\"no such id\"}";
                        return true;
                    }
                    const char *objEnd = strchr(p, '}');
                    const char *objStart = p;
                    while (objStart > params && *objStart != '{') objStart--;
                    response = cmdEcho + ",\"ch\":" + to_string(ch) + ",\"param\":";
                    response.append(objStart,
                                    objEnd ? (objEnd - objStart + 1) : strlen(objStart));
                    response += "}";
                    return true;
                }
                response = cmdEcho + ",\"ch\":" + to_string(ch) +
                           ",\"params\":" + (params ? params : "{}") + "}";
                return true;
            }

            if (s == "/debug/setPluginParam") {
                // Debug-only equivalent of a UI parameter edit. The DSP reads
                // the same atomics as a real edit, so this is suitable for
                // reproducible CPU/audio sweeps without fighting the encoder.
                // Optional "key": "current" (default) | "cv" | "trig" — the
                // same set SetChannelParamValue routes, so modulation
                // mappings are bench-writable too (no menu diving).
                if (!d.HasMember("id") || !d["id"].IsString() ||
                    !d.HasMember("value") || !d["value"].IsInt()) {
                    response = cmdEcho + ",\"error\":\"missing id/value\"}";
                    return true;
                }
                int ch = 0;
                if (d.HasMember("ch") && d["ch"].IsInt()) ch = d["ch"].GetInt();
                if (ch < 0 || ch > 1) {
                    response = cmdEcho + ",\"error\":\"bad channel\"}";
                    return true;
                }
                string key = "current";
                if (d.HasMember("key") && d["key"].IsString()) {
                    key = d["key"].GetString();
                }
                if (key != "current" && key != "cv" && key != "trig") {
                    response = cmdEcho + ",\"error\":\"bad key\"}";
                    return true;
                }
                AUDIO::SoundProcessorManager::SetChannelParamValue(
                    ch, d["id"].GetString(), key, d["value"].GetInt());
                response = cmdEcho + ",\"ok\":1}";
                return true;
            }

            if (s == "/debug/getPresets") {
                // Patch list for a channel (names + numbers — the same JSON
                // the panel PRESETS menu parses). Small reply, relay-safe.
                int ch = 0;
                if (d.HasMember("ch") && d["ch"].IsInt()) ch = d["ch"].GetInt();
                if (ch < 0 || ch > 1) {
                    response = cmdEcho + ",\"error\":\"bad channel\"}";
                    return true;
                }
                const char *list = AUDIO::SoundProcessorManager::GetCStrJSONGetPresets(ch);
                response = cmdEcho + ",\"ch\":" + to_string(ch) +
                           ",\"presets\":" + (list ? list : "{}");
                return true;
            }

            if (s == "/debug/getPresetData") {
                // CURRENT patch's full params for a channel (the model
                // serialises the active patch). For a non-active patch,
                // /link/presetLoad it first, then read. Reply ~1.5 KB per
                // patch — the bench relay ceiling is ~2 KB, keep patches lean.
                int ch = 0;
                if (d.HasMember("ch") && d["ch"].IsInt()) ch = d["ch"].GetInt();
                if (ch < 0 || ch > 1) {
                    response = cmdEcho + ",\"error\":\"bad channel\"}";
                    return true;
                }
                const char *data = AUDIO::SoundProcessorManager::GetCStrJSONAllPresetData(ch);
                response = cmdEcho + ",\"ch\":" + to_string(ch) +
                           ",\"presetData\":" + (data ? data : "{}");
                return true;
            }

            if (s == "/debug/listFiles") {
                // POSIX dir listing (names + sizes) for the preset backup —
                // reads the SPIFFS store DIRECTLY, no plugin loads needed.
                string dir = CTAG::RESOURCES::spiffsRoot + "/data/sp";
                if (d.HasMember("dir") && d["dir"].IsString())
                    dir = d["dir"].GetString();
                DIR *dp = opendir(dir.c_str());
                if (dp == nullptr) {
                    response = cmdEcho + ",\"error\":\"opendir failed\"}";
                    return true;
                }
                response = cmdEcho + ",\"files\":[";
                struct dirent *ent;
                bool first = true;
                while ((ent = readdir(dp)) != nullptr) {
                    // names only: the per-entry stat() stalls the link task
                    // on LittleFS (measured — the reply never returned with
                    // it); sizes are not worth the wedge.
                    if (!first) response += ",";
                    response += "\"" + string(ent->d_name) + "\"";
                    first = false;
                }
                closedir(dp);
                response += "]}";
                return true;
            }

            if (s == "/debug/readFileChunk") {
                // Chunked file read (hex-encoded) — relay-safe slices for
                // pulling SPIFFS preset files byte-exact.
                if (!d.HasMember("path") || !d["path"].IsString() ||
                    !d.HasMember("offset") || !d["offset"].IsInt() ||
                    !d.HasMember("len") || !d["len"].IsInt()) {
                    response = cmdEcho + ",\"error\":\"missing path/offset/len\"}";
                    return true;
                }
                string path = d["path"].GetString();
                if (path.find(CTAG::RESOURCES::spiffsRoot) != 0) {
                    response = cmdEcho + ",\"error\":\"path outside spiffs root\"}";
                    return true;
                }
                long off = d["offset"].GetInt();
                int len = d["len"].GetInt();
                if (len <= 0 || len > 512) {
                    response = cmdEcho + ",\"error\":\"bad len (1..512)\"}";
                    return true;
                }
                FILE *fp = fopen(path.c_str(), "rb");
                if (fp == nullptr) {
                    response = cmdEcho + ",\"error\":\"open failed\"}";
                    return true;
                }
                fseek(fp, 0, SEEK_END);
                long fsize = ftell(fp);
                if (off >= fsize) {
                    fclose(fp);
                    response = cmdEcho + ",\"size\":" + to_string(fsize) +
                               ",\"hex\":\"\"}";
                    return true;
                }
                if (off + len > fsize) len = static_cast<int>(fsize - off);
                fseek(fp, off, SEEK_SET);
                vector<char> buf(len);
                size_t got = fread(buf.data(), 1, len, fp);
                fclose(fp);
                static const char *hexd = "0123456789abcdef";
                string hex;
                hex.reserve(got * 2);
                for (size_t i = 0; i < got; i++) {
                    hex += hexd[(buf[i] >> 4) & 0xF];
                    hex += hexd[buf[i] & 0xF];
                }
                response = cmdEcho + ",\"size\":" + to_string(fsize) +
                           ",\"offset\":" + to_string(off) +
                           ",\"got\":" + to_string(got) +
                           ",\"hex\":\"" + hex + "\"}";
                return true;
            }

            if (s == "/debug/writeFileChunk") {
                // Chunked file write (hex in) — the restore half of the
                // SPIFFS preset backup. Path guarded to the spiffs root.
                if (!d.HasMember("path") || !d["path"].IsString() ||
                    !d.HasMember("offset") || !d["offset"].IsInt() ||
                    !d.HasMember("hex") || !d["hex"].IsString()) {
                    response = cmdEcho + ",\"error\":\"missing path/offset/hex\"}";
                    return true;
                }
                string path = d["path"].GetString();
                if (path.find(CTAG::RESOURCES::spiffsRoot) != 0) {
                    response = cmdEcho + ",\"error\":\"path outside spiffs root\"}";
                    return true;
                }
                long off = d["offset"].GetInt();
                const string hex = d["hex"].GetString();
                if (hex.size() % 2 != 0 || hex.size() / 2 > 512) {
                    response = cmdEcho + ",\"error\":\"bad hex length\"}";
                    return true;
                }
                vector<char> buf;
                buf.reserve(hex.size() / 2);
                for (size_t i = 0; i + 1 < hex.size(); i += 2) {
                    auto nib = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return -1;
                    };
                    int hi = nib(hex[i]), lo = nib(hex[i + 1]);
                    if (hi < 0 || lo < 0) {
                        response = cmdEcho + ",\"error\":\"bad hex digit\"}";
                        return true;
                    }
                    buf.push_back(static_cast<char>((hi << 4) | lo));
                }
                FILE *fp = fopen(path.c_str(), off == 0 ? "wb" : "rb+");
                if (fp == nullptr) {
                    response = cmdEcho + ",\"error\":\"open failed\"}";
                    return true;
                }
                fseek(fp, off, SEEK_SET);
                size_t put = fwrite(buf.data(), 1, buf.size(), fp);
                fclose(fp);
                response = cmdEcho + ",\"written\":" + to_string(put) + ",\"ok\":1}";
                return true;
            }

            if (s == "/debug/presetLoad") {
                // Bench preset switch: load a stored patch so its params
                // become readable via /debug/getPluginParams (the preset
                // dump flow). Persists activePatch (storeJSON) like the UI.
                if (!d.HasMember("ch") || !d["ch"].IsInt() ||
                    !d.HasMember("num") || !d["num"].IsInt()) {
                    response = cmdEcho + ",\"error\":\"missing ch/num\"}";
                    return true;
                }
                int ch = d["ch"].GetInt();
                int num = d["num"].GetInt();
                if (ch < 0 || ch > 1 || num < 0) {
                    response = cmdEcho + ",\"error\":\"bad ch/num\"}";
                    return true;
                }
                AUDIO::SoundProcessorManager::ChannelLoadPreset(ch, num);
                response = cmdEcho + ",\"ok\":1}";
                return true;
            }

            if (s == "/debug/presetSave") {
                // Bench preset store: append/overwrite a patch with the
                // CURRENT param state under the given name/number (the
                // restore half of the preset dump flow).
                if (!d.HasMember("ch") || !d["ch"].IsInt() ||
                    !d.HasMember("num") || !d["num"].IsInt() ||
                    !d.HasMember("name") || !d["name"].IsString()) {
                    response = cmdEcho + ",\"error\":\"missing ch/num/name\"}";
                    return true;
                }
                int ch = d["ch"].GetInt();
                int num = d["num"].GetInt();
                if (ch < 0 || ch > 1 || num < 0) {
                    response = cmdEcho + ",\"error\":\"bad ch/num\"}";
                    return true;
                }
                AUDIO::SoundProcessorManager::ChannelSavePreset(
                    ch, d["name"].GetString(), num);
                response = cmdEcho + ",\"ok\":1}";
                return true;
            }

            if (s == "/debug/getCodecReg") {
                // Single-register read: fast enough for a live diagnostic even
                // when the I2C bus is busy (unlike the 53-register dump).
                if (!d.HasMember("reg") || !d["reg"].IsInt()) {
                    response = cmdEcho + ",\"error\":\"missing reg\"}";
                    return true;
                }
                int reg = d["reg"].GetInt();
                if (reg < 0 || reg > 0xFF) {
                    response = cmdEcho + ",\"error\":\"bad reg\"}";
                    return true;
                }
                uint8_t val = 0;
                bool ok = DRIVERS::Codec::DebugReadReg(static_cast<uint8_t>(reg), val);
                response = cmdEcho + ",\"reg\":" + to_string(reg)
                           + ",\"value\":" + to_string(val)
                           + ",\"ok\":" + (ok ? "1" : "0") + "}";
                return true;
            }

            if (s == "/debug/setCodecReg") {
                // Bench register write for input-routing sweeps (mic hunt):
                // writes through the shadowed register layer and echoes the
                // full readback. WM8978 regs are 9-bit (val <= 0x1FF);
                // ES8388 rejects > 0xFF.
                if (!d.HasMember("reg") || !d["reg"].IsInt() ||
                    !d.HasMember("val") || !d["val"].IsInt()) {
                    response = cmdEcho + ",\"error\":\"missing reg/val\"}";
                    return true;
                }
                int reg = d["reg"].GetInt();
                int val = d["val"].GetInt();
                if (reg < 0 || reg > 0xFF || val < 0 || val > 0x1FF) {
                    response = cmdEcho + ",\"error\":\"bad reg/val\"}";
                    return true;
                }
                uint16_t rb = 0;
                bool ok = DRIVERS::Codec::DebugWriteReg(
                    static_cast<uint8_t>(reg), static_cast<uint16_t>(val), rb);
                response = cmdEcho + ",\"reg\":" + to_string(reg)
                           + ",\"wrote\":" + to_string(val)
                           + ",\"readback\":" + to_string(rb)
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
                response = cmdEcho + ",\"config\":" + string(escaped.GetString()) + "}";
                return true;
            }

            if (s == "/debug/setConfigValue") {
                // Overlay one configuration key and commit it, exactly like a
                // UI page does. Used by host tests to plant legacy stored values
                // (e.g. input_source "line2") and check the migration path.
                if (!d.HasMember("key") || !d["key"].IsString() ||
                    !d.HasMember("value") || !d["value"].IsString()) {
                    response = cmdEcho + ",\"error\":\"missing key/value\"}";
                    return true;
                }
                const char *cfg = AUDIO::SoundProcessorManager::GetCStrJSONConfiguration();
                Document cur;
                cur.Parse(cfg ? cfg : "");
                if (!cur.IsObject()) {
                    response = cmdEcho + ",\"error\":\"no config\"}";
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
                response = cmdEcho + ",\"ok\":1}";
                return true;
            }

            if (s == "/debug/sdInfo") {
                // Header-only WAV probe for the SD CARD file-info view (and
                // the bench). Path must stay inside /sd. Refused while the
                // transport is active: this runs on the link task, and a
                // fopen concurrent with the record worker's fwrite was the
                // suspected trigger of an intermittent DSP hang.
                if (AUDIO::SDAudio::IsPlaying() || AUDIO::SDAudio::IsRecording()) {
                    response = cmdEcho + ",\"error\":\"transport busy\"}";
                    return true;
                }
                if (!d.HasMember("path") || !d["path"].IsString()) {
                    response = cmdEcho + ",\"error\":\"missing path\"}";
                    return true;
                }
                const char *path = d["path"].GetString();
                if (strncmp(path, "/sd/", 4) != 0 || strstr(path, "..") != nullptr) {
                    response = cmdEcho + ",\"error\":\"bad path\"}";
                    return true;
                }
                AUDIO::SDAudio::WavInfo wi;
                if (!AUDIO::SDAudio::ProbeWav(path, &wi)) {
                    response = cmdEcho + ",\"error\":\"not a 16-bit PCM WAV\"}";
                    return true;
                }
                response = cmdEcho +
                           ",\"size\":" + to_string(wi.sizeBytes) +
                           ",\"sr\":" + to_string(wi.sampleRate) +
                           ",\"ch\":" + to_string(wi.channels) +
                           ",\"bits\":" + to_string(wi.bits) +
                           ",\"frames\":" + to_string(wi.frames) + "}";
                return true;
            }

            if (s == "/debug/sdDelete") {
                // Remove one file on the card. Refused while the transport is
                // active (same link-task/SD-concurrency reason as sdInfo) and
                // while the file is the active playback/recording source.
                if (AUDIO::SDAudio::IsPlaying() || AUDIO::SDAudio::IsRecording()) {
                    response = cmdEcho + ",\"error\":\"transport busy\"}";
                    return true;
                }
                if (!d.HasMember("path") || !d["path"].IsString()) {
                    response = cmdEcho + ",\"error\":\"missing path\"}";
                    return true;
                }
                const char *path = d["path"].GetString();
                if (strncmp(path, "/sd/", 4) != 0 || strstr(path, "..") != nullptr) {
                    response = cmdEcho + ",\"error\":\"bad path\"}";
                    return true;
                }
                if ((AUDIO::SDAudio::IsPlaying() || AUDIO::SDAudio::IsRecording()) &&
                    strcmp(path, AUDIO::SDAudio::GetFileName()) == 0) {
                    response = cmdEcho + ",\"error\":\"file in use\"}";
                    return true;
                }
                if (unlink(path) != 0) {
                    response = cmdEcho + ",\"error\":\"unlink failed\"}";
                    return true;
                }
                response = cmdEcho + ",\"ok\":1}";
                return true;
            }

            response = cmdEcho + ",\"error\":\"unknown debug command\"}";
            return true;
        }

    }
}
