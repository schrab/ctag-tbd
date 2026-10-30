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

// Link-backed DspProxy (doc/ui-split-sdd.md §4.3): getters serve the C3
// cache (populated by the DSP's boot metadata + on-change pushes + 10 Hz
// stream), setters update the cache optimistically and send /link commands.
// No getter ever round-trips the link except ListSdDir, which is
// user-paced navigation.

#include "DspProxy.hpp"
#include "MidiLink.hpp"
#include "LinkClient.hpp"
#include "ctag/link/protocol.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace std;
using namespace rapidjson;
using namespace CTAG::LINK;

static const char *TAG = "DspProxy";

namespace CTAG {
    namespace CTRL {

        namespace {
            // Slot bounds mirror the BBA DSP slot space (N_CVS=102, N_TRIGS=43).
            constexpr int kNCvs = 102;
            constexpr int kNTrigs = 43;

            int clampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
            float clampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

            // --- cache ---
            bool cacheMidiUart = true;   // DSP defaults (Midi.cpp statics)
            bool cacheMidiBle = true;
            string cachePluginList = "[]";
            // Live parameter table — STATIC, not a cached JSON string: the
            // no-PSRAM C3 cannot hold (or DOM-parse) whole-plugin params
            // next to NimBLE. Filled incrementally by the chunked
            // /push/params frames; leaves between a group marker and the
            // next belong to that group.
            DspProxy::DspParamEntry paramTable[2][DspProxy::DSP_MAX_PARAMS];
            int paramTableCount[2] = {0, 0};
            // Bumped when a chunked push completes a channel's table; the
            // Params page watches it to refresh in place.
            uint32_t paramTableVersion = 0;
            string cacheConfig = "{}";
            string cachePresets[2] = {"{\"presets\":[]}", "{\"presets\":[]}"};
            string cacheFavorites = "[]";
            string activeId[2];
            bool activeStereo[2] = {false, false};

            uint32_t vu[4] = {0, 0, 0, 0};
            uint32_t snapshot = 0;
            bool cpuOverload = false;
            bool sdPlaying = false, sdRecording = false;
            uint32_t sdPos = 0, sdTot = 0;
            bool sdMounted = false;
            int32_t activeFav = -1;

            // Mirrors the A1S pushMarbles key table (LinkEndpoint.cpp).
            struct MarblesState {
                int tModel = 0, tRange = 1, xyClockSource = 0;
                int xScale = 0, xRange = 1, xControl = 0, xDejaVu = 0;
                int yRange = 1, yDivider = 8, yDejaVu = 0;
                int tDejaVu = 0, tpwMean = 128, tpwStd = 64;
                int dejaVuLength = 4;
                float tRate = 0, tBias = 0.5f, tJitter = 0.1f;
                float xRegisterValue = 0.5f, xSpread = 0.5f, xBias = 0.5f, xSteps = 0.5f;
                float ySpread = 0.5f, yBias = 0.5f, ySteps = 0.5f;
                float dejaVuAmount = 0.5f;
                bool tExternal = false, xRegister = false;
                int tSlot[3] = {40, 41, 42};
                int xSlot[3] = {98, 99, 100};
                int ySlot = 101;
                int ccSlot[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
                int ccChan[8] = {0, 0, 0, 0, 0, 0, 0, 0};
                bool learning = false;
                int lastLearned = -1;
            };
            MarblesState mb;

            // --- command senders ---
            void sendCmd(const string &payload) {
                LinkClient::SendCommand(payload);
            }

            void sendMarblesSet(const char *key, double v) {
                char buf[96];
                // %g keeps floats compact; the DSP setters clamp anyway.
                snprintf(buf, sizeof(buf), "{\"cmd\":\"/link/marblesSet\",\"k\":\"%s\",\"v\":%g}",
                         key, v);
                sendCmd(buf);
            }

