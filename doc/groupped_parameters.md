Complete Analysis: Grouped Parameters in CTAG TBD
> **Status: IMPLEMENTED** — this analysis was fully implemented. See `UIMenuPageParams` for the actual code.
> Key outcomes: ParamInfo[256] + GroupInfo[32] in SPIRAM, MODE_GROUP→MODE_EDIT recursion, MODE_MAP for CV slot assignment, dual-channel editing.

1. parseParams() — Full Implementation
From /home/ubuntu/ctag-tbd/main/menupages/UIMenuPageParams.cpp, lines 47-71:
void UIMenuPageParams::parseParams() {
    paramCount = 0;
    const char *json = SoundProcessorManager::GetCStrJSONActivePluginParams(0);
    if (!json) return;

    Document doc;
    doc.Parse(json);
    if (!doc.HasMember("params") || !doc["params"].IsArray()) return;

    const Value &arr = doc["params"];
    for (SizeType i = 0; i < arr.Size() && paramCount < MAX_PARAMS; i++) {
        const Value &p = arr[i];
        if (!p.HasMember("id") || !p.HasMember("name") || !p.HasMember("type")) continue;
        ParamInfo &pi = params[paramCount];
        snprintf(pi.id, sizeof(pi.id), "%s", p["id"].GetString());
        snprintf(pi.name, sizeof(pi.name), "%s", p["name"].GetString());
        snprintf(pi.type, sizeof(pi.type), "%s", p["type"].GetString());
        pi.min = p.HasMember("min") ? p["min"].GetInt() : 0;
        pi.max = p.HasMember("max") ? p["max"].GetInt() : 1;
        pi.current = p.HasMember("current") ? p["current"].GetInt() : 0;
        // skip groups for now — treat as single item
        if (strcmp(pi.type, "group") == 0) continue;
        paramCount++;
    }
}
It iterates the top-level params array of the merged JSON. For each entry, it reads id, name, type, min, max, current. When it hits type == "group", it does continue — meaning it consumes one slot in params[] but does NOT increment paramCount. Groups are silently skipped. No recursion into the group's nested params array occurs.
2. ParamInfo Struct and MAX_PARAMS
From /home/ubuntu/ctag-tbd/main/menupages/UIMenuPageParams.hpp, lines 38-58:
struct ParamInfo {
    char id[24];
    char name[24];
    char type[24];
    int min;
    int max;
    int current;
};

