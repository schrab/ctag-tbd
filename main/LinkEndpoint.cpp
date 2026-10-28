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

#include "LinkEndpoint.hpp"
#include "DebugUI.hpp"
#include "SPManager.hpp"
#include "MarblesEngine.hpp"
#include "SDAudio.hpp"
#include "Favorites.hpp"
#include "fs.hpp"
#include "codec.hpp"
#include "version.hpp"
#include "Midi.hpp"
#include "ctag/link/protocol.hpp"
#include "driver/uart.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <cstring>
#include <string>

using namespace std;
using namespace rapidjson;
using namespace CTAG::LINK;
using namespace CTAG::AUDIO;
using namespace CTAG::DRIVERS;
using namespace CTAG::FAV;

static const char *TAG = "LinkEp";

namespace CTAG {
    namespace CTRL {

        namespace {
            constexpr uart_port_t kUart = UART_NUM_1;
            constexpr int kLinkBaud = 921600;
            // RX is GPIO23, NOT the SDD's GPIO19: the A1S board's GPIO19
            // input trace (ex-MIDI-IN path) corrupts everything above
            // ~250 kBd — proven by a TX→RX jumper loopback (clean at
            // 115200/250k, garbage at ≥460800 on UART1 AND UART2, while
            // GPIO18's own TX signal decodes cleanly on the panel). On
            // GPIO23 the same loopback is clean to 1 Mbps. The encoder
            // PCNT uses GPIO23 only as its direction input (it counts
            // edges on GPIO5), so the link coexists with it.
            constexpr size_t kRxRing = 16384;
            constexpr size_t kTxRing = 8192;
            // Panel commands (bench set + /link set); the framebuffer reply
            // is ~2.1 KB so this must stay above it. Matches DebugUI MAX_DOC.
            constexpr size_t kMaxDoc = 4096;
            // Cap when hashing model blobs (params JSON is the big one).
            constexpr size_t kMaxHashBlob = 128 * 1024;
            // ms without any panel byte before the link is torn down.
            constexpr uint32_t kOfflineAfterMs = 3000;
            constexpr uint32_t kStreamPeriodMs = 100;      // 10 Hz (SDD §4.2)
            constexpr uint32_t kHeartbeatPeriodMs = 1000;

            TaskHandle_t hLinkTask = nullptr;
            volatile bool s_online = false;
            uint32_t lastPanelRxMs = 0;
            uint32_t lastStreamMs = 0;
            uint32_t lastHeartbeatMs = 0;
            uint32_t heartbeatSeq = 0;
            volatile bool marblesPushPending = false;

            // Scratch for building push payloads. The model's JSON buffers
            // can be regenerated concurrently, so every push copies its
            // source here first. Params JSON is the big one (tens of KB);
            // PSRAM holds it without touching internal DRAM.
            char *txScratch = nullptr;
            constexpr size_t kTxScratch = 96 * 1024;

            uint32_t nowMs() {
                return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            }

            const char *GetCStrSafe(const char *p) { return p ? p : ""; }

            void sendPayload(const char *p, size_t len) {
                uart_write_bytes(kUart, "\x02", 1);
                uart_write_bytes(kUart, p, len);
                uart_write_bytes(kUart, "\x03", 1);
            }

            void sendStr(const string &s) { sendPayload(s.data(), s.size()); }

            // Append "key":"<json-escaped raw>" with the same writer the
            // bench channel uses for /debug/getConfig.
            void appendEscapedField(string &out, const char *key, const char *raw) {
                out += "\"";
                out += key;
                out += "\":";
                StringBuffer sb;
                Writer<StringBuffer> w(sb);
                w.String(raw ? raw : "");
                out += sb.GetString();
            }

            // Copy a source JSON blob into txScratch (model buffers may be
            // regenerated concurrently) and return it NUL-terminated.
            const char *scratchCopy(const char *src) {
                if (txScratch == nullptr) return "";
                if (src == nullptr) { txScratch[0] = '\0'; return txScratch; }
                size_t n = strnlen(src, kTxScratch - 1);
                memcpy(txScratch, src, n);
                txScratch[n] = '\0';
                return txScratch;
            }

            // ---------------------------------------------------------------------------
            // Pushes (DSP -> panel); shapes the C3 DspProxy cache parses.
            // ---------------------------------------------------------------------------

            void pushMarbles();

            // Record current hashes without pushing (defined near the diff
            // poll below); called after the boot burst.
            void seedLastPushed();

            void pushParams(int ch) {
                string s = "{\"cmd\":\"";
                s += kMsgPushParams;
                s += "\",\"ch\":" + to_string(ch) + ",";
                appendEscapedField(s, "params", scratchCopy(SoundProcessorManager::GetCStrJSONActivePluginParams(ch)));
                s += "}";
                sendStr(s);
            }

