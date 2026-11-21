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


#include "ctagSPDataModel.hpp"
#include "rapidjson/filereadstream.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "esp_log.h"
#include "ctagResources.hpp"

/*
#ifndef TBD_SIM
#define SPIFFS_PATH "/spiffs"
#else
#define SPIFFS_PATH "../../spiffs_image"
#endif
 */

using namespace CTAG::SP;

ctagSPDataModel::ctagSPDataModel(const string &id, const bool isStereo) {
    // acquire data from json files ui model and patch model
    muiFileName = string(CTAG::RESOURCES::spiffsRoot + "/data/sp/mui-") + id + string(".jsn");
    //std::cout << "Reading " << muiFileName << std::endl;
    loadJSON(mui, muiFileName);
    mpFileName = string(CTAG::RESOURCES::spiffsRoot + "/data/sp/mp-") + id + string(".jsn");
    //std::cout << "Reading " << mpFileName << std::endl;
    loadJSON(mp, mpFileName);
    // load last activated preset
    ESP_LOGD("Model", "Loading patch number %d", mp["activePatch"].GetInt());
    LoadPreset(mp["activePatch"].GetInt());

}

ctagSPDataModel::~ctagSPDataModel() {
}

const char *ctagSPDataModel::GetCStrJSONParams() {
    if (paramsDirty) {
        json.Clear();
        mergeModels();
        Writer<StringBuffer> writer(json);
        mui.Accept(writer);
        cachedParamsJson.assign(json.GetString(), json.GetSize());
        paramsDirty = false;
    }
    return cachedParamsJson.c_str();
}

void ctagSPDataModel::mergeModels() {
    // iterate preset model for all parameters
    if (!activePreset.HasMember("params")) return;
    if (!mui.HasMember("params")) return;
    Value &patchParams = activePreset["params"];
    for (auto &v : patchParams.GetArray()) {
        //printf("Searching for %s in ui model", v["id"].GetString());
        recursiveFindAndInsert(v, mui["params"]);
    }
}

void ctagSPDataModel::PreFillMissingParams(rapidjson::Value &params,
                                           const rapidjson::Value &specs,
                                           rapidjson::Document::AllocatorType &alloc) {
    if (!params.IsArray() || !specs.IsArray()) return;
    for (const auto &spec : specs.GetArray()) {
        if (!spec.HasMember("id") || !spec["id"].IsString()) continue;
        if (spec.HasMember("type") && spec["type"] == "group") {
            if (spec.HasMember("params")) {
                PreFillMissingParams(params, spec["params"], alloc);
            }
            continue;
        }
        const char *id = spec["id"].GetString();
        bool found = false;
        for (const auto &p : params.GetArray()) {
            if (p.HasMember("id") && p["id"].IsString() && p["id"] == id) {
                found = true;
                break;
            }
        }
        if (found) continue;
        int def = 0;
        if (spec.HasMember("def") && spec["def"].IsInt()) {
            def = spec["def"].GetInt();
        }
        const bool isBool = spec.HasMember("type") && spec["type"] == "bool";
        Value entry(kObjectType);
        entry.AddMember(Value("id", alloc), Value(id, alloc), alloc);
        entry.AddMember(Value("current", alloc), Value(def), alloc);
        if (isBool) {
            entry.AddMember(Value("trig", alloc), Value(-1), alloc);
        } else {
            entry.AddMember(Value("cv", alloc), Value(-1), alloc);
        }
        params.PushBack(entry, alloc);
    }
}

