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

Marbles random modulator engine. The random core is Mutable Instruments
"Marbles" by Émilie Gillet (MIT license); this file replaces the STM32
hardware layer (ADC/pot polling, DAC outputs, panel UI) of marbles/marbles.cc
with parameter state, MIDI-clock input and CV/trig-buffer outputs.
***************/

#include "MarblesEngine.hpp"
#include <cmath>
#include <cstdio>
#include <atomic>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "rapidjson/document.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/writer.h"

#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "stmlib/utils/gate_flags.h"

using namespace CTAG::DRIVERS;
using namespace marbles;

static const char *TAG = "MARBLES";

// CV/trig block tick rate: 44100 Hz / 32 samples. The marbles generators
// take sr at Init() and derive all absolute rates from it, so running them
// with size==1 blocks at 1378.125 Hz keeps frequencies correct.
static constexpr float MARBLES_SR = 44100.0f / 32.0f;

// Marbles CV outputs are volts (up to ±5 V); TBD's cv_buffer convention is
// ±1.0 bipolar / 0..1 unipolar. Scale volts by 0.2 so FULL = ±1.0.
static constexpr float VOLTS_TO_CV = 0.2f;

// y_divider_ratios from marbles/marbles.cc (Émilie Gillet, MIT).
static const Ratio y_divider_ratios[12] = {
    {1, 64}, {1, 48}, {1, 32}, {1, 24}, {1, 16}, {1, 12},
    {1, 8},  {1, 6},  {1, 4},  {1, 3},  {1, 2},  {1, 1},
};

// preset_scales — the pentatonic entry is copied from marbles/settings.cc
// (Émilie Gillet, MIT; settings.cc itself is STM32-bound). The seven
// diatonic modes follow the upstream presets' format: all 12 chromatic
// degrees with per-note weights, in-scale notes weighted by tonal role
// (tonic 255, fifth 192, third/seventh 128, second/sixth 96, fourth 64 —
// the hierarchy visible in the upstream major/minor presets), out-of-scale
// notes 8. All rooted in C (0 V).
static const Scale preset_scales[8] = {
    // Ionian
    {1.0f, 12,
     {{0.0000f, 255}, {0.0833f, 8},  {0.1667f, 96}, {0.2500f, 8},
      {0.3333f, 128}, {0.4167f, 64}, {0.5000f, 8},  {0.5833f, 192},
      {0.6667f, 8},   {0.7500f, 96}, {0.8333f, 8},  {0.9167f, 128}}},
    // Dorian
    {1.0f, 12,
     {{0.0000f, 255}, {0.0833f, 8},  {0.1667f, 96}, {0.2500f, 128},
      {0.3333f, 8},   {0.4167f, 64}, {0.5000f, 8},  {0.5833f, 192},
      {0.6667f, 8},   {0.7500f, 96}, {0.8333f, 128}, {0.9167f, 8}}},
    // Phrygian
    {1.0f, 12,
     {{0.0000f, 255}, {0.0833f, 96}, {0.1667f, 8},  {0.2500f, 128},
      {0.3333f, 8},   {0.4167f, 64}, {0.5000f, 8},  {0.5833f, 192},
      {0.6667f, 96},  {0.7500f, 8},  {0.8333f, 128}, {0.9167f, 8}}},
    // Lydian
    {1.0f, 12,
     {{0.0000f, 255}, {0.0833f, 8},  {0.1667f, 96}, {0.2500f, 8},
      {0.3333f, 128}, {0.4167f, 8},  {0.5000f, 64}, {0.5833f, 192},
      {0.6667f, 8},   {0.7500f, 96}, {0.8333f, 8},  {0.9167f, 128}}},
    // Mixolydian
    {1.0f, 12,
     {{0.0000f, 255}, {0.0833f, 8},  {0.1667f, 96}, {0.2500f, 8},
      {0.3333f, 128}, {0.4167f, 64}, {0.5000f, 8},  {0.5833f, 192},
      {0.6667f, 8},   {0.7500f, 96}, {0.8333f, 128}, {0.9167f, 8}}},
    // Aeolian
    {1.0f, 12,
     {{0.0000f, 255}, {0.0833f, 8},  {0.1667f, 96}, {0.2500f, 128},
      {0.3333f, 8},   {0.4167f, 64}, {0.5000f, 8},  {0.5833f, 192},
      {0.6667f, 96},  {0.7500f, 8},  {0.8333f, 128}, {0.9167f, 8}}},
    // Locrian
    {1.0f, 12,
     {{0.0000f, 255}, {0.0833f, 96}, {0.1667f, 8},  {0.2500f, 128},
      {0.3333f, 8},   {0.4167f, 64}, {0.5000f, 192}, {0.5833f, 8},
      {0.6667f, 96},  {0.7500f, 8},  {0.8333f, 128}, {0.9167f, 8}}},
    // Pentatonic (upstream preset)
    {1.0f, 12,
     {{0.0000f, 255}, {0.0833f, 4},  {0.1667f, 96}, {0.2500f, 4},
      {0.3333f, 4},   {0.4167f, 140}, {0.5000f, 4}, {0.5833f, 192},
      {0.6667f, 4},   {0.7500f, 96}, {0.8333f, 4},  {0.9167f, 4}}},
};