// ...
static const int MAX_PARAMS = 64;
ParamInfo params[MAX_PARAMS];
Key observations:
- 
- 
- 
- 
3. How Groups Are Structured in mui JSON (with examples)
From the 3 largest files (mui-DrumRack.jsn, mui-Antique.jsn, mui-GDVerb2.jsn):
Pattern: A group is a JSON object with "type": "group", an "id", a "name", and a nested "params" array of leaf params:
{
  "id": "ab_group",
  "name": "Analogue Bass Drum",
  "type": "group",
  "params": [
    { "id": "ab_trigger", "name": "Trigger", "type": "bool" },
    { "id": "ab_lev",     "name": "Level",   "type": "int", "min": 0, "max": 4095 },
    { "id": "ab_pan",     "name": "Pan",     "type": "int", "min": -4095, "max": 4095 }
    // ... more leaf params
  ]
}
Concrete hierarchy from GDVerb2:
params[]
├── { id:"decay",       name:"Length",      type:"group", params:[ leaf1, leaf2, ... ] }
├── { id:"diffusion",   name:"Diffusion",   type:"group", params:[ leaf1, leaf2, ... ] }
├── { id:"damping",     name:"Damping",     type:"group", params:[ leaf1, leaf2, ... ] }
├── { id:"modulation",  name:"Modulation",  type:"group", params:[ leaf1, leaf2, ... ] }
└── { id:"ios",         name:"Input/Output",type:"group", params:[ leaf1, leaf2, ... ] }
Concrete hierarchy from Antique:
params[]
├── { id:"input",    name:"Input",              type:"group", params: [3 leaf params] }
├── { id:"hiss",     name:"Hiss / Scrub / Hum", type:"group", params: [12 leaf params] }
├── { id:"wowflut",  name:"Wow and flutter",    type:"group", params: [4 leaf params] }
├── { id:"clickpop", name:"Clicks and pops",    type:"group", params: [14 leaf params] }
└── { id:"outp",     name:"Output",             type:"group", params: [6 leaf params] }
Key structural facts:
- 
- 
- 
- 
4. Preset Data Model Is Flat — No Grouping
From /home/ubuntu/ctag-tbd/spiffs_image/data/sp/mp-DrumRack.jsn:
{
  "activePatch": 0,
  "patches": [
    {
      "name": "Default",
      "params": [
        { "id": "ab_trigger", "current": 0,   "trig": 16 },
        { "id": "ab_mute",    "current": 0,   "trig": -1 },
        { "id": "ab_lev",     "current": 2047, "cv": -1 },
        { "id": "ab_pan",     "current": 0,    "cv": -1 },
        // ... every leaf param is a flat entry with just id + current (+ optional cv/trig)
      ]
    }
  ]
}
The mp-*.jsn file's params array is completely flat — there is no type, no name, no min/max, no group envelope. Just id + current (the live value) + optional cv (CV modulation amount, -1 = none) and trig (trigger button index, -1 = none).
5. recursiveFindAndInsert — The Merge Mechanism
From /home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSPDataModel.cpp, lines 167-198:
void ctagSPDataModel::recursiveFindAndInsert(const Value ¶mF, Value ¶mI) {
    if (!paramI.IsArray()) return;
    for (auto &v : paramI.GetArray()) {
        if (!paramF.HasMember("id")) return;
        if (!v.HasMember("id")) return;
        if (paramF["id"] == v["id"]) {
            // MATCH — inject current, cv, trig into the mui entry
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
        } else if (v["type"] == "group") {
            // RECURSE into the group's params array
            recursiveFindAndInsert(paramF, v["params"]);
        }
    }
}
And mergeModels() (lines 66-75) drives it:
void ctagSPDataModel::mergeModels() {
    if (!activePreset.HasMember("params")) return;
    if (!mui.HasMember("params")) return;
    Value &patchParams = activePreset["params"];
    for (auto &v : patchParams.GetArray()) {
        recursiveFindAndInsert(v, mui["params"]);
    }
}
How it works:
1. 
2. 
3. 
4. 
So the merge preserves the mui nested group structure but adds current values from the flat preset into the correct leaf nodes.
6. Does GetCStrJSONActivePluginParams Return Groups with Current Values?
Yes. The call chain is:
UIMenuPageParams::parseParams()
  → SoundProcessorManager::GetCStrJSONActivePluginParams(0)
    → sp[0]->GetCStrJSONParamSpecs()                [SPManager.hpp:54, ctagSoundProcessor.hpp:106]
      → model->GetCStrJSONParams()                  [ctagSPDataModel.cpp:57-64]
        → mergeModels()                             [injects current/cv/trig into mui tree]
        → Writer<StringBuffer> writer(json)
        → mui.Accept(writer)                        [serializes the full hierarchical mui]
The returned JSON string is the full hierarchical mui structure with current, cv, and trig values merged in from the active preset. Groups are present in this JSON, with their nested params arrays. Each leaf node now has a current field (and optionally cv or trig).
The data is there and complete — the problem is solely in how parseParams() consumes it.
7. What Data Structures Would Be Needed for Group-Aware Editing
Based on the codebase analysis, here is what you would need:
A. A Group-aware ParamInfo that can represent hierarchy:
struct ParamInfo {
    char id[24];
    char name[24];       // empty for group headers if needed
    char type[24];       // "group", "int", "bool", "enum"
    char groupId[24];    // which group this belongs to (or "" for standalone / group headers)
    int min;
    int max;
    int current;
    int childStart;      // index into params[] for first child (if this is a group)
    int childCount;      // number of children (if this is a group)
};
Or more idiomatic for embedded: a separate GroupInfo struct and a flat array approach:
struct GroupInfo {
    char id[24];
    char name[24];
    int firstParamIndex;  // index into params[]
    int paramCount;
};