void ctagSPDataModel::SetParamValue(const string &id, const string &key, const int val) {
    // NO logging here: SetParamValue runs on the link task for EVERY
    // parameter change from the panel. On the BBA2 the console is the
    // USB-Serial-JTAG, whose vfs write busy-waits up to 50 ms when the
    // host is not draining the TX FIFO — a single log line here can block
    // the link loop for seconds and trip the watchdogs (handoff doc,
    // Finding 1). Parameter paths must stay allocation- and logging-free.
    if (!activePreset.HasMember("params")) return;
    Value &patchParams = activePreset["params"];
    if (!patchParams.IsArray()) return;
    bool found = false;
    for (auto &v : patchParams.GetArray()) {
        //printf("Searching for %s in ui model", v["id"].GetString());
        if (!v.HasMember("id")) return;
        if (!v["id"].IsString()) return;
        if (v["id"] == id) {
            found = true;
            if (!v.HasMember(key)) {
                // Params whose factory-patch entry lacks the cv/trig member
                // would silently drop EVERY mapping made from the panel:
                // the engine applies it live, the model never records it,
                // and the mapping orphans on reboot (unmaps can't persist
                // either). Add the member — same as recursiveFindAndInsert
                // does for the UI copy.
                Value newKey(key, mui.GetAllocator());
                Value newVal(val);
                v.AddMember(newKey, newVal, mui.GetAllocator());
            } else {
                v[key] = val;
            }
            paramsDirty = true;
            BumpGeneration();
        }
    }
    if (!found) {
        // The loaded preset predates this parameter entirely (e.g. the
        // 2026-11-17 report: an old user preset + the new dist/warps
        // params) — a missing entry would silently drop the edit: the
        // engine got it (setParamValueInternal fired first) but the model
        // never records it, so the next re-push / flush / reload reverts
        // it. Append a complete entry.
        Value entry(kObjectType);
        Value idv(id, mui.GetAllocator());
        entry.AddMember(Value("id", mui.GetAllocator()), idv, mui.GetAllocator());
        Value cur("current", mui.GetAllocator());
        entry.AddMember(cur, Value(key == "current" ? val : 0), mui.GetAllocator());
        Value cvk("cv", mui.GetAllocator());
        entry.AddMember(cvk, Value(key == "cv" ? val : -1), mui.GetAllocator());
        Value trk("trig", mui.GetAllocator());
        entry.AddMember(trk, Value(key == "trig" ? val : -1), mui.GetAllocator());
        patchParams.PushBack(entry, mui.GetAllocator());
        paramsDirty = true;
        BumpGeneration();
    }
}

int ctagSPDataModel::GetParamValue(const string &id, const string &key) {
    if (!activePreset.HasMember("params")) return 0;
    Value &patchParams = activePreset["params"];
    if (!patchParams.IsArray()) return 0;
    for (auto &v : patchParams.GetArray()) {
        // Malformed entries must not abort the search early — skip them.
        if (!v.HasMember("id")) continue;
        if (!v["id"].IsString()) continue;
        if (v["id"] == id) {
            if (!v.HasMember(key)) return 0;
            return v[key].GetInt();
        }
    }
    return 0;
}

bool ctagSPDataModel::HasParam(const string &id) {
    if (!activePreset.IsObject()) return false;
    if (!activePreset.HasMember("params")) return false;
    Value &patchParams = activePreset["params"];
    if (!patchParams.IsArray()) return false;
    for (auto &v : patchParams.GetArray()) {
        if (!v.HasMember("id") || !v["id"].IsString()) continue;
        if (v["id"] == id) return true;
    }
    return false;
}


const char *ctagSPDataModel::GetCStrJSONPresets() {
    if (!mp.HasMember("activePatch")) return nullptr;
    if (!mp["activePatch"].IsInt()) return nullptr;
    if (!mp.HasMember("patches") || !mp["patches"].IsArray()) return nullptr;
    if (presetsDirty) {
        json.Clear();
        int num = 0;
        Value obj(kObjectType);
        Value a(kArrayType);
        Value n(kNumberType);
        Document p;
        n.SetInt(mp["activePatch"].GetInt());
        obj.AddMember("activePresetNumber", n, p.GetAllocator());
        for (auto &v : mp["patches"].GetArray()) {
            Value o(kObjectType);
            Value name(kStringType);
            name.SetString(v["name"].GetString(), p.GetAllocator());
            o.AddMember("name", name.Move(), p.GetAllocator());
            o.AddMember("number", num++, p.GetAllocator());
            a.PushBack(o, p.GetAllocator());
        }
        obj.AddMember("presets", a, p.GetAllocator());
        Writer<StringBuffer> writer(json);
        obj.Accept(writer);
        cachedPresetsJson.assign(json.GetString(), json.GetSize());
        presetsDirty = false;
    }
    return cachedPresetsJson.c_str();
}