            void pushPresets(int ch) {
                string s = "{\"cmd\":\"";
                s += kMsgPushPresets;
                s += "\",\"ch\":" + to_string(ch) + ",";
                appendEscapedField(s, "presets", scratchCopy(SoundProcessorManager::GetCStrJSONGetPresets(ch)));
                s += "}";
                sendStr(s);
            }

            void pushConfig() {
                string s = "{\"cmd\":\"";
                s += kMsgPushConfig;
                s += "\",";
                appendEscapedField(s, "config", scratchCopy(SoundProcessorManager::GetCStrJSONConfiguration()));
                s += "}";
                sendStr(s);
            }

            void pushPluginList() {
                string s = "{\"cmd\":\"";
                s += kMsgPushPluginList;
                s += "\",\"list\":";
                // Embedded raw: the model emits a valid JSON array.
                s += scratchCopy(SoundProcessorManager::GetCStrJSONSoundProcessors());
                s += "}";
                sendStr(s);
            }

            void pushActivePlugin() {
                string id0 = SoundProcessorManager::GetStringID(0);
                string id1 = SoundProcessorManager::GetStringID(1);
                string s = "{\"cmd\":\"";
                s += kMsgPushActivePlugin;
                s += "\",";
                appendEscapedField(s, "id0", id0.c_str());
                s += ",";
                appendEscapedField(s, "id1", id1.c_str());
                s += ",\"st0\":" + to_string(SoundProcessorManager::IsPluginStereo(id0) ? 1 : 0);
                s += ",\"st1\":" + to_string(SoundProcessorManager::IsPluginStereo(id1) ? 1 : 0);
                s += "}";
                sendStr(s);
            }

            void pushFavorites() {
                // GetAllFavorites() builds a fresh string; copy to scratch
                // before escaping.
                string favs = Favorites::GetAllFavorites();
                string s = "{\"cmd\":\"";
                s += kMsgPushFavorites;
                s += "\",";
                appendEscapedField(s, "favs", scratchCopy(favs.c_str()));
                s += "}";
                sendStr(s);
            }

            void pushIoCaps() {
                string s = "{\"cmd\":\"";
                s += kMsgPushIoCaps;
                s += "\",\"sd\":" + to_string(FileSystem::IsSDMounted() ? 1 : 0);
                s += "}";
                sendStr(s);
            }

            // Full Marbles state push; keys are shared with the
            // /link/marblesSet key table in handleLinkCommand().
            void pushMarbles() {
                string s = "{\"cmd\":\"";
                s += kMsgPushMarbles;
                s += "\",\"st\":{";
                s += "\"tm\":" + to_string(MarblesEngine::GetTModel());
                s += ",\"tr\":" + to_string(MarblesEngine::GetTRange());
                s += ",\"txy\":" + to_string(MarblesEngine::GetXYClockSource());
                s += ",\"te\":" + to_string(MarblesEngine::GetTExternal() ? 1 : 0);
                s += ",\"trate\":" + to_string(MarblesEngine::GetTRate());
                s += ",\"tb\":" + to_string(MarblesEngine::GetTBias());
                s += ",\"tj\":" + to_string(MarblesEngine::GetTJitter());
                s += ",\"tpwm\":" + to_string(MarblesEngine::GetTPwMean());
                s += ",\"tpws\":" + to_string(MarblesEngine::GetTPwStd());
                s += ",\"tdv\":" + to_string(MarblesEngine::GetTDejaVu());
                s += ",\"xsc\":" + to_string(MarblesEngine::GetXScale());
                s += ",\"xrg\":" + to_string(MarblesEngine::GetXRange());
                s += ",\"xc\":" + to_string(MarblesEngine::GetXControl());
                s += ",\"xr\":" + to_string(MarblesEngine::GetXRegister() ? 1 : 0);
                s += ",\"xrv\":" + to_string(MarblesEngine::GetXRegisterValue());
                s += ",\"xsp\":" + to_string(MarblesEngine::GetXSpread());
                s += ",\"xb\":" + to_string(MarblesEngine::GetXBias());
                s += ",\"xst\":" + to_string(MarblesEngine::GetXSteps());
                s += ",\"xdv\":" + to_string(MarblesEngine::GetXDejaVu());
                s += ",\"yrg\":" + to_string(MarblesEngine::GetYRange());
                s += ",\"ysp\":" + to_string(MarblesEngine::GetYSpread());
                s += ",\"yb\":" + to_string(MarblesEngine::GetYBias());
                s += ",\"yst\":" + to_string(MarblesEngine::GetYSteps());
                s += ",\"yd\":" + to_string(MarblesEngine::GetYDejaVu());
                s += ",\"ydiv\":" + to_string(MarblesEngine::GetYDivider());
                s += ",\"dva\":" + to_string(MarblesEngine::GetDejaVuAmount());
                s += ",\"dvl\":" + to_string(MarblesEngine::GetDejaVuLength());
                s += ",\"ts0\":" + to_string(MarblesEngine::GetTSlot(0));
                s += ",\"ts1\":" + to_string(MarblesEngine::GetTSlot(1));
                s += ",\"ts2\":" + to_string(MarblesEngine::GetTSlot(2));
                s += ",\"xs0\":" + to_string(MarblesEngine::GetXSlot(0));
                s += ",\"xs1\":" + to_string(MarblesEngine::GetXSlot(1));
                s += ",\"xs2\":" + to_string(MarblesEngine::GetXSlot(2));
                s += ",\"ys\":" + to_string(MarblesEngine::GetYSlot());
                for (int i = 0; i < 8; i++) {
                    s += ",\"cc" + to_string(i) + "\":" + to_string(MarblesEngine::GetDynamicSlotCC(i));
                    s += ",\"cch" + to_string(i) + "\":" + to_string(MarblesEngine::GetDynamicSlotChan(i));
                }
                s += ",\"learn\":" + to_string(MarblesEngine::IsLearning() ? 1 : 0);
                s += ",\"lls\":" + to_string(MarblesEngine::GetLastLearnedSlot());
                s += "}}";
                sendStr(s);
            }

