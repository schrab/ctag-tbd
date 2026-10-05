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

#include "ModEngine.hpp"
#include <cmath>
#include "esp_log.h"
#include "rapidjson/document.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/writer.h"
#include <cstdio>

using namespace CTAG::DRIVERS;

static const char *TAG = "MOD";

// CV slots: 90-97 dynamic CC mapping, 98-99 LFOs
// Slot 90 = OFFSET + 0, etc.
#define DYN_SLOT_BASE 90
#define LFO1_SLOT 98
#define LFO2_SLOT 99

// TBD codec runs at 44100 Hz with 32-sample audio blocks; the tempo engine
// derives its block rate from these.
static constexpr float MOD_TEMPO_FS = 44100.0f;
static constexpr uint32_t MOD_TEMPO_BLOCK = 32;

float ModEngine::lfoPhase[2] = {0, 0};
float ModEngine::lfoRate[2] = {1.0f, 2.0f};
float ModEngine::lfoAmplitude[2] = {0.5f, 0.5f};
int ModEngine::lfoShape[2] = {0, 0};
int ModEngine::lfoCVSlot[2] = {LFO1_SLOT, LFO2_SLOT};
float ModEngine::lfoHold[2] = {0, 0};
bool ModEngine::lfoSync[2] = {false, false};