struct ParamInfo {
    char id[24];
    char name[24];
    char type[24];
    int groupIndex;        // -1 for standalone, or index into groups[]
    int min, max, current;
};
B. Navigation modes would need a new state:
MODE_SELECT → MODE_GROUP_SELECT → MODE_EDIT → MODE_VALUEEDIT
Where MODE_GROUP_SELECT shows the list of group names first, then entering a group drops into MODE_EDIT showing only that group's leaf params. A onBack() would return to group selection.
C. Recursive parsing in parseParams():
void UIMenuPageParams::parseParams() {
    paramCount = 0;
    groupCount = 0;
    const char *json = SoundProcessorManager::GetCStrJSONActivePluginParams(0);
    // ... parse JSON ...
    parseParamsRecursive(doc["params"], -1); // -1 = top-level
}

void UIMenuPageParams::parseParamsRecursive(const Value &arr, int parentGroup) {
    for (SizeType i = 0; i < arr.Size() && paramCount < MAX_PARAMS; i++) {
        const Value &p = arr[i];
        if (strcmp(p["type"].GetString(), "group") == 0) {
            int gIdx = groupCount++;
            groups[gIdx].firstParamIndex = paramCount;
            snprintf(groups[gIdx].id, sizeof(groups[gIdx].id), "%s", p["id"].GetString());
            snprintf(groups[gIdx].name, sizeof(groups[gIdx].name), "%s", p["name"].GetString());
            parseParamsRecursive(p["params"], gIdx);
            groups[gIdx].paramCount = paramCount - groups[gIdx].firstParamIndex;
        } else {
            ParamInfo &pi = params[paramCount];
            // ... fill fields ...
            pi.groupIndex = parentGroup;
            paramCount++;
        }
    }
}
D. MAX_PARAMS must be increased. DrumRack alone has ~134 leaf params. A safe value would be at least 192 — but this must be weighed against DRAM constraints (~22KB free). Using PSRAM (MALLOC_CAP_SPIRAM) for the params array should be considered.
8. MAX_PARAMS = 64 — Sufficiency Assessment
Not sufficient. Table of worst-case leaf param counts:
Plugin
DrumRack
Antique
GDVerb2
WTOscDuo
Sample 1-3
DrumRack alone has ~134 leaf params, which exceeds MAX_PARAMS by more than 2x. With the current code, only the first 64 items in the mui JSON's top-level params[] array are considered — and since groups are skipped via continue (not counted), the actual count of processed entries is even lower: at most 64 top-level array entries, then groups are skipped, so you might end up with as few as 64 - 10 = 54 actual leaf params shown. The remaining ~80 params are silently invisible to the UI.
Recommendation: Increase MAX_PARAMS to at least 196, or make it dynamic with heap allocation. Given the DRAM tightness noted in the project docs (~22KB free), the params array should use heap_caps_malloc(size, MALLOC_CAP_SPIRAM) like the sound processor allocator does.
Summary of All Files Examined
File
/home/ubuntu/ctag-tbd/main/menupages/UIMenuPageParams.cpp
/home/ubuntu/ctag-tbd/main/menupages/UIMenuPageParams.hpp
/home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSPDataModel.cpp
/home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSPDataModel.hpp
/home/ubuntu/ctag-tbd/components/ctagSoundProcessor/ctagSoundProcessor.hpp
/home/ubuntu/ctag-tbd/main/SPManager.hpp
/home/ubuntu/ctag-tbd/main/SPManager.cpp
/home/ubuntu/ctag-tbd/spiffs_image/data/sp/mui-DrumRack.jsn
/home/ubuntu/ctag-tbd/spiffs_image/data/sp/mui-Antique.jsn
/home/ubuntu/ctag-tbd/spiffs_image/data/sp/mui-GDVerb2.jsn
/home/ubuntu/ctag-tbd/spiffs_image/data/sp/mp-DrumRack.jsn