            void pushBootMetadata() {
                pushPluginList();
                pushActivePlugin();
                pushParams(0);
                pushParams(1);
                pushConfig();
                pushPresets(0);
                pushPresets(1);
                pushFavorites();
                pushMarbles();
                pushIoCaps();
                seedLastPushed();
                ESP_LOGI(TAG, "boot metadata pushed");
            }

            // ---------------------------------------------------------------------------
            // Diff-poll: FNV-1a over the model's cached JSON + a cheap hash
            // over the marbles getters. Any mutation source (panel link, bench
            // scripts, the A1S-local UI until P6) shows up as a hash change
            // and re-pushes within one stream tick.
            // ---------------------------------------------------------------------------

            uint32_t hashBytes(const char *p) {
                if (p == nullptr) return 0;
                uint32_t h = 2166136261u;
                for (size_t i = 0; p[i] != '\0' && i < kMaxHashBlob; i++) {
                    h ^= (uint8_t)p[i];
                    h *= 16777619u;
                }
                return h;
            }

            uint32_t hashMarbles() {
                uint32_t h = 2166136261u;
                auto mix = [&h](uint32_t v) { h ^= v; h *= 16777619u; };
                union { float f; uint32_t u; } bit;
                mix(MarblesEngine::GetTModel()); mix(MarblesEngine::GetTRange());
                mix(MarblesEngine::GetXYClockSource()); mix(MarblesEngine::GetTExternal());
                bit.f = MarblesEngine::GetTRate(); mix(bit.u);
                bit.f = MarblesEngine::GetTBias(); mix(bit.u);
                bit.f = MarblesEngine::GetTJitter(); mix(bit.u);
                mix(MarblesEngine::GetTPwMean()); mix(MarblesEngine::GetTPwStd());
                mix(MarblesEngine::GetTDejaVu()); mix(MarblesEngine::GetXScale());
                mix(MarblesEngine::GetXRange()); mix(MarblesEngine::GetXControl());
                mix(MarblesEngine::GetXRegister());
                bit.f = MarblesEngine::GetXRegisterValue(); mix(bit.u);
                bit.f = MarblesEngine::GetXSpread(); mix(bit.u);
                bit.f = MarblesEngine::GetXBias(); mix(bit.u);
                bit.f = MarblesEngine::GetXSteps(); mix(bit.u);
                mix(MarblesEngine::GetXDejaVu()); mix(MarblesEngine::GetYRange());
                bit.f = MarblesEngine::GetYSpread(); mix(bit.u);
                bit.f = MarblesEngine::GetYBias(); mix(bit.u);
                bit.f = MarblesEngine::GetYSteps(); mix(bit.u);
                mix(MarblesEngine::GetYDejaVu()); mix(MarblesEngine::GetYDivider());
                bit.f = MarblesEngine::GetDejaVuAmount(); mix(bit.u);
                mix(MarblesEngine::GetDejaVuLength());
                for (int i = 0; i < 3; i++) {
                    mix(MarblesEngine::GetTSlot(i)); mix(MarblesEngine::GetXSlot(i));
                }
                mix(MarblesEngine::GetYSlot());
                for (int i = 0; i < 8; i++) {
                    mix(MarblesEngine::GetDynamicSlotCC(i));
                    mix(MarblesEngine::GetDynamicSlotChan(i));
                }
                mix(MarblesEngine::IsLearning());
                return h;
            }

