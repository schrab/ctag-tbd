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

using namespace CTAG::DRIVERS;

static const char *TAG = "MOD";

// CV slots: 80-87 dynamic CC mapping, 88-89 LFOs
// Slot 80 = OFFSET + 0, etc.
#define DYN_SLOT_BASE 80
#define LFO1_SLOT 88
#define LFO2_SLOT 89

float ModEngine::lfoPhase[2] = {0, 0};
float ModEngine::lfoRate[2] = {1.0f, 2.0f};
float ModEngine::lfoAmplitude[2] = {0.5f, 0.5f};
int ModEngine::lfoShape[2] = {0, 0};
int ModEngine::lfoCVSlot[2] = {LFO1_SLOT, LFO2_SLOT};
float ModEngine::lfoHold[2] = {0, 0};

int ModEngine::dynCC[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
int ModEngine::dynChan[8] = {0};
int ModEngine::dynTargetSlot[8] = {80, 81, 82, 83, 84, 85, 86, 87};

bool ModEngine::learning = false;
int ModEngine::lastLearnedSlot = -1;

void ModEngine::Init() {
    for (int i = 0; i < 8; i++) {
        dynTargetSlot[i] = DYN_SLOT_BASE + i;
        dynCC[i] = -1;
        dynChan[i] = 0;
    }
    lfoPhase[0] = 0;
    lfoPhase[1] = 0;
    ESP_LOGI(TAG, "ModEngine initialized, slots 80-89");
}

void ModEngine::Process(float *cv_buffer) {
    // Update LFOs
    for (int lfo = 0; lfo < 2; lfo++) {
        lfoPhase[lfo] += lfoRate[lfo] / 1378.0f; // block rate
        if (lfoPhase[lfo] > 1.0f) lfoPhase[lfo] -= 1.0f;

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
        if (slot >= 0 && slot < 90) {
            cv_buffer[slot] = val;
        }
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
    if (lfo >= 0 && lfo < 2 && slot >= 0 && slot < 90) lfoCVSlot[lfo] = slot;
}
int ModEngine::GetLFOShape(int lfo) { return (lfo >= 0 && lfo < 2) ? lfoShape[lfo] : 0; }
float ModEngine::GetLFORate(int lfo) { return (lfo >= 0 && lfo < 2) ? lfoRate[lfo] : 0; }
float ModEngine::GetLFOAmplitude(int lfo) { return (lfo >= 0 && lfo < 2) ? lfoAmplitude[lfo] : 0; }

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

} // namespace CTAG::DRIVERS
