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

// preset_scales copied from marbles/settings.cc (Émilie Gillet, MIT) —
// settings.cc itself is STM32-bound (ChunkStorage/flash drivers).
static const Scale preset_scales[6] = {
    // C major
    {1.0f, 12,
     {{0.0000f, 255}, {0.0833f, 16}, {0.1667f, 96}, {0.2500f, 24},
      {0.3333f, 128}, {0.4167f, 64}, {0.5000f, 8},  {0.5833f, 192},
      {0.6667f, 16},  {0.7500f, 96}, {0.8333f, 24}, {0.9167f, 128}}},
    // C minor
    {1.0f, 12,
     {{0.0000f, 255}, {0.0833f, 16}, {0.1667f, 96}, {0.2500f, 128},
      {0.3333f, 8},   {0.4167f, 64}, {0.5000f, 4},  {0.5833f, 192},
      {0.6667f, 96},  {0.7500f, 16}, {0.8333f, 128}, {0.9167f, 16}}},
    // Pentatonic
    {1.0f, 12,
     {{0.0000f, 255}, {0.0833f, 4},  {0.1667f, 96}, {0.2500f, 4},
      {0.3333f, 4},   {0.4167f, 140}, {0.5000f, 4}, {0.5833f, 192},
      {0.6667f, 4},   {0.7500f, 96}, {0.8333f, 4},  {0.9167f, 4}}},
    // Pelog
    {1.0f, 7,
     {{0.0000f, 255}, {0.1275f, 128}, {0.2625f, 32}, {0.4600f, 8},
      {0.5883f, 192}, {0.7067f, 64},  {0.8817f, 16}}},
    // Raag Bhairav That
    {1.0f, 12,
     {{0.0000f, 255}, {0.0752f, 128}, {0.1699f, 4},  {0.2630f, 4},
      {0.3219f, 128}, {0.4150f, 64},  {0.4918f, 4},  {0.5850f, 192},
      {0.6601f, 64},  {0.7549f, 4},   {0.8479f, 4},  {0.9069f, 64}}},
    // Raag Shri
    {1.0f, 12,
     {{0.0000f, 255}, {0.0752f, 4},   {0.1699f, 128}, {0.2630f, 64},
      {0.3219f, 4},   {0.4150f, 128}, {0.4918f, 4},   {0.5850f, 192},
      {0.6601f, 4},   {0.7549f, 64},  {0.8479f, 128}, {0.9069f, 4}}},
};

// ---- marbles core objects (static DRAM, ~9 KB) ----
static RandomGenerator random_generator;
static RandomStream random_stream;
static TGenerator t_generator;
static XYGenerator xy_generator;

// ---- parameter state (defaults mirror marbles.cc power-on settings) ----
static int tModel = T_GENERATOR_MODEL_COMPLEMENTARY_BERNOULLI;
static int tRange = T_GENERATOR_RANGE_1X;
static float tRate = 0.0f;      // semitones around the range base rate
static float tBias = 0.5f;
static float tJitter = 0.0f;
static int tPwMean = 90;        // ~35% pulse width
static int tPwStd = 0;
static int tDejaVu = 0;         // 0=off 1=on 2=locked
static bool tExternal = false;  // t clock = MIDI clock

static int xyClockSource = 0;   // 0=T1+T2+T3 1=T1 2=T2 3=T3 4=MIDI

static int xScale = 0;          // C major
static int xRange = 2;          // ±5 V full
static int xControl = 0;        // identical
static bool xRegister = false;
static float xRegisterValue = 0.5f;
static float xSpread = 0.5f;
static float xBias = 0.5f;
static float xSteps = 0.5f;
static int xDejaVu = 0;

static int yRange = 2;
static float ySpread = 0.5f;
static float yBias = 0.5f;
static float ySteps = 0.5f;
static int yDivider = 11;       // 1/1
static int yDejaVu = 0;         // 0=off (upstream default), 1=on, 2=locked

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
    for (int i = 0; i < 6; ++i) {
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
    x.register_mode = xRegister;
    x.register_value = xRegisterValue;
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
    // Upstream marbles.cc hardcodes y.deja_vu = 0 / y.length = 1; the
    // switch lets Y join the shared deja-vu amount/length like t and X.
    y.deja_vu = yDejaVu == 2 ? 0.5f : (yDejaVu == 1 ? dvAmount : 0.0f);
    y.length = yDejaVu == 0 ? 1 : dvLength;
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
}