            struct LastPushed {
                uint32_t pluginList = 0, params0 = 0, params1 = 0, config = 0;
                uint32_t presets0 = 0, presets1 = 0, favorites = 0;
                string id0, id1;
                uint32_t marbles = 0;
                int sdMounted = -1;
            } lastPushed;

            // Record current hashes without pushing, right after the boot
            // burst — otherwise the first pushOnChange() would re-push
            // everything a second time.
            void seedLastPushed() {
                lastPushed.pluginList = hashBytes(GetCStrSafe(SoundProcessorManager::GetCStrJSONSoundProcessors()));
                lastPushed.params0 = hashBytes(GetCStrSafe(SoundProcessorManager::GetCStrJSONActivePluginParams(0)));
                lastPushed.params1 = hashBytes(GetCStrSafe(SoundProcessorManager::GetCStrJSONActivePluginParams(1)));
                lastPushed.config = hashBytes(GetCStrSafe(SoundProcessorManager::GetCStrJSONConfiguration()));
                lastPushed.presets0 = hashBytes(GetCStrSafe(SoundProcessorManager::GetCStrJSONGetPresets(0)));
                lastPushed.presets1 = hashBytes(GetCStrSafe(SoundProcessorManager::GetCStrJSONGetPresets(1)));
                string favs = Favorites::GetAllFavorites();
                lastPushed.favorites = hashBytes(favs.c_str());
                lastPushed.id0 = SoundProcessorManager::GetStringID(0);
                lastPushed.id1 = SoundProcessorManager::GetStringID(1);
                lastPushed.marbles = hashMarbles();
                lastPushed.sdMounted = FileSystem::IsSDMounted() ? 1 : 0;
            }

            void pushOnChange() {
                uint32_t h;
                h = hashBytes(GetCStrSafe(SoundProcessorManager::GetCStrJSONSoundProcessors()));
                if (h != lastPushed.pluginList) { lastPushed.pluginList = h; pushPluginList(); }
                h = hashBytes(GetCStrSafe(SoundProcessorManager::GetCStrJSONActivePluginParams(0)));
                if (h != lastPushed.params0) { lastPushed.params0 = h; pushParams(0); }
                h = hashBytes(GetCStrSafe(SoundProcessorManager::GetCStrJSONActivePluginParams(1)));
                if (h != lastPushed.params1) { lastPushed.params1 = h; pushParams(1); }
                h = hashBytes(GetCStrSafe(SoundProcessorManager::GetCStrJSONConfiguration()));
                if (h != lastPushed.config) { lastPushed.config = h; pushConfig(); }
                h = hashBytes(GetCStrSafe(SoundProcessorManager::GetCStrJSONGetPresets(0)));
                if (h != lastPushed.presets0) { lastPushed.presets0 = h; pushPresets(0); }
                h = hashBytes(GetCStrSafe(SoundProcessorManager::GetCStrJSONGetPresets(1)));
                if (h != lastPushed.presets1) { lastPushed.presets1 = h; pushPresets(1); }
                {
                    string favs = Favorites::GetAllFavorites();
                    h = hashBytes(favs.c_str());
                    if (h != lastPushed.favorites) { lastPushed.favorites = h; pushFavorites(); }
                }
                {
                    string id0 = SoundProcessorManager::GetStringID(0);
                    string id1 = SoundProcessorManager::GetStringID(1);
                    if (id0 != lastPushed.id0 || id1 != lastPushed.id1) {
                        lastPushed.id0 = id0;
                        lastPushed.id1 = id1;
                        pushActivePlugin();
                    }
                }
                h = hashMarbles();
                if (h != lastPushed.marbles || marblesPushPending) {
                    lastPushed.marbles = h;
                    marblesPushPending = false;
                    pushMarbles();
                }
                int mounted = FileSystem::IsSDMounted() ? 1 : 0;
                if (mounted != lastPushed.sdMounted) {
                    lastPushed.sdMounted = mounted;
                    pushIoCaps();
                }
            }

            void sendStream() {
                string s = "{\"cmd\":\"";
                s += kMsgStream;
                s += "\",\"vu\":["
                     + to_string(AUDIO::SoundProcessorManager::GetVUPeak(0)) + ","
                     + to_string(AUDIO::SoundProcessorManager::GetVUPeak(1)) + ","
                     + to_string(AUDIO::SoundProcessorManager::GetVUPeak(2)) + ","
                     + to_string(AUDIO::SoundProcessorManager::GetVUPeak(3)) + "]";
                s += ",\"mb\":" + to_string(MarblesEngine::GetOutputSnapshot());
                s += ",\"ov\":" + to_string(AUDIO::SoundProcessorManager::GetCPUOverload() ? 1 : 0);
                s += ",\"sd\":{\"p\":" + to_string(AUDIO::SDAudio::IsPlaying() ? 1 : 0);
                s += ",\"r\":" + to_string(AUDIO::SDAudio::IsRecording() ? 1 : 0);
                s += ",\"pos\":" + to_string(AUDIO::SDAudio::GetPositionFrames());
                s += ",\"tot\":" + to_string(AUDIO::SDAudio::GetTotalFrames());
                s += "}}";
                sendStr(s);
            }