void ctagSPDataModel::LoadPreset(const int num) {
    loadJSON(mp, mpFileName);
    int patchNum = num;
    if (patchNum < 0) patchNum = 0;
    if (!mp.HasMember("patches")) return;
    if (!mp["patches"].IsArray()) return;
    if (patchNum >= mp["patches"].GetArray().Size()) {
        ESP_LOGD("Model", "Bounds check patch num %d, size %d", patchNum, mp["patches"].GetArray().Size());
        patchNum = mp["patches"].GetArray().Size() - 1;
    }
    activePreset.GetAllocator().Clear();
    activePreset.CopyFrom(mp["patches"].GetArray()[patchNum], activePreset.GetAllocator());
    // Pre-fill: presets saved before a parameter existed carry no entry for
    // it — SetParamValue matches by id and would silently drop EVERY edit
    // of that param, and the next params re-push would show it at 0 (the
    // 2026-11-17 dist/warps report). Seed missing params from the mui
    // spec's "def" member (factory default, 0 when absent) so the loaded
    // preset is complete: the engine gets real defaults via
    // loadPresetInternal, the panel displays them, edits record, the flush
    // persists.
    if (mui.HasMember("params") && activePreset.HasMember("params") &&
        activePreset["params"].IsArray()) {
        PreFillMissingParams(activePreset["params"], mui["params"],
                             activePreset.GetAllocator());
    }
    ESP_LOGD("Model", "Preset Name is %s number %d", activePreset["name"].GetString(), patchNum);
    // save currently loaded preset to model
    if (!mp.HasMember("activePatch")) return;
    mp["activePatch"].SetInt(patchNum);
    paramsDirty = true;
    presetsDirty = true;
    storeJSON(mp, mpFileName);
    json.Clear();
    Writer<StringBuffer> writer(json);
    mp.Accept(writer);
}

void ctagSPDataModel::recursiveFindAndInsert(const Value &paramF, Value &paramI) {
    if (!paramI.IsArray()) return;
    for (auto &v : paramI.GetArray()) {
        //printf("Matching for %s in with %s", paramF["id"].GetString(), v["id"].GetString());
        if (!paramF.HasMember("id")) return;
        if (!v.HasMember("id")) return;
        if (v.HasMember("type") && v["type"] == "group") {
            // Recurse into the group's params array
            if (v.HasMember("params"))
                recursiveFindAndInsert(paramF, v["params"]);
        } else if (paramF["id"] == v["id"]) {
            //printf("Found, inserting data...");
            Value::MemberIterator iter = v.FindMember("current");
            if (iter == v.MemberEnd())
                v.AddMember("current", paramF["current"].GetInt(), mui.GetAllocator());
            else
                iter->value = paramF["current"].GetInt();
            if (v["type"] == "bool" && paramF.HasMember("trig")) {
                Value::MemberIterator iter = v.FindMember("trig");
                if (iter == v.MemberEnd())
                    v.AddMember("trig", paramF["trig"].GetInt(), mui.GetAllocator());
                else
                    iter->value = paramF["trig"].GetInt();
            } else if (v["type"] == "int" && paramF.HasMember("cv")) {
                Value::MemberIterator iter = v.FindMember("cv");
                if (iter == v.MemberEnd())
                    v.AddMember("cv", paramF["cv"].GetInt(), mui.GetAllocator());
                else
                    iter->value = paramF["cv"].GetInt();
            }
            break;
        }
    }
}