// ---- marbles core objects (static DRAM, ~9 KB) ----
static RandomGenerator random_generator;
static RandomStream random_stream;
static TGenerator t_generator;
static XYGenerator xy_generator;

// ---- parameter state (defaults mirror marbles.cc power-on settings) ----
// Tempo model: BPM is the GLOBAL transport tempo (set manually or measured
// from the MIDI clock; other plugins may read GetBPM). The Marbles t-clock
// is its own thing, derived from BPM:
//
//   marbles_clock = BPM * range_multiplier(0.25/1/4) * 2^(tFine/12)
//
// The engine's t_generator expresses its clock as semitones around the 1x
// base (2 Hz = 120 BPM), so tRate is the DERIVED quantity
// tRate = 12*log2(BPM/120) + tFine and tRange keeps multiplying on top.
// The Clock Range switch therefore re-pitches Marbles without ever
// touching the BPM number.
static int tModel = T_GENERATOR_MODEL_COMPLEMENTARY_BERNOULLI;
static int tRange = T_GENERATOR_RANGE_1X;
static float bpm = 120.0f;      // global transport tempo, 20..480
static float tFine = 0.0f;      // Marbles fine trim around BPM, +-60 st
static float tRate = 0.0f;      // derived: 12*log2(bpm/120) + tFine
static float tBias = 0.5f;
static float tJitter = 0.0f;
static int tPwMean = 90;        // ~35% pulse width
static int tPwStd = 0;
static int tDejaVu = 0;         // 0=off 1=on 2=locked
static bool tExternal = false;  // t clock = MIDI clock

// MIDI-clock tempo measurement (OnMidiClock runs on the link task).
static int64_t lastMidiTickUs = 0;
static float midiBpmEma = -1.0f;  // -1 = no measurement yet

// Defined with the parameter setters below.
static float ClampFloat(float v, float lo, float hi);
static void ApplyClock();

static int xyClockSource = 0;   // 0=T1+T2+T3 1=T1 2=T2 3=T3 4=MIDI

static int xScale = 0;          // C major
static int xRange = 2;          // ±5 V full
static int xControl = 0;        // identical
static float xSpread = 0.5f;
static float xBias = 0.5f;
static float xSteps = 0.5f;
static int xDejaVu = 0;

static int yRange = 2;
static float ySpread = 0.5f;
static float yBias = 0.5f;
static float ySteps = 0.5f;
static int yDivider = 11;       // 1/1

static float dvAmount = 0.0f;
static int dvLength = 8;

static int tSlot[3] = {40, 41, 42};      // T1, T2 (clock), T3 -> trig slots
static int xSlot[3] = {98, 99, 100};     // X1..X3 -> cv slots
static int ySlot = 101;                  // Y -> cv slot