            void applyMarblesLocal(const char *key, double v) {
                int iv = (int)v;
                bool bv = v != 0.0;
                // Same clamps as the engine setters, so the UI shows the
                // value it will get even before the next push confirms.
                if (!strcmp(key, "tm")) mb.tModel = clampI(iv, 0, 6);
                else if (!strcmp(key, "tr")) mb.tRange = clampI(iv, 0, 2);
                else if (!strcmp(key, "txy")) mb.xyClockSource = clampI(iv, 0, 4);
                else if (!strcmp(key, "te")) mb.tExternal = bv;
                else if (!strcmp(key, "trate")) mb.tRate = clampF((float)v, -60.0f, 60.0f);
                else if (!strcmp(key, "tb")) mb.tBias = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "tj")) mb.tJitter = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "tpwm")) mb.tpwMean = clampI(iv, 0, 255);
                else if (!strcmp(key, "tpws")) mb.tpwStd = clampI(iv, 0, 255);
                else if (!strcmp(key, "tdv")) mb.tDejaVu = clampI(iv, 0, 2);
                else if (!strcmp(key, "xsc")) mb.xScale = clampI(iv, 0, 5);
                else if (!strcmp(key, "xrg")) mb.xRange = clampI(iv, 0, 2);
                else if (!strcmp(key, "xc")) mb.xControl = clampI(iv, 0, 2);
                else if (!strcmp(key, "xr")) mb.xRegister = bv;
                else if (!strcmp(key, "xrv")) mb.xRegisterValue = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "xsp")) mb.xSpread = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "xb")) mb.xBias = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "xst")) mb.xSteps = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "xdv")) mb.xDejaVu = clampI(iv, 0, 2);
                else if (!strcmp(key, "yrg")) mb.yRange = clampI(iv, 0, 2);
                else if (!strcmp(key, "ysp")) mb.ySpread = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "yb")) mb.yBias = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "yst")) mb.ySteps = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "yd")) mb.yDejaVu = clampI(iv, 0, 2);
                else if (!strcmp(key, "ydiv")) mb.yDivider = clampI(iv, 0, 11);
                else if (!strcmp(key, "dva")) mb.dejaVuAmount = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "dvl")) mb.dejaVuLength = clampI(iv, 1, 16);
                else if (!strcmp(key, "ts0")) mb.tSlot[0] = clampI(iv, -1, kNTrigs - 1);
                else if (!strcmp(key, "ts1")) mb.tSlot[1] = clampI(iv, -1, kNTrigs - 1);
                else if (!strcmp(key, "ts2")) mb.tSlot[2] = clampI(iv, -1, kNTrigs - 1);
                else if (!strcmp(key, "xs0")) mb.xSlot[0] = clampI(iv, -1, kNCvs - 1);
                else if (!strcmp(key, "xs1")) mb.xSlot[1] = clampI(iv, -1, kNCvs - 1);
                else if (!strcmp(key, "xs2")) mb.xSlot[2] = clampI(iv, -1, kNCvs - 1);
                else if (!strcmp(key, "ys")) mb.ySlot = clampI(iv, -1, kNCvs - 1);
                else if (!strcmp(key, "learn")) mb.learning = bv;
            }

            // --- push parsing ---
            void parsePushMarbles(const Document &d) {
                if (!d.HasMember("st") || !d["st"].IsObject()) return;
                const Value &s = d["st"];
                auto gi = [&](const char *k, int def) {
                    return s.HasMember(k) && s[k].IsInt() ? s[k].GetInt() : def;
                };
                auto gf = [&](const char *k, float def) {
                    return s.HasMember(k) && s[k].IsNumber() ? (float)s[k].GetDouble() : def;
                };
                auto gb = [&](const char *k, bool def) {
                    return s.HasMember(k) && s[k].IsInt() ? s[k].GetInt() != 0 : def;
                };
                mb.tModel = gi("tm", mb.tModel);
                mb.tRange = gi("tr", mb.tRange);
                mb.xyClockSource = gi("txy", mb.xyClockSource);
                mb.tExternal = gb("te", mb.tExternal);
                mb.tRate = gf("trate", mb.tRate);
                mb.tBias = gf("tb", mb.tBias);
                mb.tJitter = gf("tj", mb.tJitter);
                mb.tpwMean = gi("tpwm", mb.tpwMean);
                mb.tpwStd = gi("tpws", mb.tpwStd);
                mb.tDejaVu = gi("tdv", mb.tDejaVu);
                mb.xScale = gi("xsc", mb.xScale);
                mb.xRange = gi("xrg", mb.xRange);
                mb.xControl = gi("xc", mb.xControl);
                mb.xRegister = gb("xr", mb.xRegister);
                mb.xRegisterValue = gf("xrv", mb.xRegisterValue);
                mb.xSpread = gf("xsp", mb.xSpread);
                mb.xBias = gf("xb", mb.xBias);
                mb.xSteps = gf("xst", mb.xSteps);
                mb.xDejaVu = gi("xdv", mb.xDejaVu);
                mb.yRange = gi("yrg", mb.yRange);
                mb.ySpread = gf("ysp", mb.ySpread);
                mb.yBias = gf("yb", mb.yBias);
                mb.ySteps = gf("yst", mb.ySteps);
                mb.yDejaVu = gi("yd", mb.yDejaVu);
                mb.yDivider = gi("ydiv", mb.yDivider);
                mb.dejaVuAmount = gf("dva", mb.dejaVuAmount);
                mb.dejaVuLength = gi("dvl", mb.dejaVuLength);
                mb.tSlot[0] = gi("ts0", mb.tSlot[0]);
                mb.tSlot[1] = gi("ts1", mb.tSlot[1]);
                mb.tSlot[2] = gi("ts2", mb.tSlot[2]);
                mb.xSlot[0] = gi("xs0", mb.xSlot[0]);
                mb.xSlot[1] = gi("xs1", mb.xSlot[1]);
                mb.xSlot[2] = gi("xs2", mb.xSlot[2]);
                mb.ySlot = gi("ys", mb.ySlot);
                for (int i = 0; i < 8; i++) {
                    char k[8];
                    snprintf(k, sizeof(k), "cc%d", i);
                    mb.ccSlot[i] = gi(k, mb.ccSlot[i]);
                    snprintf(k, sizeof(k), "cch%d", i);
                    mb.ccChan[i] = gi(k, mb.ccChan[i]);
                }
                mb.learning = gb("learn", mb.learning);
                mb.lastLearned = gi("lls", mb.lastLearned);
            }
        }

        void DspProxy::Init() {
            LinkClient::Start();
        }

        bool DspProxy::IsMidiUartEnabled() { return cacheMidiUart; }