int ModEngine::dynCC[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
int ModEngine::dynChan[8] = {0};
int ModEngine::dynTargetSlot[8] = {90, 91, 92, 93, 94, 95, 96, 97};

bool ModEngine::learning = false;
int ModEngine::lastLearnedSlot = -1;

CTAG::SP::HELPERS::ctagTempo ModEngine::tempoEngine;
float ModEngine::tempoBpm = 120.0f;
CTAG::SP::HELPERS::ctagTempo::Source ModEngine::tempoSource =
    CTAG::SP::HELPERS::ctagTempo::Source::INTERNAL;
CTAG::SP::HELPERS::ctagSeq16 ModEngine::sequencer[2];
CTAG::SP::HELPERS::ctagGate16 ModEngine::gate[2];

CTAG::SP::HELPERS::ctagTempo& ModEngine::GetTempoEngine() {
    return tempoEngine;
}

CTAG::SP::HELPERS::ctagSeq16& ModEngine::GetSequencer(int idx) {
    return sequencer[idx];
}

CTAG::SP::HELPERS::ctagGate16& ModEngine::GetGate(int idx) {
    return gate[idx];
}

void ModEngine::Init() {
    for (int i = 0; i < 8; i++) {
        dynTargetSlot[i] = DYN_SLOT_BASE + i;
        dynCC[i] = -1;
        dynChan[i] = 0;
    }
    lfoPhase[0] = 0;
    lfoPhase[1] = 0;
    
    // Initialize tempo engine (44100 Hz, 32 samples per block)
    tempoEngine.SetSampleRate(MOD_TEMPO_FS, MOD_TEMPO_BLOCK);
    
    LoadConfig();
    ESP_LOGI(TAG, "ModEngine initialized, slots 90-99");
}

void ModEngine::Process(float *cv_buffer, uint8_t *trig_buffer) {
    tempoEngine.Tick();
    // Update LFOs
    for (int lfo = 0; lfo < 2; lfo++) {
        if (lfoSync[lfo]) {
            lfoPhase[lfo] = tempoEngine.GetPhase();
        } else {
            lfoPhase[lfo] += lfoRate[lfo] / 1378.0f; // block rate
            if (lfoPhase[lfo] > 1.0f) lfoPhase[lfo] -= 1.0f;
        }

        float val;
        switch (lfoShape[lfo]) {
            case 0: // sine
                val = sinf(lfoPhase[lfo] * 2.0f * 3.14159f);
                break;
            case 1: // triangle
                val = 4.0f * fabsf(lfoPhase[lfo] - 0.5f) - 1.0f;
                break;
            case 2: // saw
                val = 2.0f * lfoPhase[lfo] - 1.0f;
                break;
            case 3: // square
                val = (lfoPhase[lfo] < 0.5f) ? 1.0f : -1.0f;
                break;
            case 4: // S&H
                if (lfoPhase[lfo] < lfoRate[lfo] / 1378.0f) // new sample each cycle
                    lfoHold[lfo] = 2.0f * ((float)rand() / RAND_MAX) - 1.0f;
                val = lfoHold[lfo];
                break;
            default:
                val = 0;
        }
        val *= lfoAmplitude[lfo];
        int slot = lfoCVSlot[lfo];
        if (slot >= 0 && slot < N_CVS) {
            cv_buffer[slot] = val;
        }
    }

    // Update sequencers
    float bpm = tempoEngine.GetBPM();
    for (int i = 0; i < 2; i++) {
        sequencer[i].Process(bpm, cv_buffer, trig_buffer, 32);
    }

    // Update gate generators
    for (int i = 0; i < 2; i++) {
        gate[i].Process(bpm, cv_buffer, trig_buffer, 32);
    }
}

void ModEngine::SetLFOShape(int lfo, int shape) {
    if (lfo < 0 || lfo > 1) return;
    lfoShape[lfo] = shape;
}

void ModEngine::SetLFORate(int lfo, float hz) {
    if (lfo < 0 || lfo > 1) return;
    lfoRate[lfo] = hz;
}

void ModEngine::SetLFOAmplitude(int lfo, float amp) {
    if (lfo < 0 || lfo > 1) return;
    lfoAmplitude[lfo] = amp;
}

int ModEngine::GetLFOCVSlot(int lfo) { return (lfo >= 0 && lfo < 2) ? lfoCVSlot[lfo] : -1; }
void ModEngine::SetLFOCVSlot(int lfo, int slot) {
    if (lfo >= 0 && lfo < 2 && slot >= 0 && slot < N_CVS) lfoCVSlot[lfo] = slot;
}
int ModEngine::GetLFOShape(int lfo) { return (lfo >= 0 && lfo < 2) ? lfoShape[lfo] : 0; }
float ModEngine::GetLFORate(int lfo) { return (lfo >= 0 && lfo < 2) ? lfoRate[lfo] : 0; }
float ModEngine::GetLFOAmplitude(int lfo) { return (lfo >= 0 && lfo < 2) ? lfoAmplitude[lfo] : 0; }
bool ModEngine::GetLFOSync(int lfo) { return (lfo >= 0 && lfo < 2) ? lfoSync[lfo] : false; }
void ModEngine::SetLFOSync(int lfo, bool sync) { if (lfo >= 0 && lfo < 2) lfoSync[lfo] = sync; }

void ModEngine::StartLearn() {
    learning = true;
    lastLearnedSlot = -1;
    ESP_LOGI(TAG, "MIDI Learn started");
}

void ModEngine::StopLearn() {
    learning = false;
    ESP_LOGI(TAG, "MIDI Learn stopped, slot %d", lastLearnedSlot);
}

bool ModEngine::IsLearning() { return learning; }

int ModEngine::GetLastLearnedSlot() { return lastLearnedSlot; }

int ModEngine::GetDynamicSlotCC(int slot_idx) {
    if (slot_idx < 0 || slot_idx >= 8) return -1;
    return dynCC[slot_idx];
}

void ModEngine::SetDynamicSlotCC(int slot_idx, int cc) {
    if (slot_idx < 0 || slot_idx >= 8) return;
    dynCC[slot_idx] = cc;
    ESP_LOGI(TAG, "Dynamic slot %d → CC %d", slot_idx, cc);
}

void ModEngine::SetDynamicSlotChan(int slot_idx, int chan) {
    if (slot_idx < 0 || slot_idx >= 8) return;
    dynChan[slot_idx] = chan;
}

int ModEngine::GetDynamicSlotChan(int slot_idx) {
    if (slot_idx < 0 || slot_idx >= 8) return 0;
    return dynChan[slot_idx];
}

// Called from Midi::controlChange()
namespace CTAG::DRIVERS {

void ModEngine_MaybeCaptureCC(uint8_t channel, uint8_t cc_num, uint8_t value) {
    if (!ModEngine::IsLearning()) return;

    // Find a free slot
    for (int i = 0; i < 8; i++) {
        if (ModEngine::GetDynamicSlotCC(i) == -1) {
            ModEngine::SetDynamicSlotCC(i, cc_num);
            ModEngine::SetDynamicSlotChan(i, channel);
            ModEngine::StopLearn();
            return;
        }
    }
    ESP_LOGW(TAG, "No free dynamic CC slots");
}

// Called from Midi::controlChange() — routes CC to dynamic CV slot
void ModEngine_ApplyDynamicCC(uint8_t channel, uint8_t cc_num, uint8_t value, float *cv_buffer) {
    for (int i = 0; i < 8; i++) {
        if (ModEngine::GetDynamicSlotCC(i) == cc_num &&
            ModEngine::GetDynamicSlotChan(i) == (int)channel) {
            int slot = DYN_SLOT_BASE + i;
            cv_buffer[slot] = value * (1.0f / 127.0f);
            return;
        }
    }
}

static const char* MOD_CFG_PATH = "/spiffs/data/mod-config.jsn";

void ModEngine::SaveConfig() {
    using namespace rapidjson;
    Document d;
    d.SetObject();
    auto &alloc = d.GetAllocator();

    for (int i = 0; i < 2; i++) {
        char key[8];
        snprintf(key, sizeof(key), "lfo%d", i + 1);
        Value lfo(kObjectType);
        lfo.AddMember("shape", lfoShape[i], alloc);
        lfo.AddMember("rate", lfoRate[i], alloc);
        lfo.AddMember("amp", lfoAmplitude[i], alloc);
        lfo.AddMember("cvSlot", lfoCVSlot[i], alloc);
        lfo.AddMember("sync", lfoSync[i], alloc);
        d.AddMember(Value(key, alloc).Move(), lfo, alloc);
    }

    Value ccArr(kArrayType);
    for (int i = 0; i < 8; i++) {
        Value slot(kObjectType);
        slot.AddMember("cc", dynCC[i], alloc);
        slot.AddMember("chan", dynChan[i], alloc);
        slot.AddMember("cvSlot", dynTargetSlot[i], alloc);
        ccArr.PushBack(slot, alloc);
    }

    // Tempo is part of the documented saved schema, so a user does not have to
    // re-dial the BPM after every reboot.
    Value tempo(kObjectType);
    tempo.AddMember("bpm", tempoEngine.GetBPM(), alloc);
    tempo.AddMember("source",
                    tempoEngine.GetSource() == SP::HELPERS::ctagTempo::Source::MIDI_CLOCK ? 1 : 0,
                    alloc);
    d.AddMember("tempo", tempo, alloc);
    for (int i = 0; i < 2; i++) {
        char key[8];
        snprintf(key, sizeof(key), "seq%d", i);
        Value sq(kObjectType);
        Value stepsArr(kArrayType);
        for (int j = 0; j < 16; j++) {
            stepsArr.PushBack(sequencer[i].GetStep(j), alloc);
        }
        sq.AddMember("steps", stepsArr, alloc);
        sq.AddMember("stepLength", sequencer[i].GetStepLength(), alloc);
        sq.AddMember("direction", (int)sequencer[i].GetDirection(), alloc);
        sq.AddMember("slew", sequencer[i].GetSlew(), alloc);
        sq.AddMember("cvSlot", sequencer[i].GetCVSlot(), alloc);
        sq.AddMember("trigSlot", sequencer[i].GetTrigSlot(), alloc);
        d.AddMember(Value(key, alloc).Move(), sq, alloc);
    }

    for (int i = 0; i < 2; i++) {
        char key[8];
        snprintf(key, sizeof(key), "gate%d", i);
        Value gt(kObjectType);
        Value enArr(kArrayType);
        Value probArr(kArrayType);
        for (int j = 0; j < 16; j++) {
            enArr.PushBack(gate[i].GetStepEnabled(j), alloc);
            probArr.PushBack((int)gate[i].GetStepProbability(j), alloc);
        }
        gt.AddMember("enabled", enArr, alloc);
        gt.AddMember("probs", probArr, alloc);
        gt.AddMember("stepLength", gate[i].GetStepLength(), alloc);
        gt.AddMember("direction", (int)gate[i].GetDirection(), alloc);
        gt.AddMember("trigSlot", gate[i].GetTrigSlot(), alloc);
        gt.AddMember("accentSlot", gate[i].GetAccentSlot(), alloc);
        gt.AddMember("swing", gate[i].GetSwing(), alloc);
        gt.AddMember("gateLength", gate[i].GetGateLength(), alloc);
        gt.AddMember("accentAmount", gate[i].GetAccentAmount(), alloc);
        d.AddMember(Value(key, alloc).Move(), gt, alloc);
    }

    d.AddMember("ccSlots", ccArr, alloc);

    FILE *fp = fopen(MOD_CFG_PATH, "w");
    if (!fp) {
        ESP_LOGE(TAG, "SaveConfig: cannot open %s", MOD_CFG_PATH);
        return;
    }
    // SaveConfig runs on the UIMenu task, whose 4 KB stack is deliberately
    // small (it only polls a queue and draws). FileWriteStream flushes in
    // chunks, so a large buffer buys nothing and risks exhausting that stack.
    char writeBuf[512];
    FileWriteStream os(fp, writeBuf, sizeof(writeBuf));
    Writer<FileWriteStream> writer(os);
    d.Accept(writer);
    fflush(fp);
    fclose(fp);
    ESP_LOGI(TAG, "SaveConfig: written to %s", MOD_CFG_PATH);
}

// mod-config.jsn lives on SPIFFS and is hand-editable, so every read is
// type-checked. RapidJSON's asserts compile out under NDEBUG, which would turn
// a wrong-typed member into undefined behaviour during ModEngine::Init() --
// before the UI exists to report anything.
static bool GetIntMember(const rapidjson::Value &v, const char *name, int &out) {
    if (!v.IsObject() || !v.HasMember(name) || !v[name].IsInt()) return false;
    out = v[name].GetInt();
    return true;
}

static bool GetFloatMember(const rapidjson::Value &v, const char *name, float &out) {
    if (!v.IsObject() || !v.HasMember(name) || !v[name].IsNumber()) return false;
    out = v[name].GetFloat();
    return true;
}

static bool GetBoolMember(const rapidjson::Value &v, const char *name, bool &out) {
    if (!v.IsObject() || !v.HasMember(name) || !v[name].IsBool()) return false;
    out = v[name].GetBool();
    return true;
}

void ModEngine::LoadConfig() {
    using namespace rapidjson;
    Document d;
    FILE *fp = fopen(MOD_CFG_PATH, "r");
    if (!fp) {
        ESP_LOGW(TAG, "LoadConfig: no config file %s, using defaults", MOD_CFG_PATH);
        return;
    }
    char readBuf[512];
    FileReadStream is(fp, readBuf, sizeof(readBuf));
    d.ParseStream(is);
    fclose(fp);

    if (d.HasParseError()) {
        ESP_LOGE(TAG, "LoadConfig: parse error, using defaults");
        return;
    }

    if (!d.IsObject()) {
        ESP_LOGE(TAG, "LoadConfig: root is not an object, using defaults");
        return;
    }

    for (int i = 0; i < 2; i++) {
        char key[8];
        snprintf(key, sizeof(key), "lfo%d", i + 1);
        if (!d.HasMember(key) || !d[key].IsObject()) continue;
        const Value &lfo = d[key];
        GetIntMember(lfo, "shape", lfoShape[i]);
        GetFloatMember(lfo, "rate", lfoRate[i]);
        GetFloatMember(lfo, "amp", lfoAmplitude[i]);
        GetIntMember(lfo, "cvSlot", lfoCVSlot[i]);
        GetBoolMember(lfo, "sync", lfoSync[i]);
    }

    if (d.HasMember("ccSlots") && d["ccSlots"].IsArray()) {
        const Value &ccArr = d["ccSlots"];
        for (SizeType i = 0; i < ccArr.Size() && i < 8; i++) {
            const Value &slot = ccArr[i];
            if (!slot.IsObject()) continue;
            GetIntMember(slot, "cc", dynCC[i]);
            GetIntMember(slot, "chan", dynChan[i]);
            GetIntMember(slot, "cvSlot", dynTargetSlot[i]);
        }
    }

    // Tempo is documented as part of the saved schema (doc/modulation-system.md).
    if (d.HasMember("tempo") && d["tempo"].IsObject()) {
        const Value &tm = d["tempo"];
        GetFloatMember(tm, "bpm", tempoBpm);
        int src = 0;
        if (GetIntMember(tm, "source", src)) {
            tempoSource = (src == 1) ? SP::HELPERS::ctagTempo::Source::MIDI_CLOCK
                                     : SP::HELPERS::ctagTempo::Source::INTERNAL;
        }
    }

    for (int i = 0; i < 2; i++) {
        char key[8];
        snprintf(key, sizeof(key), "seq%d", i);
        if (!d.HasMember(key) || !d[key].IsObject()) continue;
        const Value &sq = d[key];
        if (sq.HasMember("steps") && sq["steps"].IsArray()) {
            const Value &sarr = sq["steps"];
            for (SizeType j = 0; j < sarr.Size() && j < 16; j++) {
                float v = 0.5f;
                if (!sarr[j].IsNumber()) continue;
                v = sarr[j].GetFloat();
                sequencer[i].SetStep((int)j, v);
            }
        }
        float f;
        int n;
        if (GetFloatMember(sq, "stepLength", f)) sequencer[i].SetStepLength(f);
        if (GetIntMember(sq, "direction", n)) sequencer[i].SetDirection((SP::HELPERS::ctagSeq16::Direction)n);
        if (GetFloatMember(sq, "slew", f)) sequencer[i].SetSlew(f);
        if (GetIntMember(sq, "cvSlot", n)) sequencer[i].SetCVSlot(n);
        if (GetIntMember(sq, "trigSlot", n)) sequencer[i].SetTrigSlot(n);
    }

    for (int i = 0; i < 2; i++) {
        char key[8];
        snprintf(key, sizeof(key), "gate%d", i);
        if (!d.HasMember(key) || !d[key].IsObject()) continue;
        const Value &gt = d[key];
        if (gt.HasMember("enabled") && gt["enabled"].IsArray()) {
            const Value &earr = gt["enabled"];
            for (SizeType j = 0; j < earr.Size() && j < 16; j++) {
                if (!earr[j].IsBool()) continue;
                gate[i].SetStepEnabled((int)j, earr[j].GetBool());
            }
        }
        if (gt.HasMember("probs") && gt["probs"].IsArray()) {
            const Value &parr = gt["probs"];
            for (SizeType j = 0; j < parr.Size() && j < 16; j++) {
                if (!parr[j].IsNumber()) continue;
                int p = parr[j].GetInt();
                if (p < 0) p = 0;
                if (p > 100) p = 100;
                gate[i].SetStepProbability((int)j, (uint8_t)p);
            }
        }
        float f;
        int n;
        if (GetFloatMember(gt, "stepLength", f)) gate[i].SetStepLength(f);
        if (GetIntMember(gt, "direction", n)) gate[i].SetDirection((SP::HELPERS::ctagGate16::Direction)n);
        if (GetIntMember(gt, "trigSlot", n)) gate[i].SetTrigSlot(n);
        if (GetIntMember(gt, "accentSlot", n)) gate[i].SetAccentSlot(n);
        if (GetFloatMember(gt, "swing", f)) gate[i].SetSwing(f);
        if (GetFloatMember(gt, "gateLength", f)) gate[i].SetGateLength(f);
        if (GetFloatMember(gt, "accentAmount", f)) gate[i].SetAccentAmount(f);
    }

    // Apply the loaded tempo last so the shared engine sees the final values.
    tempoEngine.SetSampleRate(MOD_TEMPO_FS, MOD_TEMPO_BLOCK);
    tempoEngine.SetBPM(tempoBpm);
    tempoEngine.SetSource(tempoSource);

    ESP_LOGI(TAG, "LoadConfig: loaded from %s", MOD_CFG_PATH);
}

} // namespace CTAG::DRIVERS