            // ---------------------------------------------------------------------------
            // /link/ command handlers (the /debug/* set goes to the shared
            // DebugUI dispatcher).
            // ---------------------------------------------------------------------------

            bool getCh(const Document &d, int &ch) {
                ch = 0;
                if (d.HasMember("ch") && d["ch"].IsInt()) ch = d["ch"].GetInt();
                if (ch < 0 || ch > 1) return false;
                return true;
            }

            // One Marbles key -> engine setter. `v` is the raw value (double
            // from JSON); the engine setters clamp.
            bool applyMarblesKey(const string &k, double v) {
                int iv = (int)v;
                bool bv = v != 0.0;
                if (k == "tm") MarblesEngine::SetTModel(iv);
                else if (k == "rst") MarblesEngine::RequestReset();
                else if (k == "tr") MarblesEngine::SetTRange(iv);
                else if (k == "txy") MarblesEngine::SetXYClockSource(iv);
                else if (k == "te") MarblesEngine::SetTExternal(bv);
                else if (k == "trate") MarblesEngine::SetTRate((float)v);
                else if (k == "tb") MarblesEngine::SetTBias((float)v);
                else if (k == "tj") MarblesEngine::SetTJitter((float)v);
                else if (k == "tpwm") MarblesEngine::SetTPwMean(iv);
                else if (k == "tpws") MarblesEngine::SetTPwStd(iv);
                else if (k == "tdv") MarblesEngine::SetTDejaVu(iv);
                else if (k == "xsc") MarblesEngine::SetXScale(iv);
                else if (k == "xrg") MarblesEngine::SetXRange(iv);
                else if (k == "xc") MarblesEngine::SetXControl(iv);
                else if (k == "xr") MarblesEngine::SetXRegister(bv);
                else if (k == "xrv") MarblesEngine::SetXRegisterValue((float)v);
                else if (k == "xsp") MarblesEngine::SetXSpread((float)v);
                else if (k == "xb") MarblesEngine::SetXBias((float)v);
                else if (k == "xst") MarblesEngine::SetXSteps((float)v);
                else if (k == "xdv") MarblesEngine::SetXDejaVu(iv);
                else if (k == "yrg") MarblesEngine::SetYRange(iv);
                else if (k == "ysp") MarblesEngine::SetYSpread((float)v);
                else if (k == "yb") MarblesEngine::SetYBias((float)v);
                else if (k == "yst") MarblesEngine::SetYSteps((float)v);
                else if (k == "yd") MarblesEngine::SetYDejaVu(iv);
                else if (k == "ydiv") MarblesEngine::SetYDivider(iv);
                else if (k == "dva") MarblesEngine::SetDejaVuAmount((float)v);
                else if (k == "dvl") MarblesEngine::SetDejaVuLength(iv);
                else if (k == "ts0") MarblesEngine::SetTSlot(0, iv);
                else if (k == "ts1") MarblesEngine::SetTSlot(1, iv);
                else if (k == "ts2") MarblesEngine::SetTSlot(2, iv);
                else if (k == "xs0") MarblesEngine::SetXSlot(0, iv);
                else if (k == "xs1") MarblesEngine::SetXSlot(1, iv);
                else if (k == "xs2") MarblesEngine::SetXSlot(2, iv);
                else if (k == "ys") MarblesEngine::SetYSlot(iv);
                else if (k == "learn") { if (bv) MarblesEngine::StartLearn(); else MarblesEngine::StopLearn(); }
                // Dynamic CC-slot keys — must mirror pushMarbles()'s
                // cc<i>/cch<i> push keys; the panel's CC-slot editor sends them.
                else if (k.size() == 3 && k.compare(0, 2, "cc") == 0 && k[2] >= '0' && k[2] <= '7')
                    MarblesEngine::SetDynamicSlotCC(k[2] - '0', iv);
                else if (k.size() == 4 && k.compare(0, 3, "cch") == 0 && k[3] >= '0' && k[3] <= '7')
                    MarblesEngine::SetDynamicSlotChan(k[3] - '0', iv);
                else return false;
                return true;
            }