int ctagSPDataModel::SavePreset(const string &name, const int number) {
    //ESP_LOGE("Model", "Save preset %s %d", name.c_str(), number);
    //ESP_LOGE("MOdel", "Stored JSON before");
    //PrintSelf();
    loadJSON(mp, mpFileName);
    int patchNum = number;
    if (patchNum < 0) patchNum = 0;
    if (!mp.HasMember("patches")) return -1;
    if (!mp["patches"].IsArray()) return -1;
    if (patchNum > mp["patches"].GetArray().Size()) patchNum = mp["patches"].GetArray().Size();
    if (!activePreset.HasMember("name")) return -1;
    activePreset["name"].SetString(name, activePreset.GetAllocator());
    //ESP_LOGE("Model", "Adding new number %d, patchnum %d, patch array size %d", number, patchNum, mp["patches"].GetArray().Size());
    if (patchNum == mp["patches"].GetArray().Size()) {
        Value copyOfPreset(activePreset, mp.GetAllocator());
        //copyOfPreset["name"].SetString(name, mp.GetAllocator());
        mp["patches"].PushBack(copyOfPreset.Move(), mp.GetAllocator());
        /*
        ESP_LOGE("Model", "Adding new number %d, patchnum %d, patch array size %d", number, patchNum,
                 mp["patches"].GetArray().Size());
                 */
    } else {
        mp["patches"][patchNum].CopyFrom(activePreset, mp.GetAllocator()); // saved preset is current
        //ESP_LOGE("Model", "Replace");
    }
    if (!mp.HasMember("activePatch")) return -1;
    mp["activePatch"] = patchNum;
    paramsDirty = true;
    presetsDirty = true;
    storeJSON(mp, mpFileName);
    //ESP_LOGE("MOdel", "Stored JSON after");
    //PrintSelf();
    return patchNum;
}

void ctagSPDataModel::PrintSelf() {
    ESP_LOGD("Model", "mp:");
    printJSON(mp);
    ESP_LOGD("Model", "mui:");
    printJSON(mui);
    ESP_LOGD("Model", "activePreset:");
    printJSON(activePreset);
}

const char *ctagSPDataModel::GetCStrJSONAllPresetData() {
    Document d;
    json.Clear();
    loadJSON(d, mpFileName);
    Writer<StringBuffer> writer(json);
    d.Accept(writer);
    //printf("%s\n", json.GetString());
    return json.GetString();
}

bool ctagSPDataModel::IsParamTrig(const string &id) {
    if (!activePreset.HasMember("params")) return false;
    Value &patchParams = activePreset["params"];
    if (!patchParams.IsArray()) return false;
    for (auto &v : patchParams.GetArray()) {
        if (!v.HasMember("id")) return false;
        if (!v["id"].IsString()) return false;
        if (v["id"] == id) {
            if (v.HasMember("trig")) return true;
        }
    }
    return false;
}

bool ctagSPDataModel::IsParamCV(const string &id) {
    if (!activePreset.HasMember("params")) return false;
    Value &patchParams = activePreset["params"];
    if (!patchParams.IsArray()) return false;
    for (auto &v : patchParams.GetArray()) {
        if (!v.HasMember("id")) return false;
        if (!v["id"].IsString()) return false;
        if (v["id"] == id) {
            if (v.HasMember("cv")) return true;
        }
    }
    return false;
}

std::string ctagSPDataModel::GetActivePluginParameters() {
    rapidjson::StringBuffer parameters;
    rapidjson::Writer<rapidjson::StringBuffer> writer(parameters);
    activePreset.Accept(writer);
    return parameters.GetString();
}

void ctagSPDataModel::SetActivePluginParameters(const std::string &parameters) {
    Document d;
    d.Parse(parameters.c_str());
    activePreset.GetAllocator().Clear();
    activePreset.CopyFrom(d, activePreset.GetAllocator());
    paramsDirty = true;
    BumpGeneration();
}
