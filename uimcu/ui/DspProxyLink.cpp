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
#include <cstdlib>
#include <cstring>
#include <atomic>
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
            // Per-value text labels ("texts" arrays in the plugin spec,
            // e.g. the 24 TBDaits / 98 TBPalette engine names). Kept out of
            // DspParamEntry (the 80 B stride x 320-entry table stays untouched):
            // a shared NUL-terminated pool per channel plus per-entry offset/count
            // parallel arrays; count 0 = numeric display fallback.
            char labelPool[2][2560];
            uint16_t labelUsed[2] = {0, 0};
            uint16_t labelOff[2][DspProxy::DSP_MAX_PARAMS];
            uint8_t labelCount[2][DspProxy::DSP_MAX_PARAMS];
            // Bumped on every presets push; the PRESETS list watches it so
            // a store/overwrite done from the panel shows up without
            // leaving the list (the DSP re-pushes on its presets hash).
            uint32_t presetsVersion = 0;
            string cacheConfig = "{}";
            string cachePresets[2] = {"{\"presets\":[]}", "{\"presets\":[]}"};
            string cacheFavorites = "[]";
            string activeId[2];
            bool activeStereo[2] = {false, false};

            // Plugin-load overlay state: set by SetSoundProcessorChannel,
            // cleared by the loaded plugin's params push (a completed
            // paramTableVersion bump), by a refusal (pushActivePlugin
            // naming a different plugin) or by the 5 s timeout.
            uint32_t loadPendingStartMs = 0;
            int loadPendingCh = 0;
            char loadPendingId[24] = "";

            // OLED brightness from the latest config push; -1 = consumed.
            // Applied by the menu task (display SPI has no cross-task lock).
            std::atomic<int> pendingOledContrast{-1};

            uint32_t vu[4] = {0, 0, 0, 0};
            uint32_t snapshot = 0;
            bool cpuOverload = false;
            bool sdPlaying = false, sdRecording = false;
            uint32_t sdPos = 0, sdTot = 0;
            uint32_t sdLevel = 0;
            bool sdMounted = false;
            bool sdCardPresent = false;
            int sdRoute = 0; // 0 = main out, 1 = plugin input (last set)
            int sdGain = 100;
            int32_t activeFav = -1;

            // Mirrors the A1S pushMarbles key table (LinkEndpoint.cpp).
            struct MarblesState {
                int tModel = 0, tRange = 1, xyClockSource = 0;
                int xScale = 0, xRange = 1, xControl = 0, xDejaVu = 0;
                int yRange = 1, yDivider = 8;
                int tDejaVu = 0, tpwMean = 128, tpwStd = 64;
                int dejaVuLength = 4;
                float bpm = 120.0f;
                float tRate = 0, tBias = 0.5f, tJitter = 0.1f;
                float xSpread = 0.5f, xBias = 0.5f, xSteps = 0.5f;
                float ySpread = 0.5f, yBias = 0.5f, ySteps = 0.5f;
                float dejaVuAmount = 0.5f;
                bool tExternal = false;
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
                else if (!strcmp(key, "bpm")) mb.bpm = clampF((float)v, 20.0f, 480.0f);
                else if (!strcmp(key, "trate")) mb.tRate = clampF((float)v, -60.0f, 60.0f);
                else if (!strcmp(key, "tb")) mb.tBias = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "tj")) mb.tJitter = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "tpwm")) mb.tpwMean = clampI(iv, 0, 255);
                else if (!strcmp(key, "tpws")) mb.tpwStd = clampI(iv, 0, 255);
                else if (!strcmp(key, "tdv")) mb.tDejaVu = clampI(iv, 0, 2);
                else if (!strcmp(key, "xsc")) mb.xScale = clampI(iv, 0, 7);
                else if (!strcmp(key, "xrg")) mb.xRange = clampI(iv, 0, 2);
                else if (!strcmp(key, "xc")) mb.xControl = clampI(iv, 0, 2);
                else if (!strcmp(key, "xsp")) mb.xSpread = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "xb")) mb.xBias = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "xst")) mb.xSteps = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "xdv")) mb.xDejaVu = clampI(iv, 0, 2);
                else if (!strcmp(key, "yrg")) mb.yRange = clampI(iv, 0, 2);
                else if (!strcmp(key, "ysp")) mb.ySpread = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "yb")) mb.yBias = clampF((float)v, 0.0f, 1.0f);
                else if (!strcmp(key, "yst")) mb.ySteps = clampF((float)v, 0.0f, 1.0f);
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
                mb.bpm = gf("bpm", mb.bpm);
                mb.tRate = gf("trate", mb.tRate);
                mb.tBias = gf("tb", mb.tBias);
                mb.tJitter = gf("tj", mb.tJitter);
                mb.tpwMean = gi("tpwm", mb.tpwMean);
                mb.tpwStd = gi("tpws", mb.tpwStd);
                mb.tDejaVu = gi("tdv", mb.tDejaVu);
                mb.xScale = gi("xsc", mb.xScale);
                mb.xRange = gi("xrg", mb.xRange);
                mb.xControl = gi("xc", mb.xControl);
                mb.xSpread = gf("xsp", mb.xSpread);
                mb.xBias = gf("xb", mb.xBias);
                mb.xSteps = gf("xst", mb.xSteps);
                mb.xDejaVu = gi("xdv", mb.xDejaVu);
                mb.yRange = gi("yrg", mb.yRange);
                mb.ySpread = gf("ysp", mb.ySpread);
                mb.yBias = gf("yb", mb.yBias);
                mb.ySteps = gf("yst", mb.ySteps);
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
            // never touches d) — skip the parse entirely for it. Same for
            // the tiny single-parameter value push.
            if (strcmp(cmd, kMsgPushParams) != 0 &&
                strcmp(cmd, kMsgPushParamValue) != 0) {
                d.Parse(payload, len);
                if (d.HasParseError() || !d.IsObject()) return;
            }

            if (strcmp(cmd, kMsgPushParamValue) == 0) {
                // Incremental sync: one parameter value changed on the DSP.
                // Update the cached table entry in place; refresh the page
                // view at most ~5x/s (fast turns would otherwise re-parse
                // the whole table per detent). Values for the row the user
                // is editing are preserved by the page's edit-row guard.
                // "k" carries the edited key — "current" (default for frames
                // from older DSP builds), "cv" or "trig" (mapping edits).
                // Applying a cv/trig mapping to e.current would clobber a
                // bool param's value with a slot number.
                int ch = -1, val = 0, key = 0;  // key: 0=current 1=cv 2=trig
                const char *id = nullptr;
                {   // tiny manual extraction — the frame is machine-made
                    const char *p = strstr(payload, "\"ch\":");
                    if (p) ch = atoi(p + 5);
                    p = strstr(payload, "\"id\":\"");
                    if (p) id = p + 6;
                    p = strstr(payload, "\"k\":\"");
                    if (p) {
                        p += 5;
                        if (!strncmp(p, "cv", 2)) key = 1;
                        else if (!strncmp(p, "trig", 4)) key = 2;
                    }
                    p = strstr(payload, "\"current\":");
                    if (p) val = atoi(p + 10);
                }
                if (ch >= 0 && ch <= 1 && id && id[0]) {
                    const char *qe = strchr(id, '"');
                    size_t idlen = qe ? (size_t)(qe - id) : strlen(id);
                    for (int i = 0; i < paramTableCount[ch]; i++) {
                        DspProxy::DspParamEntry &e = paramTable[ch][i];
                        if (strncmp(e.id, id, idlen) == 0 && e.id[idlen] == '\0') {
                            if (key == 1) e.cv = val;
                            else if (key == 2) e.trig = val;
                            else e.current = val;
                            uint32_t nowMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                            static uint32_t lastBumpMs = 0;
                            if (nowMs - lastBumpMs >= 200 || lastBumpMs == 0) {
                                lastBumpMs = nowMs;
                                paramTableVersion++;
                            }
                            break;
                        }
                    }
                }
                return;
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
                    labelUsed[ch] = 0;
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
                    // Per-value labels: value val shows texts[val - min]
                    // when the spec carries the array. Count 0 = numeric.
                    labelCount[ch][paramTableCount[ch]] = 0;
                    if (v.HasMember("texts") && v["texts"].IsArray()) {
                        uint16_t off = labelUsed[ch];
                        uint8_t n = 0;
                        for (auto &t : v["texts"].GetArray()) {
                            if (n >= 128) break;
                            if (!t.IsString()) continue;
                            size_t room = sizeof(labelPool[ch]) - labelUsed[ch];
                            if (room < 2) break;
                            size_t len = strnlen(t.GetString(), 20);
                            if (len >= room) len = room - 1;
                            memcpy(&labelPool[ch][labelUsed[ch]], t.GetString(), len);
                            labelPool[ch][labelUsed[ch] + len] = '\0';
                            labelUsed[ch] += len + 1;
                            n++;
                        }
                        if (n > 0) {
                            labelOff[ch][paramTableCount[ch]] = off;
                            labelCount[ch][paramTableCount[ch]] = n;
                        }
                    }
                    e.isGroup = (v.HasMember("type") && strcmp(v["type"].GetString(), "group") == 0);
                    paramTableCount[ch]++;
                }
                if (last) {
                    paramTableVersion++;
                    // A completed params table ends a pending plugin load:
                    // the new plugin's values are on the panel, the loading
                    // overlay can go.
                    loadPendingStartMs = 0;
                }
            } else if (strcmp(cmd, kMsgPushConfig) == 0) {
                if (d.HasMember("config") && d["config"].IsString()) {
                    cacheConfig = d["config"].GetString();
                    // Hand the configured OLED brightness to the menu task
                    // (no DOM: the config can be sizeable and this runs on
                    // the link task; the value is an escaped string).
                    const char *p = strstr(cacheConfig.c_str(), "\"oled_brightness\"");
                    if (p) {
                        const char *q = strchr(p, ':');
                        while (q && *q && (*q < '0' || *q > '9')) q++;
                        if (q && *q)
                            pendingOledContrast.store(atoi(q), std::memory_order_relaxed);
                    }
                }
            } else if (strcmp(cmd, kMsgPushPresets) == 0) {
                int ch = d.HasMember("ch") && d["ch"].IsInt() ? d["ch"].GetInt() : 0;
                if (ch >= 0 && ch <= 1 && d.HasMember("presets") && d["presets"].IsString()) {
                    cachePresets[ch] = d["presets"].GetString();
                    presetsVersion++;
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
                // A pending load whose plugin did NOT become active was
                // refused — nothing else will arrive for it, lift the
                // loading overlay. On success the push names the requested
                // plugin and the overlay stays up until its params table
                // completes.
                if (loadPendingStartMs != 0) {
                    const string &now = activeId[loadPendingCh];
                    if (now != loadPendingId) loadPendingStartMs = 0;
                }
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
                if (d.HasMember("cd") && d["cd"].IsInt()) sdCardPresent = d["cd"].GetInt() != 0;
            } else if (strcmp(cmd, kMsgStream) == 0) {
                if (d.HasMember("vu") && d["vu"].IsArray()) {
                    const Value &a = d["vu"];
                    for (SizeType i = 0; i < a.Size() && i < 4; i++) {
                        if (a[i].IsUint()) vu[i] = a[i].GetUint();
                    }
                }
                if (d.HasMember("mb") && d["mb"].IsUint()) snapshot = d["mb"].GetUint();
                if (d.HasMember("ov") && d["ov"].IsInt()) cpuOverload = d["ov"].GetInt() != 0;
                if (d.HasMember("tl") && d["tl"].IsUint()) sdLevel = d["tl"].GetUint();
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

        uint32_t DspProxy::GetPresetsVersion() { return presetsVersion; }

        // Loading overlay: auto-expires after 5 s so a wedged load (no
        // push of either kind) can never hold the panel hostage.
        bool DspProxy::IsLoadPending() {
            if (loadPendingStartMs == 0) return false;
            uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            if (now - loadPendingStartMs > 5000) {
                loadPendingStartMs = 0;
                return false;
            }
            return true;
        }

        uint32_t DspProxy::LoadPendingStartMs() { return loadPendingStartMs; }

        int DspProxy::TakePendingOledContrast() {
            return pendingOledContrast.exchange(-1, std::memory_order_relaxed);
        }

        const DspProxy::DspParamEntry *DspProxy::GetParamEntry(int chan, int idx) {
            if (chan < 0 || chan > 1 || idx < 0 || idx >= paramTableCount[chan])
                return nullptr;
            return &paramTable[chan][idx];
        }

        const char *DspProxy::GetParamEntryLabel(int chan, int idx, int value) {
            if (chan < 0 || chan > 1 || idx < 0 || idx >= paramTableCount[chan])
                return nullptr;
            uint8_t n = labelCount[chan][idx];
            if (n == 0) return nullptr;
            int rel = value - paramTable[chan][idx].min;
            if (rel < 0 || rel >= n) return nullptr;
            uint16_t off = labelOff[chan][idx];
            for (int i = 0; i < rel; i++)
                off += strlen(&labelPool[chan][off]) + 1;
            return &labelPool[chan][off];
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
            // NOTE: no optimistic local update — a load can be REFUSED on
            // the DSP (stereo ch0 blocks ch1), and the truth arrives via
            // the pushActivePlugin that follows every load attempt.
            // Arm the loading overlay: the menu task shows the splash bar
            // until the new plugin's params table lands (or the load is
            // refused / times out).
            loadPendingCh = chan;
            snprintf(loadPendingId, sizeof(loadPendingId), "%s", id.c_str());
            loadPendingStartMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
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

        void DspProxy::SetSdRoute(int r) {
            if (r != 0 && r != 1) return;
            sdRoute = r;
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"cmd\":\"/link/sdRoute\",\"v\":%d}", r);
            sendCmd(buf);
        }

        int DspProxy::GetSdRoute() { return sdRoute; }

        void DspProxy::SetSdGain(int percent) {
            if (percent < 0) percent = 0;
            if (percent > 100) percent = 100;
            sdGain = percent;
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"cmd\":\"/link/sdGain\",\"v\":%d}", percent);
            sendCmd(buf);
        }

        int DspProxy::GetSdGain() { return sdGain; }

        uint32_t DspProxy::GetSdLevel() { return sdLevel; }

        // The /debug/ round trips (LinkClient::RequestDebug) complete on the
        // DSP's cmd-echoed reply; the sdInfo/sdDelete handlers live in the
        // DSP's shared /debug dispatcher (DebugUI::handleCommand).
        bool DspProxy::SdGetFileInfo(const char *path, SdFileInfo *out) {
            if (!path || !out) return false;
            memset(out, 0, sizeof(*out));
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "{\"cmd\":\"/debug/sdInfo\",\"path\":\"%s\"}", path);
            string reply;
            if (!LinkClient::RequestDebug(buf, reply)) return false;
            Document d;
            d.Parse(reply.c_str());
            if (d.HasParseError() || !d.IsObject() || d.HasMember("error")) return false;
            if (!d.HasMember("size") || !d["size"].IsUint()) return false;
            out->sizeBytes = d["size"].GetUint();
            if (d.HasMember("frames") && d["frames"].IsUint()) out->frames = d["frames"].GetUint();
            if (d.HasMember("sr") && d["sr"].IsUint()) out->sampleRate = (uint16_t) d["sr"].GetUint();
            if (d.HasMember("ch") && d["ch"].IsUint()) out->channels = (uint16_t) d["ch"].GetUint();
            if (d.HasMember("bits") && d["bits"].IsUint()) out->bits = (uint16_t) d["bits"].GetUint();
            return true;
        }

        bool DspProxy::SdDelete(const char *path) {
            if (!path) return false;
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "{\"cmd\":\"/debug/sdDelete\",\"path\":\"%s\"}", path);
            string reply;
            if (!LinkClient::RequestDebug(buf, reply)) return false;
            Document d;
            d.Parse(reply.c_str());
            return !d.HasParseError() && d.IsObject() && !d.HasMember("error");
        }

        std::string DspProxy::GetAllFavorites() { return cacheFavorites; }

        int32_t DspProxy::GetActiveFav() { return activeFav; }

        void DspProxy::ActivateFavorite(int id) {
            activeFav = id;
            char buf[80];
            snprintf(buf, sizeof(buf), "{\"cmd\":\"/link/favoriteActivate\",\"id\":%d}", id);
            sendCmd(buf);
        }

        bool DspProxy::IsSDMounted() { return sdMounted; }

        bool DspProxy::IsSDPresent() { return sdCardPresent; }

        void DspProxy::SdUnmount() { sendCmd("{\"cmd\":\"/link/sdUnmount\"}"); }

        void DspProxy::SdMount() { sendCmd("{\"cmd\":\"/link/sdMount\"}"); }

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
        float DspProxy::GetBPM() { return mb.bpm; }
        void DspProxy::SetBPM(float v) { mb.bpm = clampF(v, 20.0f, 480.0f); sendMarblesSet("bpm", v); }
        int DspProxy::GetXYClockSource() { return mb.xyClockSource; }
        void DspProxy::SetXYClockSource(int v) { mb.xyClockSource = clampI(v, 0, 4); sendMarblesSet("txy", v); }
        int DspProxy::GetXScale() { return mb.xScale; }
        void DspProxy::SetXScale(int v) { mb.xScale = clampI(v, 0, 7); sendMarblesSet("xsc", v); }
        int DspProxy::GetXRange() { return mb.xRange; }
        void DspProxy::SetXRange(int v) { mb.xRange = clampI(v, 0, 2); sendMarblesSet("xrg", v); }
        int DspProxy::GetXControl() { return mb.xControl; }
        void DspProxy::SetXControl(int v) { mb.xControl = clampI(v, 0, 2); sendMarblesSet("xc", v); }
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