            // SD proxy listing: the DSP owns the card (SDD §4.2).
            void handleFsList(const Document &d) {
                string path = "/sd";
                if (d.HasMember("path") && d["path"].IsString()) path = d["path"].GetString();
                // Reject anything escaping the SD tree.
                if (path.rfind("/sd", 0) != 0) {
                    sendStr(string("{\"cmd\":\"") + kMsgError + "\",\"what\":\"bad path\"}");
                    return;
                }
                // Build [["name",isDir],...] as escaped JSON.
                string entries = "[";
                DIR *dir = opendir(path.c_str());
                if (dir != nullptr) {
                    struct dirent *ent;
                    bool first = true;
                    while ((ent = readdir(dir)) != nullptr) {
                        const char *name = ent->d_name;
                        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
                        if (!first) entries += ",";
                        first = false;
                        StringBuffer sb;
                        Writer<StringBuffer> w(sb);
                        w.StartArray();
                        w.String(name);
                        w.Int(ent->d_type == DT_DIR ? 1 : 0);
                        w.EndArray();
                        entries += sb.GetString();
                        if (entries.size() > kTxScratch - 512) break;
                    }
                    closedir(dir);
                }
                entries += "]";
                string s = "{\"cmd\":\"";
                s += kMsgFsList;
                s += ",";
                appendEscapedField(s, "entries", entries.c_str());
                if (d.HasMember("seq") && d["seq"].IsInt()) {
                    s += ",\"seq\":" + to_string(d["seq"].GetInt());
                }
                s += "}";
                sendStr(s);
            }

            // Returns true when handled; `response` is sent framed to the panel.
            bool handleLinkCommand(const string &doc, string &response) {
                Document d;
                d.Parse(doc);
                if (d.HasParseError() || !d.IsObject() || !d.HasMember("cmd")) return false;
                string cmdName(d["cmd"].GetString());

                // The bench dispatcher serves the whole /debug/* set on this
                // transport too — one implementation (SDD §4).
                if (cmdName.rfind("/debug/", 0) == 0) {
                    return DebugUI::handleCommand(doc, response);
                }
                if (cmdName.rfind("/link/", 0) != 0) return false;

                if (cmdName == "/link/paramSet") {
                    // Generalizes the bench setPluginParam: key is "current",
                    // "cv" or "trig" (SDD §6 DspProxy surface).
                    int ch;
                    if (getCh(d, ch) && d.HasMember("id") && d["id"].IsString() &&
                        d.HasMember("key") && d["key"].IsString() &&
                        d.HasMember("value") && d["value"].IsInt()) {
                        SoundProcessorManager::SetChannelParamValue(
                            ch, d["id"].GetString(), d["key"].GetString(), d["value"].GetInt());
                        response = "{\"ok\":1}";
                    } else {
                        response = "{\"error\":\"bad paramSet\"}";
                    }
                    return true;
                }
                if (cmdName == kMsgCmdPresetSave) {
                    int ch;
                    if (!getCh(d, ch) || !d.HasMember("name") || !d["name"].IsString() ||
                        !d.HasMember("number") || !d["number"].IsInt()) {
                        response = "{\"error\":\"bad presetSave\"}";
                    } else {
                        SoundProcessorManager::ChannelSavePreset(ch, d["name"].GetString(), d["number"].GetInt());
                        response = "{\"ok\":1}";
                    }
                    return true;
                }
                if (cmdName == kMsgCmdPresetLoad) {
                    int ch;
                    if (!getCh(d, ch) || !d.HasMember("number") || !d["number"].IsInt()) {
                        response = "{\"error\":\"bad presetLoad\"}";
                    } else {
                        SoundProcessorManager::ChannelLoadPreset(ch, d["number"].GetInt());
                        // A preset load changes params + the preset list.
                        pushParams(ch);
                        pushPresets(ch);
                        response = "{\"ok\":1}";
                    }
                    return true;
                }
                if (cmdName == kMsgCmdFavoriteActivate) {
                    if (d.HasMember("id") && d["id"].IsInt()) {
                        Favorites::ActivateFavorite(d["id"].GetInt());
                        response = "{\"ok\":1}";
                    } else {
                        response = "{\"error\":\"bad favoriteActivate\"}";
                    }
                    return true;
                }
                if (cmdName == kMsgCmdSdStartPlayback) {
                    if (d.HasMember("path") && d["path"].IsString()) {
                        AUDIO::SDAudio::StartPlayback(d["path"].GetString());
                        response = "{\"ok\":1}";
                    } else {
                        response = "{\"error\":\"bad path\"}";
                    }
                    return true;
                }
                if (cmdName == kMsgCmdSdStartRecording) {
                    if (d.HasMember("path") && d["path"].IsString()) {
                        AUDIO::SDAudio::StartRecording(d["path"].GetString());
                        response = "{\"ok\":1}";
                    } else {
                        response = "{\"error\":\"bad path\"}";
                    }
                    return true;
                }
                if (cmdName == kMsgCmdSdStop) {
                    AUDIO::SDAudio::Stop();
                    response = "{\"ok\":1}";
                    return true;
                }
                // Live codec previews from the MIX/System pages. Deliberately
                // NOT config commits — the pages commit persisted values via
                // /link/configSet when editing ends (same split as the local
                // UI, which never persists per encoder step).
                if (cmdName == "/link/codecGain") {
                    if (d.HasMember("g") && d["g"].IsInt()) {
                        DRIVERS::Codec::SetInputGain(d["g"].GetInt());
                        response = "{\"ok\":1}";
                    } else {
                        response = "{\"error\":\"bad g\"}";
                    }
                    return true;
                }
                if (cmdName == "/link/codecLevels") {
                    if (d.HasMember("l") && d["l"].IsInt() && d.HasMember("r") && d["r"].IsInt()) {
                        DRIVERS::Codec::SetOutputLevels(
                            (uint32_t)d["l"].GetInt(), (uint32_t)d["r"].GetInt());
                        response = "{\"ok\":1}";
                    } else {
                        response = "{\"error\":\"bad l/r\"}";
                    }
                    return true;
                }
                if (cmdName == "/link/configSet") {
                    if (d.HasMember("config") && d["config"].IsString()) {
                        SoundProcessorManager::SetConfigurationFromJSON(
                            string(d["config"].GetString()));
                        response = "{\"ok\":1}";
                    } else {
                        response = "{\"error\":\"bad config\"}";
                    }
                    return true;
                }
                if (cmdName == kMsgCmdMarblesSet) {
                    if (d.HasMember("k") && d["k"].IsString() && d.HasMember("v") &&
                        (d["v"].IsNumber())) {
                        string k(d["k"].GetString());
                        if (k == "_save") {
                            MarblesEngine::SaveConfig();
                            response = "{\"ok\":1}";
                        } else if (applyMarblesKey(k, d["v"].GetDouble())) {
                            response = "{\"ok\":1}";
                        } else {
                            response = "{\"error\":\"unknown key\"}";
                        }
                    } else {
                        response = "{\"error\":\"bad marblesSet\"}";
                    }
                    return true;
                }
                if (cmdName == kMsgCmdMarblesGet) {
                    marblesPushPending = true;
                    response = "{\"ok\":1}";
                    return true;
                }
                if (cmdName == kMsgFsList) {
                    handleFsList(d);
                    return true;
                }
                if (cmdName == kMsgCmdMidiFlags) {
                    // P4: MIDI source enables (DIN via link, BLE via panel).
                    // No args = query. Reply is cmd-tagged so the panel's
                    // frame parser can update its cache.
                    if (d.HasMember("uart") && d["uart"].IsBool())
                        Midi::SetUartEnabled(d["uart"].GetBool());
                    if (d.HasMember("ble") && d["ble"].IsBool())
                        Midi::SetBleEnabled(d["ble"].GetBool());
                    response = string("{\"cmd\":\"") + kMsgCmdMidiFlags
                             + "\",\"ok\":1,\"uart\":"
                             + (Midi::IsUartEnabled() ? "true" : "false")
                             + ",\"ble\":"
                             + (Midi::IsBleEnabled() ? "true" : "false") + "}";
                    return true;
                }
                if (cmdName == kMsgMidiIn) {
                    // Panel relays TRS-DIN bytes as an array of byte values.
                    // One-way, high-rate: no reply frame.
                    if (d.HasMember("d") && d["d"].IsArray()) {
                        const Value &arr = d["d"];
                        uint8_t bytes[128];
                        size_t n = 0;
                        for (size_t i = 0; i < arr.Size() && n < sizeof(bytes); i++)
                            if (arr[i].IsInt())
                                bytes[n++] = (uint8_t) (arr[i].GetInt() & 0xFF);
                        if (n) {
                            Midi::FeedLinkBytes(bytes, n);
                        }
                    }
                    return true;
                }

                response = "{\"error\":\"unknown link command\"}";
                return true;
            }