// ---- dynamic CC slots (unchanged from ModEngine) ----
#define DYN_SLOT_BASE 90
static int dynCC[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int dynChan[8] = {0};
static bool learning = false;
static int lastLearnedSlot = -1;

// ---- runtime state shared between MIDI/UI tasks and audio task ----
static std::atomic<uint32_t> pendingClockEdges{0};
static volatile bool resetRequest = false;
static stmlib::GateFlags extClockFlags = stmlib::GATE_FLAG_LOW;
static bool extGateLevel = false;
static std::atomic<uint32_t> outSnapshot{0};

// Normalize volts to -1..1 against an ABSOLUTE ±5 V ruler — independent of
// the selected output range, so switching the octave range never moves the
// UI bar (a 1 V note always sits at the same position) — and quantize to
// 7 bits. Narrow ranges therefore use only the inner part of the travel.
static uint32_t PackNormalized(float volts) {
    float n = volts / 5.0f;
    if (n < -1.0f) n = -1.0f;
    if (n > 1.0f) n = 1.0f;
    return (uint32_t)((n + 1.0f) * 63.5f) & 0x7Fu;
}

void MarblesEngine::Init() {
    for (int i = 0; i < 8; i++) {
        dynCC[i] = -1;
        dynChan[i] = 0;
    }
    learning = false;
    lastLearnedSlot = -1;

    random_generator.Init(esp_random());
    random_stream.Init(&random_generator);
    t_generator.Init(&random_stream, MARBLES_SR);
    xy_generator.Init(&random_stream, MARBLES_SR);
    for (int i = 0; i < 8; ++i) {
        xy_generator.LoadScale(i, preset_scales[i]);
    }
    pendingClockEdges.store(0, std::memory_order_relaxed);
    resetRequest = false;
    extClockFlags = stmlib::GATE_FLAG_LOW;
    extGateLevel = false;

    LoadConfig();
    ESP_LOGI(TAG, "Marbles engine initialized (cv %d/%d/%d/%d, trig %d/%d/%d)",
             xSlot[0], xSlot[1], xSlot[2], ySlot, tSlot[0], tSlot[1], tSlot[2]);
}

void MarblesEngine::OnMidiClock() {
    pendingClockEdges.fetch_add(1, std::memory_order_relaxed);
    // Measure the transport tempo from the MIDI clock (24 ticks/beat) and
    // adopt it as the global BPM — the value then also sticks when the
    // internal clock takes over again. EMA keeps the display steady.
    int64_t now = esp_timer_get_time();
    if (lastMidiTickUs != 0) {
        float ms = (float)(now - lastMidiTickUs) / 1000.0f;
        if (ms >= 2.5f && ms <= 1000.0f) {  // 2.5..1000 ms/tick = 15..6000 BPM
            float inst = 60000.0f / (ms * 24.0f);
            midiBpmEma = (midiBpmEma < 0.0f) ? inst : (0.9f * midiBpmEma + 0.1f * inst);
            bpm = ClampFloat(midiBpmEma, 20.0f, 480.0f);
            ApplyClock();
        }
    }
    lastMidiTickUs = now;
}

void MarblesEngine::OnMidiStart() {
    resetRequest = true;
}

void MarblesEngine::RequestReset() {
    resetRequest = true;
}

void MarblesEngine::Process(float *cv_buffer, uint8_t *trig_buffer) {
    // Convert queued MIDI clock edges into a gate stream at block rate.
    // Each edge holds the gate high for one block tick; edges closer than
    // one block (clock > 1378 Hz) collapse.
    uint32_t edges = pendingClockEdges.load(std::memory_order_relaxed);
    if (edges > 0) {
        pendingClockEdges.fetch_sub(1, std::memory_order_relaxed);
    }
    extGateLevel = edges > 0;
    extClockFlags = stmlib::ExtractGateFlags(extClockFlags, extGateLevel);

    bool reset = resetRequest;
    resetRequest = false;

    // --- t section (mirrors marbles.cc) ---
    t_generator.set_model(TGeneratorModel(tModel));
    t_generator.set_range(TGeneratorRange(tRange));
    t_generator.set_rate(tRate);
    t_generator.set_bias(tBias);
    t_generator.set_jitter(tJitter);
    t_generator.set_deja_vu(tDejaVu == 2 ? 0.5f : (tDejaVu == 1 ? dvAmount : 0.0f));
    t_generator.set_length(dvLength);
    t_generator.set_pulse_width_mean(float(tPwMean) / 256.0f);
    t_generator.set_pulse_width_std(float(tPwStd) / 256.0f);

    float ramp_external, ramp_master, ramp_slave0, ramp_slave1;
    bool gates[2 * 1];
    Ramps ramps;
    ramps.external = &ramp_external;
    ramps.master = &ramp_master;
    ramps.slave[0] = &ramp_slave0;
    ramps.slave[1] = &ramp_slave1;

    t_generator.Process(
        tExternal,          // use external (MIDI) clock for the t master
        &reset,
        &extClockFlags,
        ramps,
        gates,
        1);

    // --- X/Y section ---
    GroupSettings x, y;
    x.control_mode = ControlMode(xControl);
    x.voltage_range = VoltageRange(xRange % 3);
    // Register mode samples an external CV (the [O] jack on the module);
    // the BBA has no CV inputs, so it stays off like upstream's Y section.
    x.register_mode = false;
    x.register_value = 0.0f;
    x.spread = xSpread;
    x.bias = xBias;
    x.steps = xSteps;
    x.deja_vu = xDejaVu == 2 ? 0.5f : (xDejaVu == 1 ? dvAmount : 0.0f);
    x.length = dvLength;
    x.ratio.p = 1;
    x.ratio.q = 1;
    x.scale_index = xScale;

    y.control_mode = CONTROL_MODE_IDENTICAL;
    y.voltage_range = VoltageRange(yRange % 3);
    y.register_mode = false;
    y.register_value = 0.0f;
    y.spread = ySpread;
    y.bias = yBias;
    y.steps = ySteps;
    y.deja_vu = 0.0f;  // upstream marbles.cc: Y is never affected by deja vu
    y.length = 1;
    y.ratio = y_divider_ratios[yDivider];
    y.scale_index = xScale;

    ClockSource cs;
    if (xyClockSource == 4) {
        cs = CLOCK_SOURCE_EXTERNAL;
    } else {
        // T1 / T2(master) / T3 / T1+T2+T3 share the enum values 1..3, 0
        cs = xyClockSource == 0 ? CLOCK_SOURCE_INTERNAL_T1_T2_T3
                                : ClockSource(xyClockSource);
    }

    bool xy_reset = reset;

    float voltages[4 * 1];
    xy_generator.Process(
        cs, x, y,
        &xy_reset,
        &extClockFlags,    // used only when cs == CLOCK_SOURCE_EXTERNAL
        ramps,
        voltages,
        1);

    // --- write outputs into the shared buffers ---
    if (trig_buffer != nullptr) {
        if (tSlot[0] >= 0) trig_buffer[tSlot[0]] = gates[0] ? 1 : 0;      // T1
        if (tSlot[1] >= 0) trig_buffer[tSlot[1]] = ramp_master < 0.5f ? 1 : 0; // T2 = clock
        if (tSlot[2] >= 0) trig_buffer[tSlot[2]] = gates[1] ? 1 : 0;      // T3
    }
    if (cv_buffer != nullptr) {
        for (int i = 0; i < 3; i++) {
            if (xSlot[i] >= 0) cv_buffer[xSlot[i]] = voltages[i] * VOLTS_TO_CV;
        }
        if (ySlot >= 0) cv_buffer[ySlot] = voltages[3] * VOLTS_TO_CV;
    }

    // output indication snapshot for the UI (raw engine outputs, regardless
    // of slot assignment)
    uint32_t snap = (gates[0] ? 1u : 0u)
                  | ((ramp_master < 0.5f ? 1u : 0u) << 1)
                  | ((gates[1] ? 1u : 0u) << 2);
    snap |= PackNormalized(voltages[0]) << 3;
    snap |= PackNormalized(voltages[1]) << 10;
    snap |= PackNormalized(voltages[2]) << 17;
    snap |= PackNormalized(voltages[3]) << 24;
    outSnapshot.store(snap, std::memory_order_relaxed);
}

uint32_t MarblesEngine::GetOutputSnapshot() {
    return outSnapshot.load(std::memory_order_relaxed);
}

// ---- parameter setters/getters (clamped; slot indices via ctagSlotBounds) ----
static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float ClampFloat(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Re-derive the engine rate from the global BPM and the fine trim.
static void ApplyClock() {
    float st = 12.0f * log2f(bpm / 120.0f) + tFine;
    tRate = ClampFloat(st, -60.0f, 60.0f);
}

int MarblesEngine::GetTModel() { return tModel; }
void MarblesEngine::SetTModel(int v) { tModel = ClampInt(v, 0, 6); }
int MarblesEngine::GetTRange() { return tRange; }
void MarblesEngine::SetTRange(int v) { tRange = ClampInt(v, 0, 2); }
// Global transport tempo (manual entry, tap tempo, or MIDI-measured).
float MarblesEngine::GetBPM() { return bpm; }
void MarblesEngine::SetBPM(float v) {
    bpm = ClampFloat(v, 20.0f, 480.0f);
    ApplyClock();
}
// Marbles fine trim around BPM (the engine rate is derived from BPM+tFine).
float MarblesEngine::GetTRate() { return tFine; }
void MarblesEngine::SetTRate(float v) {
    tFine = ClampFloat(v, -60.0f, 60.0f);
    ApplyClock();
}
float MarblesEngine::GetTBias() { return tBias; }
void MarblesEngine::SetTBias(float v) { tBias = ClampFloat(v, 0.0f, 1.0f); }
float MarblesEngine::GetTJitter() { return tJitter; }
void MarblesEngine::SetTJitter(float v) { tJitter = ClampFloat(v, 0.0f, 1.0f); }
int MarblesEngine::GetTPwMean() { return tPwMean; }
void MarblesEngine::SetTPwMean(int v) { tPwMean = ClampInt(v, 0, 255); }
int MarblesEngine::GetTPwStd() { return tPwStd; }
void MarblesEngine::SetTPwStd(int v) { tPwStd = ClampInt(v, 0, 255); }
int MarblesEngine::GetTDejaVu() { return tDejaVu; }
void MarblesEngine::SetTDejaVu(int v) { tDejaVu = ClampInt(v, 0, 2); }
bool MarblesEngine::GetTExternal() { return tExternal; }
void MarblesEngine::SetTExternal(bool v) { tExternal = v; }

int MarblesEngine::GetXYClockSource() { return xyClockSource; }
void MarblesEngine::SetXYClockSource(int v) { xyClockSource = ClampInt(v, 0, 4); }

int MarblesEngine::GetXScale() { return xScale; }
void MarblesEngine::SetXScale(int v) { xScale = ClampInt(v, 0, 7); }
int MarblesEngine::GetXRange() { return xRange; }
void MarblesEngine::SetXRange(int v) { xRange = ClampInt(v, 0, 2); }
int MarblesEngine::GetXControl() { return xControl; }
void MarblesEngine::SetXControl(int v) { xControl = ClampInt(v, 0, 2); }
float MarblesEngine::GetXSpread() { return xSpread; }
void MarblesEngine::SetXSpread(float v) { xSpread = ClampFloat(v, 0.0f, 1.0f); }
float MarblesEngine::GetXBias() { return xBias; }
void MarblesEngine::SetXBias(float v) { xBias = ClampFloat(v, 0.0f, 1.0f); }
float MarblesEngine::GetXSteps() { return xSteps; }
void MarblesEngine::SetXSteps(float v) { xSteps = ClampFloat(v, 0.0f, 1.0f); }
int MarblesEngine::GetXDejaVu() { return xDejaVu; }
void MarblesEngine::SetXDejaVu(int v) { xDejaVu = ClampInt(v, 0, 2); }

int MarblesEngine::GetYRange() { return yRange; }
void MarblesEngine::SetYRange(int v) { yRange = ClampInt(v, 0, 2); }
float MarblesEngine::GetYSpread() { return ySpread; }
void MarblesEngine::SetYSpread(float v) { ySpread = ClampFloat(v, 0.0f, 1.0f); }
float MarblesEngine::GetYBias() { return yBias; }
void MarblesEngine::SetYBias(float v) { yBias = ClampFloat(v, 0.0f, 1.0f); }
float MarblesEngine::GetYSteps() { return ySteps; }
void MarblesEngine::SetYSteps(float v) { ySteps = ClampFloat(v, 0.0f, 1.0f); }
int MarblesEngine::GetYDivider() { return yDivider; }
void MarblesEngine::SetYDivider(int v) { yDivider = ClampInt(v, 0, 11); }

float MarblesEngine::GetDejaVuAmount() { return dvAmount; }
void MarblesEngine::SetDejaVuAmount(float v) { dvAmount = ClampFloat(v, 0.0f, 1.0f); }
int MarblesEngine::GetDejaVuLength() { return dvLength; }
void MarblesEngine::SetDejaVuLength(int v) { dvLength = ClampInt(v, 1, 16); }

int MarblesEngine::GetTSlot(int i) { return (i >= 0 && i < 3) ? tSlot[i] : -1; }
void MarblesEngine::SetTSlot(int i, int slot) {
    if (i < 0 || i >= 3) return;
    tSlot[i] = SP::HELPERS::ClampTrigSlot(slot);
}
int MarblesEngine::GetXSlot(int i) { return (i >= 0 && i < 3) ? xSlot[i] : -1; }
void MarblesEngine::SetXSlot(int i, int slot) {
    if (i < 0 || i >= 3) return;
    xSlot[i] = SP::HELPERS::ClampCVSlot(slot);
}
int MarblesEngine::GetYSlot() { return ySlot; }
void MarblesEngine::SetYSlot(int slot) { ySlot = SP::HELPERS::ClampCVSlot(slot); }

// ---- dynamic CC slots / MIDI learn (carried over from ModEngine) ----
void MarblesEngine::StartLearn() {
    learning = true;
    lastLearnedSlot = -1;
    ESP_LOGI(TAG, "MIDI Learn started");
}

void MarblesEngine::StopLearn() {
    learning = false;
    ESP_LOGI(TAG, "MIDI Learn stopped, slot %d", lastLearnedSlot);
}

bool MarblesEngine::IsLearning() { return learning; }
int MarblesEngine::GetLastLearnedSlot() { return lastLearnedSlot; }

int MarblesEngine::GetDynamicSlotCC(int slot_idx) {
    return (slot_idx >= 0 && slot_idx < 8) ? dynCC[slot_idx] : -1;
}

void MarblesEngine::SetDynamicSlotCC(int slot_idx, int cc) {
    if (slot_idx < 0 || slot_idx >= 8) return;
    if (cc < -1) cc = -1;   // -1 = unassigned slot
    if (cc > 127) cc = 127;
    dynCC[slot_idx] = cc;
    ESP_LOGI(TAG, "Dynamic slot %d -> CC %d", slot_idx, cc);
}

void MarblesEngine::SetDynamicSlotChan(int slot_idx, int chan) {
    if (slot_idx < 0 || slot_idx >= 8) return;
    if (chan < 0) chan = 0;
    if (chan > 15) chan = 15;
    dynChan[slot_idx] = chan;
}

int MarblesEngine::GetDynamicSlotChan(int slot_idx) {
    return (slot_idx >= 0 && slot_idx < 8) ? dynChan[slot_idx] : 0;
}

namespace CTAG::DRIVERS {

void MarblesEngine_MaybeCaptureCC(uint8_t channel, uint8_t cc_num, uint8_t value) {
    if (!MarblesEngine::IsLearning()) return;
    // Re-learning an already-bound CC reuses its slot instead of burning
    // the next free one (the old "next CC also mapped" behavior).
    for (int i = 0; i < 8; i++) {
        if (MarblesEngine::GetDynamicSlotCC(i) == cc_num &&
            MarblesEngine::GetDynamicSlotChan(i) == channel) {
            lastLearnedSlot = DYN_SLOT_BASE + i;
            MarblesEngine::StopLearn();
            return;
        }
    }
    for (int i = 0; i < 8; i++) {
        if (MarblesEngine::GetDynamicSlotCC(i) == -1) {
            MarblesEngine::SetDynamicSlotCC(i, cc_num);
            MarblesEngine::SetDynamicSlotChan(i, channel);
            // Record the CV slot (90+i) so a caller (e.g. the mapping page)
            // can bind the freshly learned source to a parameter.
            lastLearnedSlot = DYN_SLOT_BASE + i;
            MarblesEngine::StopLearn();
            return;
        }
    }
    ESP_LOGW(TAG, "No free dynamic CC slots");
}

void MarblesEngine_ApplyDynamicCC(uint8_t channel, uint8_t cc_num, uint8_t value, float *cv_buffer) {
    for (int i = 0; i < 8; i++) {
        if (MarblesEngine::GetDynamicSlotCC(i) == cc_num &&
            MarblesEngine::GetDynamicSlotChan(i) == (int)channel) {
            cv_buffer[DYN_SLOT_BASE + i] = value * (1.0f / 127.0f);
            return;
        }
    }
}

// ---- persistence ----
static const char *MOD_CFG_PATH = "/spiffs/data/mod-config.jsn";

void MarblesEngine::SaveConfig() {
    using namespace rapidjson;
    Document d;
    d.SetObject();
    auto &alloc = d.GetAllocator();

    Value m(kObjectType);
    m.AddMember("tExternal", tExternal, alloc);
    m.AddMember("tModel", tModel, alloc);
    m.AddMember("tRange", tRange, alloc);
    m.AddMember("bpm", bpm, alloc);
    m.AddMember("tFine", tFine, alloc);
    m.AddMember("tRate", tRate, alloc);  // derived, kept for diagnostics
    m.AddMember("tBias", tBias, alloc);
    m.AddMember("tJitter", tJitter, alloc);
    m.AddMember("tPwMean", tPwMean, alloc);
    m.AddMember("tPwStd", tPwStd, alloc);
    m.AddMember("tDejaVu", tDejaVu, alloc);
    m.AddMember("xyClockSource", xyClockSource, alloc);
    m.AddMember("xScale", xScale, alloc);
    m.AddMember("xRange", xRange, alloc);
    m.AddMember("xControl", xControl, alloc);
    m.AddMember("xSpread", xSpread, alloc);
    m.AddMember("xBias", xBias, alloc);
    m.AddMember("xSteps", xSteps, alloc);
    m.AddMember("xDejaVu", xDejaVu, alloc);
    m.AddMember("yRange", yRange, alloc);
    m.AddMember("ySpread", ySpread, alloc);
    m.AddMember("yBias", yBias, alloc);
    m.AddMember("ySteps", ySteps, alloc);
    m.AddMember("yDivider", yDivider, alloc);
    m.AddMember("dvAmount", dvAmount, alloc);
    m.AddMember("dvLength", dvLength, alloc);
    Value tArr(kArrayType);
    for (int i = 0; i < 3; i++) tArr.PushBack(tSlot[i], alloc);
    m.AddMember("tSlot", tArr, alloc);
    Value xArr(kArrayType);
    for (int i = 0; i < 3; i++) xArr.PushBack(xSlot[i], alloc);
    m.AddMember("xSlot", xArr, alloc);
    m.AddMember("ySlot", ySlot, alloc);
    d.AddMember("marbles", m, alloc);

    Value ccArr(kArrayType);
    for (int i = 0; i < 8; i++) {
        Value slot(kObjectType);
        slot.AddMember("cc", dynCC[i], alloc);
        slot.AddMember("chan", dynChan[i], alloc);
        slot.AddMember("cvSlot", DYN_SLOT_BASE + i, alloc);
        ccArr.PushBack(slot, alloc);
    }
    d.AddMember("ccSlots", ccArr, alloc);

    FILE *fp = fopen(MOD_CFG_PATH, "w");
    if (!fp) {
        ESP_LOGE(TAG, "SaveConfig: cannot open %s", MOD_CFG_PATH);
        return;
    }
    // SaveConfig runs on the UIMenu task (4 KB stack) — keep the buffer
    // modest, FileWriteStream flushes in chunks.
    char writeBuf[512];
    FileWriteStream os(fp, writeBuf, sizeof(writeBuf));
    Writer<FileWriteStream> writer(os);
    d.Accept(writer);
    fflush(fp);
    fclose(fp);
    ESP_LOGI(TAG, "SaveConfig: written to %s", MOD_CFG_PATH);
}

// mod-config.jsn lives on SPIFFS and is hand-editable, so every read is
// type-checked (RapidJSON asserts compile out under NDEBUG).
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

void MarblesEngine::LoadConfig() {
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

    if (d.HasParseError() || !d.IsObject()) {
        ESP_LOGE(TAG, "LoadConfig: parse error, using defaults");
        return;
    }

    if (d.HasMember("marbles") && d["marbles"].IsObject()) {
        const Value &m = d["marbles"];
        int n;
        float f;
        bool b;
        if (GetBoolMember(m, "tExternal", b)) SetTExternal(b);
        if (GetIntMember(m, "tModel", n)) SetTModel(n);
        if (GetIntMember(m, "tRange", n)) SetTRange(n);
        // NOTE: tRate/bpm/tFine are handled by the migration block below —
        // tRate is a derived quantity and must not feed SetTRate (the fine
        // trim) directly.
        if (GetFloatMember(m, "tBias", f)) SetTBias(f);
        if (GetFloatMember(m, "tJitter", f)) SetTJitter(f);
        if (GetIntMember(m, "tPwMean", n)) SetTPwMean(n);
        if (GetIntMember(m, "tPwStd", n)) SetTPwStd(n);
        if (GetIntMember(m, "tDejaVu", n)) SetTDejaVu(n);
        if (GetIntMember(m, "xyClockSource", n)) SetXYClockSource(n);
        if (GetIntMember(m, "xScale", n)) SetXScale(n);
        if (GetIntMember(m, "xRange", n)) SetXRange(n);
        if (GetIntMember(m, "xControl", n)) SetXControl(n);
        if (GetFloatMember(m, "xSpread", f)) SetXSpread(f);
        if (GetFloatMember(m, "xBias", f)) SetXBias(f);
        if (GetFloatMember(m, "xSteps", f)) SetXSteps(f);
        if (GetIntMember(m, "xDejaVu", n)) SetXDejaVu(n);
        if (GetIntMember(m, "yRange", n)) SetYRange(n);
        if (GetFloatMember(m, "ySpread", f)) SetYSpread(f);
        if (GetFloatMember(m, "yBias", f)) SetYBias(f);
        if (GetFloatMember(m, "ySteps", f)) SetYSteps(f);
        if (GetIntMember(m, "yDivider", n)) SetYDivider(n);
        if (GetFloatMember(m, "dvAmount", f)) SetDejaVuAmount(f);
        if (GetIntMember(m, "dvLength", n)) SetDejaVuLength(n);
        if (m.HasMember("tSlot") && m["tSlot"].IsArray()) {
            const Value &a = m["tSlot"];
            for (SizeType i = 0; i < a.Size() && i < 3; i++) {
                if (a[i].IsInt()) SetTSlot((int)i, a[i].GetInt());
            }
        }
        if (m.HasMember("xSlot") && m["xSlot"].IsArray()) {
            const Value &a = m["xSlot"];
            for (SizeType i = 0; i < a.Size() && i < 3; i++) {
                if (a[i].IsInt()) SetXSlot((int)i, a[i].GetInt());
            }
        }
        if (GetIntMember(m, "ySlot", n)) SetYSlot(n);

        // Tempo-model migration: configs from before the global-BPM split
        // stored only tRate (semitones around the range base). The old
        // effective clock at 1x was 2 Hz * 2^(tRate/12) = 120 BPM *
        // 2^(tRate/12), so that becomes the global BPM with no fine trim —
        // the new model reproduces the same clock for any persisted range.
        if (GetFloatMember(m, "bpm", f)) {
            SetBPM(f);
            if (GetFloatMember(m, "tFine", f)) SetTRate(f);
        } else if (GetFloatMember(m, "tRate", f)) {
            SetBPM(120.0f * powf(2.0f, f / 12.0f));
            tFine = 0.0f;
        }
        ApplyClock();
    }

    // ccSlots keep the ModEngine schema so an old config file carries over.
    if (d.HasMember("ccSlots") && d["ccSlots"].IsArray()) {
        const Value &ccArr = d["ccSlots"];
        for (SizeType i = 0; i < ccArr.Size() && i < 8; i++) {
            const Value &slot = ccArr[i];
            if (!slot.IsObject()) continue;
            GetIntMember(slot, "cc", dynCC[i]);
            GetIntMember(slot, "chan", dynChan[i]);
        }
    }

    ESP_LOGI(TAG, "LoadConfig: loaded from %s", MOD_CFG_PATH);
    // Surface the clock state so a migrated vs. fresh config is visible:
    // the engine rate is derived from the global BPM and the fine trim.
    const char *tclksrc = tExternal ? "MIDI" : "internal";
    ESP_LOGI(TAG, "LoadConfig: clock src=%s bpm=%.1f fine=%.2fst range=%d -> rate=%.2fst",
             tclksrc, bpm, tFine, tRange, tRate);
}

} // namespace CTAG::DRIVERS
