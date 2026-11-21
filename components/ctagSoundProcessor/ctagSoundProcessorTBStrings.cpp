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

#include "ctagSoundProcessorTBStrings.hpp"
#include <cmath>
#include "rings/dsp/string_synth_part.h"  // ChordVoicing() for VARG algo 6

using namespace CTAG::SP;

void ctagSoundProcessorTBStrings::Init(std::size_t blockSize, void *blockPtr) {
    knowYourself();
    model = std::make_unique<ctagSPDataModel>(id, isStereo);
    LoadPreset(0);

    void *mem = heap_caps_malloc(sizeof(rings::GuitarStringBank),
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (mem == nullptr) {
        mem = heap_caps_malloc(sizeof(rings::GuitarStringBank), MALLOC_CAP_SPIRAM);
    }
    assert(mem != nullptr);
    bank = new (mem) rings::GuitarStringBank();
    // FIR state must be in internal DRAM: a 512-tap loop touching PSRAM
    // costs ~17 cycles per tap on this core.
    const size_t cabBytes = tbd::kSpeakerIrTaps * sizeof(float);
    cab_ir_mem = (float *) heap_caps_malloc(cabBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    cab_hist_mem = (float *) heap_caps_malloc(cabBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    bank->SetCabBuffers(cab_ir_mem, cab_hist_mem);
    bank->Init(false);  // default: Sympathetic

    paramAD.SetSampleRate(44100.f / 32.f);
    paramAD.SetModeLin();
}

void ctagSoundProcessorTBStrings::Process(const ProcessData &data) {
    updateParams(data);

    rings::GuitarStringBankConfig cfg;
    cfg.structure = toneStructure;
    cfg.brightness = toneBrightness;
    cfg.damping = toneDamping;
    cfg.position = tonePosition;
    cfg.dist_type = distType;
    cfg.dist_drive = distDrive;
    cfg.dist_bias = distBias;
    cfg.dist_bass = distBass;
    cfg.dist_mid = distMid;
    cfg.dist_tone = distTone;
    cfg.dist_mix = distMix;

    float in[bufSz], out[bufSz], aux[bufSz];
    for (int i = 0; i < bufSz; ++i) in[i] = data.buf[i * 2];
    bank->Process(cfg, in, out, aux, bufSz);
    for (int i = 0; i < bufSz; ++i) {
        data.buf[i * 2] = out[i];
        data.buf[i * 2 + 1] = aux[i];
    }
}

void ctagSoundProcessorTBStrings::updateParams(const ProcessData &data) {
    // --- Model: Sympathetic (no dispersion) vs Inharmonic (dispersion) -----
    int modelSel = reson_model;
    if (cv_reson_model != -1) {
        modelSel = (int) floorf(fabsf(data.cv[cv_reson_model]) * 2.f);
    }
    CONSTRAIN(modelSel, 0, 1)
    bank->SetDispersion(modelSel == 1);

    // --- Fast pitch glide --------------------------------------------------
    float glideMs = glide / 4095.f * 50.f;
    if (cv_glide != -1) glideMs = fabsf(data.cv[cv_glide]) * 50.f;
    CONSTRAIN(glideMs, 0.f, 50.f)
    bank->SetGlideMs(glideMs);

    // --- AD envelope (modulation source) -----------------------------------
    float fAttack = eg_attack / 4095.f * 5.f;
    if (cv_eg_attack != -1) fAttack = fabsf(data.cv[cv_eg_attack]) * 5.f;
    CONSTRAIN(fAttack, 0.f, 5.f)
    paramAD.SetAttack(fAttack);
    float fDecay = eg_decay / 4095.f * 5.f;
    if (cv_eg_decay != -1) fDecay = fabsf(data.cv[cv_eg_decay]) * 5.f;
    CONSTRAIN(fDecay, 0.f, 5.f)
    paramAD.SetDecay(fDecay);

    bool triggerAD = eg_trigger;
    if (trig_eg_trigger != -1) triggerAD = data.trig[trig_eg_trigger] != 1;
    if (triggerAD && !eg_pre_trigger) paramAD.Trigger();
    eg_pre_trigger = triggerAD;
    bool loopAD = eg_loop;
    if (trig_eg_loop != -1) loopAD = data.trig[trig_eg_loop] != 1;
    paramAD.SetLoop(loopAD);
    float fAD = paramAD.Process();

    // --- String tone controls ----------------------------------------------
    toneStructure = structure / 4095.f;
    if (cv_structure != -1) toneStructure = fabsf(data.cv[cv_structure]);
    if (cv_mod_structure != -1) toneStructure += data.cv[cv_mod_structure] * mod_structure / 4095.f;
    else toneStructure += fAD * mod_structure / 4095.f;
    CONSTRAIN(toneStructure, 0.f, 1.f)

    toneBrightness = brightness / 4095.f;
    if (cv_brightness != -1) toneBrightness = fabsf(data.cv[cv_brightness]);
    if (cv_mod_brightness != -1) toneBrightness += data.cv[cv_mod_brightness] * mod_brightness / 4095.f;
    else toneBrightness += fAD * mod_brightness / 4095.f;
    CONSTRAIN(toneBrightness, 0.f, 1.f)

    toneDamping = damping / 4095.f;
    if (cv_damping != -1) toneDamping = fabsf(data.cv[cv_damping]);
    if (cv_mod_damping != -1) toneDamping += data.cv[cv_mod_damping] * mod_damping / 4095.f;
    else toneDamping += fAD * mod_damping / 4095.f;
    CONSTRAIN(toneDamping, 0.f, 1.f)

    tonePosition = position / 4095.f;
    if (cv_position != -1) tonePosition = fabsf(data.cv[cv_position]);
    if (cv_mod_position != -1) tonePosition += data.cv[cv_mod_position] * mod_position / 4095.f;
    else tonePosition += fAD * mod_position / 4095.f;
    CONSTRAIN(tonePosition, 0.f, 1.f)

    // --- Distortion ---------------------------------------------------------
    distType = dist_type;
    if (cv_dist_type != -1) distType = ((int) floorf(fabsf(data.cv[cv_dist_type]) * 6.f)) % 6;
    CONSTRAIN(distType, 0, 5)
    distDrive = dist_drive / 4095.f;
    if (cv_dist_drive != -1) distDrive = fabsf(data.cv[cv_dist_drive]);
    CONSTRAIN(distDrive, 0.f, 1.f)
    distBias = dist_bias / 4095.f;
    if (cv_dist_bias != -1) distBias = fabsf(data.cv[cv_dist_bias]);
    CONSTRAIN(distBias, 0.f, 1.f)
    distBass = dist_bass / 4095.f;
    if (cv_dist_bass != -1) distBass = fabsf(data.cv[cv_dist_bass]);
    CONSTRAIN(distBass, 0.f, 1.f)
    distMid = dist_mid / 4095.f;
    if (cv_dist_mid != -1) distMid = fabsf(data.cv[cv_dist_mid]);
    CONSTRAIN(distMid, 0.f, 1.f)
    distTone = dist_tone / 4095.f;
    if (cv_dist_tone != -1) distTone = fabsf(data.cv[cv_dist_tone]);
    CONSTRAIN(distTone, 0.f, 1.f)
    distMix = dist_mix / 4095.f;
    if (cv_dist_mix != -1) distMix = fabsf(data.cv[cv_dist_mix]);
    CONSTRAIN(distMix, 0.f, 1.f)
    float cabMix = cab_mix / 4095.f;
    if (cv_cab_mix != -1) cabMix = fabsf(data.cv[cv_cab_mix]);
    CONSTRAIN(cabMix, 0.f, 1.f)
    bank->SetCabMix(cabMix);

    // --- Note source: VARG companion, or manual CV/trig --------------------
    // vargString is published by BOTH VARG algorithms; vargVoicing is present
    // only when the consumer must expand a whole chord (not Arpeggio mode).
    int vargArp = -1;
    if (data.vargString[0] >= 0 && data.vargString[0] < 4) vargArp = 0;
    else if (data.vargString[1] >= 0 && data.vargString[1] < 4) vargArp = 1;

    // VARG requests glide only on chord changes. Manual notes use the Glide
    // knob (zero by default); each plucked string owns its glide thereafter.
    bank->SetGlideEnabled(vargArp < 0 || data.vargGlide[vargArp] != 0);
    const int pitchSlot = vargArp >= 0 ? (int)data.vargPitchSlot[vargArp] : (int)cv_frequency;
    const int gateSlot = vargArp >= 0 ? (int)data.vargGateSlot[vargArp] : (int)trig_strum_int;

    float note = frequency / 4032.f * 96.f;
    if (pitchSlot >= 0) note += data.cv[pitchSlot] * 5.f * 12.f;
    if (cv_mod_frequency != -1) note += data.cv[cv_mod_frequency] * mod_frequency / 4095.f * 48.f;
    else note += fAD * mod_frequency / 4095.f * 48.f;
    CONSTRAIN(note, 0.f, 96.f)

    bool gate = strum_int != 0;
    if (gateSlot >= 0) gate = data.trig[gateSlot] != 1;
    const bool gateEdge = gate && !lastGate;
    lastGate = gate;

    if (gateEdge) {
        const int sourceString = vargArp >= 0 ? data.vargString[vargArp] : -1;
        const int voicing = vargArp >= 0 ? data.vargVoicing[vargArp] : -1;
        const auto event = rings::RouteGuitarNote(
            sourceString, voicing, vargArp >= 0 && data.vargBurst[vargArp] != 0,
            fallbackString, vargArp >= 0 ? data.vargBurstVel[vargArp] / 100.f : 1.f,
            vargArp >= 0 ? data.vargGhost[vargArp] : 0);
        if (event.count > 1) {
            // Only block-chord companions fan out. Arpeggio publishes no
            // voicing, so it follows the same single-string path as a burst.
            const float *offsets = rings::ChordVoicing(1, voicing);
            const int n = rings::ChordSize(1);
            for (int s = 0; s < event.count; ++s) {
                const int idx = n > 0 ? (s % n) : 0;
                bank->SetNote((event.string + s) % rings::kGuitarStringCount,
                              note + offsets[idx], true, event.velocity, event.damping);
            }
        } else {
            bank->SetNote(event.string, note, true, event.velocity, event.damping);
            if (vargArp < 0) fallbackString = (fallbackString + 1) % rings::kGuitarFrettedStrings;
        }
    }
}

ctagSoundProcessorTBStrings::~ctagSoundProcessorTBStrings() {
    heap_caps_free(cab_ir_mem);
    heap_caps_free(cab_hist_mem);
    cab_ir_mem = cab_hist_mem = nullptr;
    if (bank != nullptr) {
        bank->~GuitarStringBank();
        heap_caps_free(bank);
        bank = nullptr;
    }
}

void ctagSoundProcessorTBStrings::knowYourself() {
    // sectionCpp0
    pMapPar.emplace("reson_model", [&](const int val) { reson_model = val; });
    pMapCv.emplace("reson_model", [&](const int val) { cv_reson_model = val; });
    pMapPar.emplace("frequency", [&](const int val) { frequency = val; });
    pMapCv.emplace("frequency", [&](const int val) { cv_frequency = val; });
    pMapPar.emplace("structure", [&](const int val) { structure = val; });
    pMapCv.emplace("structure", [&](const int val) { cv_structure = val; });
    pMapPar.emplace("brightness", [&](const int val) { brightness = val; });
    pMapCv.emplace("brightness", [&](const int val) { cv_brightness = val; });
    pMapPar.emplace("damping", [&](const int val) { damping = val; });
    pMapCv.emplace("damping", [&](const int val) { cv_damping = val; });
    pMapPar.emplace("position", [&](const int val) { position = val; });
    pMapCv.emplace("position", [&](const int val) { cv_position = val; });
    pMapPar.emplace("glide", [&](const int val) { glide = val; });
    pMapCv.emplace("glide", [&](const int val) { cv_glide = val; });
    pMapPar.emplace("strum_int", [&](const int val) { strum_int = val; });
    pMapTrig.emplace("strum_int", [&](const int val) { trig_strum_int = val; });
    pMapPar.emplace("eg_trigger", [&](const int val) { eg_trigger = val; });
    pMapTrig.emplace("eg_trigger", [&](const int val) { trig_eg_trigger = val; });
    pMapPar.emplace("eg_loop", [&](const int val) { eg_loop = val; });
    pMapTrig.emplace("eg_loop", [&](const int val) { trig_eg_loop = val; });
    pMapPar.emplace("eg_attack", [&](const int val) { eg_attack = val; });
    pMapCv.emplace("eg_attack", [&](const int val) { cv_eg_attack = val; });
    pMapPar.emplace("eg_decay", [&](const int val) { eg_decay = val; });
    pMapCv.emplace("eg_decay", [&](const int val) { cv_eg_decay = val; });
    pMapPar.emplace("mod_structure", [&](const int val) { mod_structure = val; });
    pMapCv.emplace("mod_structure", [&](const int val) { cv_mod_structure = val; });
    pMapPar.emplace("mod_brightness", [&](const int val) { mod_brightness = val; });
    pMapCv.emplace("mod_brightness", [&](const int val) { cv_mod_brightness = val; });
    pMapPar.emplace("mod_frequency", [&](const int val) { mod_frequency = val; });
    pMapCv.emplace("mod_frequency", [&](const int val) { cv_mod_frequency = val; });
    pMapPar.emplace("mod_damping", [&](const int val) { mod_damping = val; });
    pMapCv.emplace("mod_damping", [&](const int val) { cv_mod_damping = val; });
    pMapPar.emplace("mod_position", [&](const int val) { mod_position = val; });
    pMapCv.emplace("mod_position", [&](const int val) { cv_mod_position = val; });
    pMapPar.emplace("dist_type", [&](const int val) { dist_type = val; });
    pMapCv.emplace("dist_type", [&](const int val) { cv_dist_type = val; });
    pMapPar.emplace("dist_drive", [&](const int val) { dist_drive = val; });
    pMapCv.emplace("dist_drive", [&](const int val) { cv_dist_drive = val; });
    pMapPar.emplace("dist_bias", [&](const int val) { dist_bias = val; });
    pMapCv.emplace("dist_bias", [&](const int val) { cv_dist_bias = val; });
    pMapPar.emplace("dist_bass", [&](const int val) { dist_bass = val; });
    pMapCv.emplace("dist_bass", [&](const int val) { cv_dist_bass = val; });
    pMapPar.emplace("dist_mid", [&](const int val) { dist_mid = val; });
    pMapCv.emplace("dist_mid", [&](const int val) { cv_dist_mid = val; });
    pMapPar.emplace("dist_tone", [&](const int val) { dist_tone = val; });
    pMapCv.emplace("dist_tone", [&](const int val) { cv_dist_tone = val; });
    pMapPar.emplace("dist_mix", [&](const int val) { dist_mix = val; });
    pMapCv.emplace("dist_mix", [&](const int val) { cv_dist_mix = val; });
    pMapPar.emplace("cab_mix", [&](const int val) { cab_mix = val; });
    pMapCv.emplace("cab_mix", [&](const int val) { cv_cab_mix = val; });
    isStereo = true;
    id = "TBStrings";
    // sectionCpp0
}