            // ---------------------------------------------------------------------------
            // Link task
            // ---------------------------------------------------------------------------

            void linkTask(void *) {
                string cmd;
                bool receiving = false;
                uint8_t rx[256];
                bool bootPushed = false;

                while (true) {
                    uint32_t now = nowMs();

                    int got = uart_read_bytes(kUart, rx, sizeof(rx), 0);
                    if (got > 0) {
                        lastPanelRxMs = now;
                        for (int i = 0; i < got; i++) {
                            char c = (char)rx[i];
                            if (c == (char)kStx) {
                                receiving = true;
                                cmd.clear();
                            } else if (c == (char)kEtx) {
                                if (receiving && cmd.size() <= kMaxDoc) {
                                    // Panel handshake?
                                    HelloReply hello = ParseHandshake(cmd.data(), cmd.size());
                                    if (hello.valid && !hello.isAck &&
                                        hello.role == Role::kPanel) {
                                        if (VersionCompatible(hello.major)) {
                                            string ack = MakeHelloAck(Role::kDsp,
                                                                      TBD_FW_VERSION.c_str(),
                                                                      true, nullptr);
                                            sendStr(ack);
                                            s_online = true;
                                            bootPushed = false;
                                            lastStreamMs = now;
                                            lastHeartbeatMs = now;
                                            ESP_LOGI(TAG, "panel handshake ok (proto %u.%u)",
                                                     hello.major, hello.minor);
                                        } else {
                                            sendStr(MakeHelloAck(Role::kDsp,
                                                                 TBD_FW_VERSION.c_str(), false,
                                                                 "version mismatch"));
                                            ESP_LOGW(TAG, "refused panel: version mismatch");
                                        }
                                    } else if (s_online) {
                                        string response;
                                        // Empty response = one-way command
                                        // (e.g. midiIn): no reply frame.
                                        if (handleLinkCommand(cmd, response) &&
                                            !response.empty())
                                            sendStr(response);
                                    }
                                }
                                receiving = false;
                                cmd.clear();
                            } else if (receiving) {
                                if (cmd.size() < kMaxDoc) cmd.push_back(c);
                            }
                        }
                    }

                    // Offline watchdog: no panel byte for a while.
                    if (s_online && (now - lastPanelRxMs) > kOfflineAfterMs) {
                        s_online = false;
                        bootPushed = false;
                        ESP_LOGW(TAG, "panel went quiet — link offline");
                    }

                    if (s_online) {
                        if (!bootPushed) {
                            // One settle delay so the panel task is draining
                            // before the burst arrives.
                            vTaskDelay(pdMS_TO_TICKS(50));
                            pushBootMetadata();
                            bootPushed = true;
                            lastStreamMs = nowMs();
                            lastHeartbeatMs = lastStreamMs;
                        } else {
                            if (now - lastStreamMs >= kStreamPeriodMs) {
                                lastStreamMs = now;
                                sendStream();
                                pushOnChange();
                            }
                            if (now - lastHeartbeatMs >= kHeartbeatPeriodMs) {
                                lastHeartbeatMs = now;
                                sendStr(MakeHeartbeat(now, ++heartbeatSeq));
                            }
                        }
                    }

                    // P4: the link task is Midi::Update()'s caller now —
                    // the parser drains link-relayed MIDI bytes and fills
                    // the CV/trig arrays the audio task reads per block
                    // (the old Control::Update caller stays uncalled).
                    Midi::Update();

                    // One FULL tick (10 ms at FREERTOS_HZ=100): finer delays
                    // round to zero ticks and busy-spin into task_wdt — the
                    // same trap DebugUI's POLL_TICKS comment documents. The
                    // 16 KB RX ring buffers ~170 ms at line rate.
                    vTaskDelay(1);
                }
            }
        }

