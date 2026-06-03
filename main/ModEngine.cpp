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
    tempoEngine.SetSampleRate(44100.0f, 32);
    
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
    char writeBuf[2048];
    FileWriteStream os(fp, writeBuf, sizeof(writeBuf));
    Writer<FileWriteStream> writer(os);
    d.Accept(writer);
    fflush(fp);
    fclose(fp);
    ESP_LOGI(TAG, "SaveConfig: written to %s", MOD_CFG_PATH);
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

    for (int i = 0; i < 2; i++) {
        char key[8];
        snprintf(key, sizeof(key), "lfo%d", i + 1);
        if (!d.HasMember(key) || !d[key].IsObject()) continue;
        const Value &lfo = d[key];
        if (lfo.HasMember("shape")) lfoShape[i] = lfo["shape"].GetInt();
        if (lfo.HasMember("rate")) lfoRate[i] = lfo["rate"].GetFloat();
        if (lfo.HasMember("amp")) lfoAmplitude[i] = lfo["amp"].GetFloat();
        if (lfo.HasMember("cvSlot")) lfoCVSlot[i] = lfo["cvSlot"].GetInt();
        if (lfo.HasMember("sync")) lfoSync[i] = lfo["sync"].GetBool();
    }

    if (d.HasMember("ccSlots") && d["ccSlots"].IsArray()) {
        const Value &ccArr = d["ccSlots"];
        for (SizeType i = 0; i < ccArr.Size() && i < 8; i++) {
            const Value &slot = ccArr[i];
            if (slot.HasMember("cc")) dynCC[i] = slot["cc"].GetInt();
            if (slot.HasMember("chan")) dynChan[i] = slot["chan"].GetInt();
            if (slot.HasMember("cvSlot")) dynTargetSlot[i] = slot["cvSlot"].GetInt();
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
                sequencer[i].SetStep((int)j, sarr[j].GetFloat());
            }
        }
        if (sq.HasMember("stepLength")) sequencer[i].SetStepLength(sq["stepLength"].GetFloat());
        if (sq.HasMember("direction")) sequencer[i].SetDirection((SP::HELPERS::ctagSeq16::Direction)sq["direction"].GetInt());
        if (sq.HasMember("slew")) sequencer[i].SetSlew(sq["slew"].GetFloat());
        if (sq.HasMember("cvSlot")) sequencer[i].SetCVSlot(sq["cvSlot"].GetInt());
        if (sq.HasMember("trigSlot")) sequencer[i].SetTrigSlot(sq["trigSlot"].GetInt());
    }

    for (int i = 0; i < 2; i++) {
        char key[8];
        snprintf(key, sizeof(key), "gate%d", i);
        if (!d.HasMember(key) || !d[key].IsObject()) continue;
        const Value &gt = d[key];
        if (gt.HasMember("enabled") && gt["enabled"].IsArray()) {
            const Value &earr = gt["enabled"];
            for (SizeType j = 0; j < earr.Size() && j < 16; j++) {
                gate[i].SetStepEnabled((int)j, earr[j].GetBool());
            }
        }
        if (gt.HasMember("probs") && gt["probs"].IsArray()) {
            const Value &parr = gt["probs"];
            for (SizeType j = 0; j < parr.Size() && j < 16; j++) {
                gate[i].SetStepProbability((int)j, (uint8_t)parr[j].GetInt());
            }
        }
        if (gt.HasMember("stepLength")) gate[i].SetStepLength(gt["stepLength"].GetFloat());
        if (gt.HasMember("direction")) gate[i].SetDirection((SP::HELPERS::ctagGate16::Direction)gt["direction"].GetInt());
        if (gt.HasMember("trigSlot")) gate[i].SetTrigSlot(gt["trigSlot"].GetInt());
        if (gt.HasMember("accentSlot")) gate[i].SetAccentSlot(gt["accentSlot"].GetInt());
        if (gt.HasMember("swing")) gate[i].SetSwing(gt["swing"].GetFloat());
        if (gt.HasMember("gateLength")) gate[i].SetGateLength(gt["gateLength"].GetFloat());
        if (gt.HasMember("gateLength")) gate[i].SetGateLength(gt["gateLength"].GetFloat());
        if (gt.HasMember("accentAmount")) gate[i].SetAccentAmount(gt["accentAmount"].GetFloat());
    }

    ESP_LOGI(TAG, "LoadConfig: loaded from %s", MOD_CFG_PATH);
}

} // namespace CTAG::DRIVERS