// ---- parameter setters/getters (clamped; slot indices via ctagSlotBounds) ----
static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float ClampFloat(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int MarblesEngine::GetTModel() { return tModel; }
void MarblesEngine::SetTModel(int v) { tModel = ClampInt(v, 0, 6); }
int MarblesEngine::GetTRange() { return tRange; }
void MarblesEngine::SetTRange(int v) { tRange = ClampInt(v, 0, 2); }
float MarblesEngine::GetTRate() { return tRate; }
void MarblesEngine::SetTRate(float v) { tRate = ClampFloat(v, -60.0f, 60.0f); }
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
void MarblesEngine::SetXScale(int v) { xScale = ClampInt(v, 0, 5); }
int MarblesEngine::GetXRange() { return xRange; }
void MarblesEngine::SetXRange(int v) { xRange = ClampInt(v, 0, 2); }
int MarblesEngine::GetXControl() { return xControl; }
void MarblesEngine::SetXControl(int v) { xControl = ClampInt(v, 0, 2); }
bool MarblesEngine::GetXRegister() { return xRegister; }
void MarblesEngine::SetXRegister(bool v) { xRegister = v; }
float MarblesEngine::GetXRegisterValue() { return xRegisterValue; }
void MarblesEngine::SetXRegisterValue(float v) { xRegisterValue = ClampFloat(v, 0.0f, 1.0f); }
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
int MarblesEngine::GetYDejaVu() { return yDejaVu; }
void MarblesEngine::SetYDejaVu(int v) { yDejaVu = ClampInt(v, 0, 2); }

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
    dynCC[slot_idx] = cc;
    ESP_LOGI(TAG, "Dynamic slot %d -> CC %d", slot_idx, cc);
}

void MarblesEngine::SetDynamicSlotChan(int slot_idx, int chan) {
    if (slot_idx < 0 || slot_idx >= 8) return;
    dynChan[slot_idx] = chan;
}

int MarblesEngine::GetDynamicSlotChan(int slot_idx) {
    return (slot_idx >= 0 && slot_idx < 8) ? dynChan[slot_idx] : 0;
}

namespace CTAG::DRIVERS {

void MarblesEngine_MaybeCaptureCC(uint8_t channel, uint8_t cc_num, uint8_t value) {
    if (!MarblesEngine::IsLearning()) return;
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
    m.AddMember("tRate", tRate, alloc);
    m.AddMember("tBias", tBias, alloc);
    m.AddMember("tJitter", tJitter, alloc);
    m.AddMember("tPwMean", tPwMean, alloc);
    m.AddMember("tPwStd", tPwStd, alloc);
    m.AddMember("tDejaVu", tDejaVu, alloc);
    m.AddMember("xyClockSource", xyClockSource, alloc);
    m.AddMember("xScale", xScale, alloc);
    m.AddMember("xRange", xRange, alloc);
    m.AddMember("xControl", xControl, alloc);
    m.AddMember("xRegister", xRegister, alloc);
    m.AddMember("xRegisterValue", xRegisterValue, alloc);
    m.AddMember("xSpread", xSpread, alloc);
    m.AddMember("xBias", xBias, alloc);
    m.AddMember("xSteps", xSteps, alloc);
    m.AddMember("xDejaVu", xDejaVu, alloc);
    m.AddMember("yRange", yRange, alloc);
    m.AddMember("ySpread", ySpread, alloc);
    m.AddMember("yBias", yBias, alloc);
    m.AddMember("ySteps", ySteps, alloc);
    m.AddMember("yDivider", yDivider, alloc);
    m.AddMember("yDejaVu", yDejaVu, alloc);
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
        if (GetFloatMember(m, "tRate", f)) SetTRate(f);
        if (GetFloatMember(m, "tBias", f)) SetTBias(f);
        if (GetFloatMember(m, "tJitter", f)) SetTJitter(f);
        if (GetIntMember(m, "tPwMean", n)) SetTPwMean(n);
        if (GetIntMember(m, "tPwStd", n)) SetTPwStd(n);
        if (GetIntMember(m, "tDejaVu", n)) SetTDejaVu(n);
        if (GetIntMember(m, "xyClockSource", n)) SetXYClockSource(n);
        if (GetIntMember(m, "xScale", n)) SetXScale(n);
        if (GetIntMember(m, "xRange", n)) SetXRange(n);
        if (GetIntMember(m, "xControl", n)) SetXControl(n);
        if (GetBoolMember(m, "xRegister", b)) SetXRegister(b);
        if (GetFloatMember(m, "xRegisterValue", f)) SetXRegisterValue(f);
        if (GetFloatMember(m, "xSpread", f)) SetXSpread(f);
        if (GetFloatMember(m, "xBias", f)) SetXBias(f);
        if (GetFloatMember(m, "xSteps", f)) SetXSteps(f);
        if (GetIntMember(m, "xDejaVu", n)) SetXDejaVu(n);
        if (GetIntMember(m, "yRange", n)) SetYRange(n);
        if (GetFloatMember(m, "ySpread", f)) SetYSpread(f);
        if (GetFloatMember(m, "yBias", f)) SetYBias(f);
        if (GetFloatMember(m, "ySteps", f)) SetYSteps(f);
        if (GetIntMember(m, "yDivider", n)) SetYDivider(n);
        if (GetIntMember(m, "yDejaVu", n)) SetYDejaVu(n);
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
    // Surface the clock state so a stale tRate/tRange (e.g. 4x range ->
    // 480 BPM at rate 0) is distinguishable from the 120 BPM factory default.
    const char *tclksrc = tExternal ? "MIDI" : "internal";
    float bpm = 60.0f;
    if (tRange == 0) bpm = 0.5f;
    else if (tRange == 2) bpm = 8.0f;
    bpm *= powf(2.0f, tRate / 12.0f) * 60.0f;
    ESP_LOGI(TAG, "LoadConfig: clock src=%s range=%d rate=%.2fst -> %.1f BPM",
             tclksrc, tRange, tRate, bpm);
}

} // namespace CTAG::DRIVERS