        bool LinkEndpoint::IsOnline() { return s_online; }

        bool LinkEndpoint::SetBaud(int baud) {
            if (baud < 1200 || baud > 5000000) return false;
            if (uart_set_baudrate(kUart, baud) != ESP_OK) return false;
            ESP_LOGI(TAG, "link baud retuned to %d", baud);
            return true;
        }

        void LinkEndpoint::Start() {
            if (hLinkTask != nullptr) return;

            // The midiuart static instance installed UART1 at 31250 with a
            // 256-byte RX ring. Nothing reads it at runtime (Midi::Update has
            // no caller on BBA), so replace the driver with link-sized rings.
            uart_driver_delete(kUart);
            uart_config_t uc = {};
            uc.baud_rate = kLinkBaud;
            uc.data_bits = UART_DATA_8_BITS;
            uc.parity = UART_PARITY_DISABLE;
            uc.stop_bits = UART_STOP_BITS_1;
            uc.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
            uc.rx_flow_ctrl_thresh = 0;
            uc.source_clk = UART_SCLK_DEFAULT;
            ESP_ERROR_CHECK(uart_set_pin(kUart, 18, 23, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
            ESP_ERROR_CHECK(uart_param_config(kUart, &uc));
            ESP_ERROR_CHECK(uart_driver_install(kUart, kRxRing, kTxRing, 0, nullptr, 0));

            txScratch = (char *)heap_caps_malloc(kTxScratch, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (txScratch == nullptr)
                txScratch = (char *)heap_caps_malloc(kTxScratch, MALLOC_CAP_8BIT);
            if (txScratch == nullptr) {
                ESP_LOGE(TAG, "no TX scratch — link disabled");
                return;
            }

            if (xTaskCreatePinnedToCore(linkTask, "link_ep", 8192, nullptr,
                                        tskIDLE_PRIORITY + 1, &hLinkTask, 0) != pdPASS) {
                ESP_LOGW(TAG, "could not start link endpoint task");
                hLinkTask = nullptr;
                return;
            }
            ESP_LOGI(TAG, "link endpoint on UART1 GPIO18/23 @%d", kLinkBaud);
        }
    }
}