bool DspProxy::IsMidiBleEnabled() { return cacheMidiBle; }

void DspProxy::SetMidiFlags(bool uart, bool ble) {
    cacheMidiUart = uart;
    cacheMidiBle = ble;
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"/link/midiFlags\",\"uart\":%s,\"ble\":%s}",
             uart ? "true" : "false", ble ? "true" : "false");
    LinkClient::SendCommand(buf);
}

void DspProxy::RefreshMidiFlags() {
    LinkClient::SendCommand("{\"cmd\":\"/link/midiFlags\"}");
}

bool DspProxy::IsOnline() { return LinkClient::IsOnline(); }

        // Entry point for every complete frame the link client receives
        // (pushes, stream, fsList replies). Small docs only: big JSON blobs
        // arrive as escaped strings and are stored without DOM parsing.
        void DspProxy::OnLinkFrame(const char *payload, size_t len) {
            // The cmd header is extracted WITHOUT a DOM: the params push
            // carries tens of KB in one string member and must never be
            // DOM-parsed whole (see the pushParams handler). The producer's
            // escaped content cannot contain a raw "cmd":" sequence, so the
            // strstr hit is unambiguous.
            char cmd[40] = {0};
            {
                const char *cp = strstr(payload, "\"cmd\":\"");
                if (cp == nullptr) return;
                cp += 7;
                size_t ci = 0;
                while (cp[ci] != '\0' && cp[ci] != '"' && ci < sizeof(cmd) - 1) {
                    cmd[ci] = cp[ci];
                    ci++;
                }
            }
            Document d;
            // The params frame is handled without any DOM (its handler
            // never touches d) — skip the parse entirely for it.
            if (strcmp(cmd, kMsgPushParams) != 0) {
                d.Parse(payload, len);
                if (d.HasParseError() || !d.IsObject()) return;
            }

            if (strcmp(cmd, kMsgPushPluginList) == 0) {
                if (d.HasMember("list") && d["list"].IsArray()) {
                    StringBuffer sb;
                    Writer<StringBuffer> w(sb);
                    d["list"].Accept(w);
                    cachePluginList = sb.GetString();
                }
            } else if (strcmp(cmd, kMsgPushParams) == 0) {
                // NO DOM for the frame: whole-plugin params once faulted
                // the DOM pool (malloc returned null next to NimBLE) and
                // the assert-less memcpy store-faulted the panel into a
                // bootloop. The producer sends flat, hint-free slices
                // (≤~2 KB): scan out "part"/"last"/"params", unescape into
                // a small static buffer, parse that, fill the table.
                int ch = 0;
                const char *chPos = strstr(payload, "\"ch\":");
                if (chPos) ch = atoi(chPos + 5);
                if (ch < 0 || ch > 1) return;
                const char *partPos = strstr(payload, "\"part\":");
                int part = partPos ? atoi(partPos + 7) : 0;
                const char *lastPos = strstr(payload, "\"last\":");
                bool last = lastPos ? atoi(lastPos + 7) != 0 : true;

                static char sliceBuf[4096];
                const char *mark = strstr(payload, "\"params\":\"");
                if (mark == nullptr) return;
                mark += 10; // into the escaped slice text
                size_t out = 0;
                const char *p = mark;
                while (*p != '\0' && out < sizeof(sliceBuf) - 1) {
                    if (*p == '\\' && *(p + 1) != '\0') {
                        p++;
                        char c = *p;
                        if (c == 'u' && *(p + 1) == '0' && *(p + 2) == '0') {
                            // \u00XX — the producer's only \u form (control chars)
                            static const char *hex = "0123456789abcdef";
                            char h = *(p + 3);
                            char l = *(p + 4);
                            if (h >= 'A' && h <= 'F') h += 32;
                            if (l >= 'A' && l <= 'F') l += 32;
                            const char *hi = strchr(hex, h);
                            const char *lo = strchr(hex, l);
                            if (hi && lo) sliceBuf[out++] = (char)((hi - hex) << 4 | (lo - hex));
                            p += 4;
                        } else {
                            switch (c) {
                                case 'n': sliceBuf[out++] = '\n'; break;
                                case 't': sliceBuf[out++] = '\t'; break;
                                case 'r': sliceBuf[out++] = '\r'; break;
                                case 'b': sliceBuf[out++] = '\b'; break;
                                case 'f': sliceBuf[out++] = '\f'; break;
                                default: sliceBuf[out++] = c; break; // \" \\ \/
                            }
                        }
                        p++;
                    } else if (*p == '"') {
                        break; // unescaped closing quote
                    } else {
                        sliceBuf[out++] = *p++;
                    }
                }
                sliceBuf[out] = '\0';

                if (part == 0) {
                    paramTableCount[ch] = 0;
                }
                Document sd;
                if (sd.ParseInsitu(sliceBuf).HasParseError() || !sd.IsArray()) return;
                for (auto &v : sd.GetArray()) {
                    if (!v.IsObject()) continue;
                    if (paramTableCount[ch] >= DspProxy::DSP_MAX_PARAMS) break;
                    DspProxy::DspParamEntry &e = paramTable[ch][paramTableCount[ch]];
                    memset(&e, 0, sizeof(e));
                    e.min = 0;
                    e.max = 1;
                    e.current = 0;
                    e.cv = -1;
                    e.trig = -1;
                    if (v.HasMember("id") && v["id"].IsString())
                        snprintf(e.id, sizeof(e.id), "%s", v["id"].GetString());
                    if (v.HasMember("name") && v["name"].IsString())
                        snprintf(e.name, sizeof(e.name), "%s", v["name"].GetString());
                    if (v.HasMember("type") && v["type"].IsString())
                        snprintf(e.type, sizeof(e.type), "%s", v["type"].GetString());
                    if (v.HasMember("min") && v["min"].IsInt()) e.min = v["min"].GetInt();
                    if (v.HasMember("max") && v["max"].IsInt()) e.max = v["max"].GetInt();
                    if (v.HasMember("current") && v["current"].IsInt()) e.current = v["current"].GetInt();
                    if (v.HasMember("cv") && v["cv"].IsInt()) { e.cv = v["cv"].GetInt(); e.hasCv = true; }
                    if (v.HasMember("trig") && v["trig"].IsInt()) { e.trig = v["trig"].GetInt(); e.hasTrig = true; }
                    e.isGroup = (v.HasMember("type") && strcmp(v["type"].GetString(), "group") == 0);
                    paramTableCount[ch]++;
                }
                if (last) paramTableVersion++;
            } else if (strcmp(cmd, kMsgPushConfig) == 0) {
                if (d.HasMember("config") && d["config"].IsString()) {
                    cacheConfig = d["config"].GetString();
                }
            } else if (strcmp(cmd, kMsgPushPresets) == 0) {
                int ch = d.HasMember("ch") && d["ch"].IsInt() ? d["ch"].GetInt() : 0;
                if (ch >= 0 && ch <= 1 && d.HasMember("presets") && d["presets"].IsString()) {
                    cachePresets[ch] = d["presets"].GetString();
                }
            } else if (strcmp(cmd, kMsgPushFavorites) == 0) {
                if (d.HasMember("favs") && d["favs"].IsString()) {
                    cacheFavorites = d["favs"].GetString();
                }
            } else if (strcmp(cmd, kMsgPushActivePlugin) == 0) {
                if (d.HasMember("id0") && d["id0"].IsString()) activeId[0] = d["id0"].GetString();
                if (d.HasMember("id1") && d["id1"].IsString()) activeId[1] = d["id1"].GetString();
                if (d.HasMember("st0") && d["st0"].IsInt()) activeStereo[0] = d["st0"].GetInt() != 0;
                if (d.HasMember("st1") && d["st1"].IsInt()) activeStereo[1] = d["st1"].GetInt() != 0;
            } else if (strcmp(cmd, kMsgCmdMidiFlags) == 0) {
                // Authoritative flag state (set reply or get reply).
                if (d.HasMember("uart") && d["uart"].IsBool())
                    cacheMidiUart = d["uart"].GetBool();
                if (d.HasMember("ble") && d["ble"].IsBool())
                    cacheMidiBle = d["ble"].GetBool();
            } else if (strcmp(cmd, kMsgMidiOut) == 0) {
                // P4: DSP → TRS OUT jack (raw bytes as an array).
                if (d.HasMember("d") && d["d"].IsArray()) {
                    const Value &arr = d["d"];
                    uint8_t bytes[128];
                    size_t n = 0;
                    for (size_t i = 0; i < arr.Size() && n < sizeof(bytes); i++)
                        if (arr[i].IsInt())
                            bytes[n++] = (uint8_t) (arr[i].GetInt() & 0xFF);
                    MidiLink::OnMidiOut(bytes, n);
                }
            } else if (strcmp(cmd, kMsgPushMarbles) == 0) {
                parsePushMarbles(d);
            } else if (strcmp(cmd, kMsgPushIoCaps) == 0) {
                if (d.HasMember("sd") && d["sd"].IsInt()) sdMounted = d["sd"].GetInt() != 0;
            } else if (strcmp(cmd, kMsgStream) == 0) {
                if (d.HasMember("vu") && d["vu"].IsArray()) {
                    const Value &a = d["vu"];
                    for (SizeType i = 0; i < a.Size() && i < 4; i++) {
                        if (a[i].IsUint()) vu[i] = a[i].GetUint();
                    }
                }
                if (d.HasMember("mb") && d["mb"].IsUint()) snapshot = d["mb"].GetUint();
                if (d.HasMember("ov") && d["ov"].IsInt()) cpuOverload = d["ov"].GetInt() != 0;
                if (d.HasMember("sd") && d["sd"].IsObject()) {
                    const Value &s = d["sd"];
                    if (s.HasMember("p") && s["p"].IsInt()) sdPlaying = s["p"].GetInt() != 0;
                    if (s.HasMember("r") && s["r"].IsInt()) sdRecording = s["r"].GetInt() != 0;
                    if (s.HasMember("pos") && s["pos"].IsUint()) sdPos = s["pos"].GetUint();
                    if (s.HasMember("tot") && s["tot"].IsUint()) sdTot = s["tot"].GetUint();
                }
            } else if (strcmp(cmd, kMsgFsList) == 0) {
                // Complete a pending RequestFsList; hand over the unescaped
                // entries array text.
                uint32_t seq = d.HasMember("seq") && d["seq"].IsUint() ? d["seq"].GetUint() : 0;
                const char *entries = d.HasMember("entries") && d["entries"].IsString()
                                          ? d["entries"].GetString() : "[]";
                extern void LinkClient_NotifyFsList(uint32_t seq, const char *entriesJson);
                LinkClient_NotifyFsList(seq, entries);
            } else if (strcmp(cmd, kMsgError) == 0) {
                if (d.HasMember("what") && d["what"].IsString()) {
                    ESP_LOGW(TAG, "DSP error: %s", d["what"].GetString());
                }
            } else if (strncmp(cmd, "/debug/", 7) == 0) {
                // A reply to a bench /debug command forwarded through
                // LinkClient::RequestDebug. Hand over the raw frame text;
                // dropped silently when nothing is pending (these can also
                // arrive unsolicited).
                extern void LinkClient_NotifyDebug(const char *payload, size_t len);
                LinkClient_NotifyDebug(payload, len);
            }
        }

        const char *DspProxy::GetCStrJSONSoundProcessors() { return cachePluginList.c_str(); }

        int DspProxy::GetParamEntryCount(int chan) {
            if (chan < 0 || chan > 1) return 0;
            return paramTableCount[chan];
        }

        uint32_t DspProxy::GetParamTableVersion() { return paramTableVersion; }

        const DspProxy::DspParamEntry *DspProxy::GetParamEntry(int chan, int idx) {
            if (chan < 0 || chan > 1 || idx < 0 || idx >= paramTableCount[chan])
                return nullptr;
            return &paramTable[chan][idx];
        }

        // NetRest convenience: serialize the table as the classic
        // {"params":[...]} document. Rare path (HTTP only) — the transient
        // StringBuffer lives in PSRAM-backed rapidjson heap.
        const char *DspProxy::GetCStrJSONActivePluginParams(int chan) {
            static string out;
            if (chan < 0 || chan > 1) return "{\"params\":[]}";
            out = "{\"params\":[";
            for (int i = 0; i < paramTableCount[chan]; i++) {
                const DspParamEntry &e = paramTable[chan][i];
                if (i) out += ",";
                out += "{\"id\":\"";
                out += e.id;
                out += "\",\"name\":\"";
                out += e.name;
                out += "\",\"type\":\"";
                out += e.type;
                out += "\",\"min\":" + to_string(e.min);
                out += ",\"max\":" + to_string(e.max);
                out += ",\"current\":" + to_string(e.current);
                if (e.hasCv) out += ",\"cv\":" + to_string(e.cv);
                if (e.hasTrig) out += ",\"trig\":" + to_string(e.trig);
                out += "}";
            }
            out += "]}";
            return out.c_str();
        }

        const char *DspProxy::GetCStrJSONGetPresets(int chan) {
            if (chan < 0 || chan > 1) return "{\"presets\":[]}";
            return cachePresets[chan].c_str();
        }

        const char *DspProxy::GetCStrJSONConfiguration() { return cacheConfig.c_str(); }

        void DspProxy::SetConfigurationFromJSON(const std::string &json) {
            // Cache optimistically; the DSP's config-commit hash diff will
            // confirm (or correct) with a push.
            cacheConfig = json;
            StringBuffer sb;
            Writer<StringBuffer> w(sb);
            w.StartObject();
            w.Key("cmd");
            w.String("/link/configSet");
            w.Key("config");
            w.String(json.c_str());
            w.EndObject();
            sendCmd(sb.GetString());
        }

        std::string DspProxy::GetStringID(int chan) {
            if (chan < 0 || chan > 1) return "";
            return activeId[chan];
        }

        bool DspProxy::IsPluginStereo(const std::string &id) {
            // Plugin list carries isStereo; fall back to the active flags.
            Document d;
            d.Parse(cachePluginList.c_str());
            if (d.IsArray()) {
                for (SizeType i = 0; i < d.Size(); i++) {
                    const Value &p = d[i];
                    if (p.IsObject() && p.HasMember("id") && p["id"].IsString() &&
                        id == p["id"].GetString()) {
                        return p.HasMember("isStereo") && p["isStereo"].IsBool() &&
                               p["isStereo"].GetBool();
                    }
                }
            }
            return id == activeId[0] ? activeStereo[0] : activeStereo[1];
        }

        void DspProxy::SetSoundProcessorChannel(int chan, const std::string &id) {
            if (chan < 0 || chan > 1) return;
            activeId[chan] = id;
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "{\"cmd\":\"/debug/setActivePlugin\",\"ch\":%d,\"id\":\"%s\"}",
                     chan, id.c_str());
            sendCmd(buf);
        }

        void DspProxy::SetChannelParamValue(int chan, const std::string &id,
                                            const std::string &key, int value) {
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "{\"cmd\":\"/link/paramSet\",\"ch\":%d,\"id\":\"%s\",\"key\":\"%s\",\"value\":%d}",
                     chan, id.c_str(), key.c_str(), value);
            sendCmd(buf);
        }

        void DspProxy::ChannelSavePreset(int chan, const std::string &name, int number) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "{\"cmd\":\"/link/presetSave\",\"ch\":%d,\"name\":\"%s\",\"number\":%d}",
                     chan, name.c_str(), number);
            sendCmd(buf);
        }

        void DspProxy::ChannelLoadPreset(int chan, int number) {
            char buf[96];
            snprintf(buf, sizeof(buf),
                     "{\"cmd\":\"/link/presetLoad\",\"ch\":%d,\"number\":%d}", chan, number);
            sendCmd(buf);
        }

        void DspProxy::SetMeteringEnabled(bool on) {
            // The A1S audio task skips its peak pass entirely while metering
            // is off, so the MIX page's tick arms it through a link command.
            // Change-tracked: the tick fires at 25 Hz, the command must go
            // out once. Re-arm while offline so the state is re-sent after a
            // link drop or DSP reboot (the A1S boots with metering off).
            static bool sentOn = false;
            if (on) {
                if (sentOn && IsOnline()) return;
                if (!IsOnline()) { sentOn = false; return; }
            } else if (!sentOn) {
                return;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"cmd\":\"/link/metering\",\"on\":%d}", on ? 1 : 0);
            sendCmd(buf);
            sentOn = on;
        }

        bool DspProxy::GetCPUOverload() { return cpuOverload; }

        uint32_t DspProxy::GetVUPeak(int channel) {
            if (channel < 0 || channel > 3) return 0;
            return vu[channel];
        }

        void DspProxy::SetInputGain(int gain) {
            // Live preview only, no config commit (the pages commit the
            // persisted value through SetConfigurationFromJSON at edit end —
            // same split as the A1S-local MIX page).
            char buf[80];
            snprintf(buf, sizeof(buf), "{\"cmd\":\"/link/codecGain\",\"g\":%d}",
                     clampI(gain, 0, 8));
            sendCmd(buf);
        }

        void DspProxy::SetOutputLevels(int left, int right) {
            // Live preview only; see SetInputGain.
            char buf[112];
            snprintf(buf, sizeof(buf), "{\"cmd\":\"/link/codecLevels\",\"l\":%d,\"r\":%d}",
                     clampI(left, 0, MAX_OUTPUT_LEVEL), clampI(right, 0, MAX_OUTPUT_LEVEL));
            sendCmd(buf);
        }

        void DspProxy::SdStartPlayback(const char *path) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "{\"cmd\":\"/link/sdStartPlayback\",\"path\":\"%s\"}", path);
            sendCmd(buf);
        }

        void DspProxy::SdStartRecording(const char *path) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "{\"cmd\":\"/link/sdStartRecording\",\"path\":\"%s\"}", path);
            sendCmd(buf);
        }

        void DspProxy::SdStop() { sendCmd("{\"cmd\":\"/link/sdStop\"}"); }

        bool DspProxy::SdIsPlaying() { return sdPlaying; }

        bool DspProxy::SdIsRecording() { return sdRecording; }

        uint32_t DspProxy::SdGetTotalFrames() { return sdTot; }

        uint32_t DspProxy::SdGetPositionFrames() { return sdPos; }

        std::string DspProxy::GetAllFavorites() { return cacheFavorites; }

        int32_t DspProxy::GetActiveFav() { return activeFav; }

        void DspProxy::ActivateFavorite(int id) {
            activeFav = id;
            char buf[80];
            snprintf(buf, sizeof(buf), "{\"cmd\":\"/link/favoriteActivate\",\"id\":%d}", id);
            sendCmd(buf);
        }

        bool DspProxy::IsSDMounted() { return sdMounted; }

        int DspProxy::ListSdDir(const char *path, char names[][32], bool *isDir, int max) {
            string entries;
            if (!LinkClient::RequestFsList(path, entries)) return 0;
            Document d;
            d.Parse(entries.c_str());
            if (!d.IsArray()) return 0;
            int count = 0;
            for (SizeType i = 0; i < d.Size() && count < max; i++) {
                const Value &e = d[i];
                if (!e.IsArray() || e.Size() < 2 || !e[0].IsString()) continue;
                snprintf(names[count], 32, "%s", e[0].GetString());
                isDir[count] = e[1].IsInt() && e[1].GetInt() != 0;
                count++;
            }
            return count;
        }

        void DspProxy::RequestReset() { sendMarblesSet("rst", 1); }

        uint32_t DspProxy::GetOutputSnapshot() { return snapshot; }

        int DspProxy::GetTModel() { return mb.tModel; }
        void DspProxy::SetTModel(int v) { mb.tModel = clampI(v, 0, 6); sendMarblesSet("tm", v); }
        int DspProxy::GetTRange() { return mb.tRange; }
        void DspProxy::SetTRange(int v) { mb.tRange = clampI(v, 0, 2); sendMarblesSet("tr", v); }
        float DspProxy::GetTRate() { return mb.tRate; }
        void DspProxy::SetTRate(float v) { mb.tRate = clampF(v, -60.0f, 60.0f); sendMarblesSet("trate", v); }
        float DspProxy::GetTBias() { return mb.tBias; }
        void DspProxy::SetTBias(float v) { mb.tBias = clampF(v, 0, 1); sendMarblesSet("tb", v); }
        float DspProxy::GetTJitter() { return mb.tJitter; }
        void DspProxy::SetTJitter(float v) { mb.tJitter = clampF(v, 0, 1); sendMarblesSet("tj", v); }
        int DspProxy::GetTPwMean() { return mb.tpwMean; }
        void DspProxy::SetTPwMean(int v) { mb.tpwMean = clampI(v, 0, 255); sendMarblesSet("tpwm", v); }
        int DspProxy::GetTPwStd() { return mb.tpwStd; }
        void DspProxy::SetTPwStd(int v) { mb.tpwStd = clampI(v, 0, 255); sendMarblesSet("tpws", v); }
        int DspProxy::GetTDejaVu() { return mb.tDejaVu; }
        void DspProxy::SetTDejaVu(int v) { mb.tDejaVu = clampI(v, 0, 2); sendMarblesSet("tdv", v); }
        bool DspProxy::GetTExternal() { return mb.tExternal; }
        void DspProxy::SetTExternal(bool v) { mb.tExternal = v; sendMarblesSet("te", v ? 1 : 0); }
        int DspProxy::GetXYClockSource() { return mb.xyClockSource; }
        void DspProxy::SetXYClockSource(int v) { mb.xyClockSource = clampI(v, 0, 4); sendMarblesSet("txy", v); }
        int DspProxy::GetXScale() { return mb.xScale; }
        void DspProxy::SetXScale(int v) { mb.xScale = clampI(v, 0, 5); sendMarblesSet("xsc", v); }
        int DspProxy::GetXRange() { return mb.xRange; }
        void DspProxy::SetXRange(int v) { mb.xRange = clampI(v, 0, 2); sendMarblesSet("xrg", v); }
        int DspProxy::GetXControl() { return mb.xControl; }
        void DspProxy::SetXControl(int v) { mb.xControl = clampI(v, 0, 2); sendMarblesSet("xc", v); }
        bool DspProxy::GetXRegister() { return mb.xRegister; }
        void DspProxy::SetXRegister(bool v) { mb.xRegister = v; sendMarblesSet("xr", v ? 1 : 0); }
        float DspProxy::GetXRegisterValue() { return mb.xRegisterValue; }
        void DspProxy::SetXRegisterValue(float v) { mb.xRegisterValue = clampF(v, 0, 1); sendMarblesSet("xrv", v); }
        float DspProxy::GetXSpread() { return mb.xSpread; }
        void DspProxy::SetXSpread(float v) { mb.xSpread = clampF(v, 0, 1); sendMarblesSet("xsp", v); }
        float DspProxy::GetXBias() { return mb.xBias; }
        void DspProxy::SetXBias(float v) { mb.xBias = clampF(v, 0, 1); sendMarblesSet("xb", v); }
        float DspProxy::GetXSteps() { return mb.xSteps; }
        void DspProxy::SetXSteps(float v) { mb.xSteps = clampF(v, 0, 1); sendMarblesSet("xst", v); }
        int DspProxy::GetXDejaVu() { return mb.xDejaVu; }
        void DspProxy::SetXDejaVu(int v) { mb.xDejaVu = clampI(v, 0, 2); sendMarblesSet("xdv", v); }
        int DspProxy::GetYRange() { return mb.yRange; }
        void DspProxy::SetYRange(int v) { mb.yRange = clampI(v, 0, 2); sendMarblesSet("yrg", v); }
        float DspProxy::GetYSpread() { return mb.ySpread; }
        void DspProxy::SetYSpread(float v) { mb.ySpread = clampF(v, 0, 1); sendMarblesSet("ysp", v); }
        float DspProxy::GetYBias() { return mb.yBias; }
        void DspProxy::SetYBias(float v) { mb.yBias = clampF(v, 0, 1); sendMarblesSet("yb", v); }
        float DspProxy::GetYSteps() { return mb.ySteps; }
        void DspProxy::SetYSteps(float v) { mb.ySteps = clampF(v, 0, 1); sendMarblesSet("yst", v); }
        int DspProxy::GetYDivider() { return mb.yDivider; }
        void DspProxy::SetYDivider(int v) { mb.yDivider = clampI(v, 0, 11); sendMarblesSet("ydiv", v); }
        int DspProxy::GetYDejaVu() { return mb.yDejaVu; }
        void DspProxy::SetYDejaVu(int v) { mb.yDejaVu = clampI(v, 0, 2); sendMarblesSet("yd", v); }
        float DspProxy::GetDejaVuAmount() { return mb.dejaVuAmount; }
        void DspProxy::SetDejaVuAmount(float v) { mb.dejaVuAmount = clampF(v, 0, 1); sendMarblesSet("dva", v); }
        int DspProxy::GetDejaVuLength() { return mb.dejaVuLength; }
        void DspProxy::SetDejaVuLength(int v) { mb.dejaVuLength = clampI(v, 1, 16); sendMarblesSet("dvl", v); }
        int DspProxy::GetTSlot(int i) { return (i >= 0 && i < 3) ? mb.tSlot[i] : -1; }
        void DspProxy::SetTSlot(int i, int slot) {
            if (i < 0 || i > 2) return;
            mb.tSlot[i] = clampI(slot, -1, kNTrigs - 1);
            char k[8];
            snprintf(k, sizeof(k), "ts%d", i);
            sendMarblesSet(k, slot);
        }
        int DspProxy::GetXSlot(int i) { return (i >= 0 && i < 3) ? mb.xSlot[i] : -1; }
        void DspProxy::SetXSlot(int i, int slot) {
            if (i < 0 || i > 2) return;
            mb.xSlot[i] = clampI(slot, -1, kNCvs - 1);
            char k[8];
            snprintf(k, sizeof(k), "xs%d", i);
            sendMarblesSet(k, slot);
        }
        int DspProxy::GetYSlot() { return mb.ySlot; }
        void DspProxy::SetYSlot(int slot) { mb.ySlot = clampI(slot, -1, kNCvs - 1); sendMarblesSet("ys", slot); }

        void DspProxy::StartLearn() { mb.learning = true; sendMarblesSet("learn", 1); }

        void DspProxy::StopLearn() { mb.learning = false; sendMarblesSet("learn", 0); }

        bool DspProxy::IsLearning() { return mb.learning; }

        int DspProxy::GetLastLearnedSlot() { return mb.lastLearned; }

        int DspProxy::GetDynamicSlotCC(int slot_idx) {
            return (slot_idx >= 0 && slot_idx < 8) ? mb.ccSlot[slot_idx] : -1;
        }

        void DspProxy::SetDynamicSlotCC(int slot_idx, int cc) {
            if (slot_idx < 0 || slot_idx > 7) return;
            mb.ccSlot[slot_idx] = clampI(cc, -1, 127);
            char k[8];
            snprintf(k, sizeof(k), "cc%d", slot_idx);
            sendMarblesSet(k, cc);
        }

        void DspProxy::SetDynamicSlotChan(int slot_idx, int chan) {
            if (slot_idx < 0 || slot_idx > 7) return;
            mb.ccChan[slot_idx] = clampI(chan, 0, 15);
            char k[8];
            snprintf(k, sizeof(k), "cch%d", slot_idx);
            sendMarblesSet(k, chan);
        }

        int DspProxy::GetDynamicSlotChan(int slot_idx) {
            return (slot_idx >= 0 && slot_idx < 8) ? mb.ccChan[slot_idx] : 0;
        }

        void DspProxy::SaveConfig() { sendMarblesSet("_save", 0); }
    }
}
