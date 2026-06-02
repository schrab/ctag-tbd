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

#include "ctagSoundProcessorElements.hpp"
#include <iostream>
#include <cmath>
#include "stmlib/stmlib.h"

using namespace CTAG::SP;

void ctagSoundProcessorElements::Init(std::size_t blockSize, void *blockPtr) {
    knowYourself();
    model = std::make_unique<ctagSPDataModel>(id, isStereo);
    LoadPreset(0);

    reverb_buffer = (uint16_t *) heap_caps_malloc(32768 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    assert(reverb_buffer != nullptr);

    part.Init(reverb_buffer);
}

void ctagSoundProcessorElements::Process(const ProcessData &data) {
    // Write into Part's internal patch_ (Part::Process reads from patch_, not a param)
    elements::Patch* p = part.mutable_patch();
    p->exciter_envelope_shape = exciter_envelope_shape / 4095.f;
    if (cv_exciter_envelope_shape != -1)
        p->exciter_envelope_shape = fabsf(data.cv[cv_exciter_envelope_shape]);
    CONSTRAIN(p->exciter_envelope_shape, 0.f, 1.f);

    p->exciter_bow_level = exciter_bow_level / 4095.f;
    if (cv_exciter_bow_level != -1)
        p->exciter_bow_level = fabsf(data.cv[cv_exciter_bow_level]);
    CONSTRAIN(p->exciter_bow_level, 0.f, 1.f);

    p->exciter_bow_timbre = exciter_bow_timbre / 4095.f;
    if (cv_exciter_bow_timbre != -1)
        p->exciter_bow_timbre = fabsf(data.cv[cv_exciter_bow_timbre]);
    CONSTRAIN(p->exciter_bow_timbre, 0.f, 1.f);

    p->exciter_blow_level = exciter_blow_level / 4095.f;
    if (cv_exciter_blow_level != -1)
        p->exciter_blow_level = fabsf(data.cv[cv_exciter_blow_level]);
    CONSTRAIN(p->exciter_blow_level, 0.f, 1.f);

    p->exciter_blow_meta = exciter_blow_meta / 4095.f;
    if (cv_exciter_blow_meta != -1)
        p->exciter_blow_meta = fabsf(data.cv[cv_exciter_blow_meta]);
    CONSTRAIN(p->exciter_blow_meta, 0.f, 1.f);

    p->exciter_blow_timbre = exciter_blow_timbre / 4095.f;
    if (cv_exciter_blow_timbre != -1)
        p->exciter_blow_timbre = fabsf(data.cv[cv_exciter_blow_timbre]);
    CONSTRAIN(p->exciter_blow_timbre, 0.f, 1.f);

    p->exciter_strike_level = exciter_strike_level / 4095.f;
    if (cv_exciter_strike_level != -1)
        p->exciter_strike_level = fabsf(data.cv[cv_exciter_strike_level]);
    CONSTRAIN(p->exciter_strike_level, 0.f, 1.f);

    p->exciter_strike_meta = exciter_strike_meta / 4095.f;
    if (cv_exciter_strike_meta != -1)
        p->exciter_strike_meta = fabsf(data.cv[cv_exciter_strike_meta]);
    CONSTRAIN(p->exciter_strike_meta, 0.f, 1.f);

    p->exciter_strike_timbre = exciter_strike_timbre / 4095.f;
    if (cv_exciter_strike_timbre != -1)
        p->exciter_strike_timbre = fabsf(data.cv[cv_exciter_strike_timbre]);
    CONSTRAIN(p->exciter_strike_timbre, 0.f, 1.f);

    p->exciter_signature = exciter_signature / 4095.f;
    if (cv_exciter_signature != -1)
        p->exciter_signature = fabsf(data.cv[cv_exciter_signature]);
    CONSTRAIN(p->exciter_signature, 0.f, 1.f);

    p->resonator_geometry = resonator_geometry / 4095.f;
    if (cv_resonator_geometry != -1)
        p->resonator_geometry = fabsf(data.cv[cv_resonator_geometry]);
    CONSTRAIN(p->resonator_geometry, 0.f, 1.f);

    p->resonator_brightness = resonator_brightness / 4095.f;
    if (cv_resonator_brightness != -1)
        p->resonator_brightness = fabsf(data.cv[cv_resonator_brightness]);
    CONSTRAIN(p->resonator_brightness, 0.f, 1.f);

    p->resonator_damping = resonator_damping / 4095.f;
    if (cv_resonator_damping != -1)
        p->resonator_damping = fabsf(data.cv[cv_resonator_damping]);
    CONSTRAIN(p->resonator_damping, 0.f, 1.f);

    p->resonator_position = resonator_position / 4095.f;
    if (cv_resonator_position != -1)
        p->resonator_position = fabsf(data.cv[cv_resonator_position]);
    CONSTRAIN(p->resonator_position, 0.f, 1.f);

    p->resonator_modulation_frequency = (resonator_mod_freq / 4095.f * 0.5f + 0.4f) / 44100.f;
    if (cv_resonator_mod_freq != -1)
        p->resonator_modulation_frequency = (fabsf(data.cv[cv_resonator_mod_freq]) * 0.5f + 0.4f) / 44100.f;
    CONSTRAIN(p->resonator_modulation_frequency, 0.4f / 44100.f, 1.2f / 44100.f);

    p->resonator_modulation_offset = resonator_mod_offset / 4095.f * 0.15f + 0.05f;
    if (cv_resonator_mod_offset != -1)
        p->resonator_modulation_offset = fabsf(data.cv[cv_resonator_mod_offset]) * 0.15f + 0.05f;
    CONSTRAIN(p->resonator_modulation_offset, 0.05f, 0.2f);

    p->reverb_diffusion = reverb_diffusion / 4095.f * 0.3f + 0.55f;
    if (cv_reverb_diffusion != -1)
        p->reverb_diffusion = fabsf(data.cv[cv_reverb_diffusion]) * 0.3f + 0.55f;
    CONSTRAIN(p->reverb_diffusion, 0.55f, 0.85f);

    p->reverb_lp = reverb_lp / 4095.f * 0.3f + 0.5f;
    if (cv_reverb_lp != -1)
        p->reverb_lp = fabsf(data.cv[cv_reverb_lp]) * 0.3f + 0.5f;
    CONSTRAIN(p->reverb_lp, 0.5f, 0.8f);

    p->space = space / 4095.f * 1.75f;
    if (cv_space != -1)
        p->space = fabsf(data.cv[cv_space]) * 1.75f;
    CONSTRAIN(p->space, 0.f, 1.75f);

    p->modulation_frequency = modulation_frequency / 4095.f * 0.5f;
    if (cv_modulation_frequency != -1)
        p->modulation_frequency = fabsf(data.cv[cv_modulation_frequency]) * 0.5f;
    CONSTRAIN(p->modulation_frequency, 0.f, 0.5f);

    // PerformanceState
    bool gateVal = gate;
    if (trig_gate != -1)
        gateVal = data.trig[trig_gate] == 1;
    perfState.gate = gateVal;

    float freqCv = frequency / 4032.f * 96.f;
    if (cv_frequency != -1)
        freqCv += data.cv[cv_frequency] * 5.f * 12.f;
    CONSTRAIN(freqCv, 0.f, 127.f);
    perfState.note = freqCv;

    perfState.modulation = 0.f;
    perfState.strength = 0.5f;

    // Resonator model
    int rModel = resonator_model;
    if (cv_resonator_model != -1)
        rModel = (int)(floorf(data.cv[cv_resonator_model] * 3.f)) % 3;
    CONSTRAIN(rModel, 0, 2);
    part.set_resonator_model((elements::ResonatorModel)rModel);

    bool ee = easter_egg;
    if (trig_easter_egg != -1)
        ee = data.trig[trig_easter_egg] == 1;
    part.set_easter_egg(ee);

    // Generate buffers from stereo input
    float blow_in[bufSz], strike_in[bufSz], out[bufSz], aux[bufSz];
    for (int i = 0; i < bufSz; i++) {
        strike_in[i] = data.buf[i * 2];
        blow_in[i] = data.buf[i * 2 + 1];
    }

    part.Process(perfState, blow_in, strike_in, out, aux, bufSz);

    for (int i = 0; i < bufSz; i++) {
        data.buf[i * 2] = out[i] * 0.125f;
        data.buf[i * 2 + 1] = aux[i] * 0.125f;
    }
}

ctagSoundProcessorElements::~ctagSoundProcessorElements() {
    heap_caps_free(reverb_buffer);
}

void ctagSoundProcessorElements::knowYourself() {
    pMapPar.emplace("frequency", [&](const int val) { frequency = val; });
    pMapCv.emplace("frequency", [&](const int val) { cv_frequency = val; });
    pMapPar.emplace("resonator_model", [&](const int val) { resonator_model = val; });
    pMapCv.emplace("resonator_model", [&](const int val) { cv_resonator_model = val; });
    pMapPar.emplace("exciter_envelope_shape", [&](const int val) { exciter_envelope_shape = val; });
    pMapCv.emplace("exciter_envelope_shape", [&](const int val) { cv_exciter_envelope_shape = val; });
    pMapPar.emplace("exciter_bow_level", [&](const int val) { exciter_bow_level = val; });
    pMapCv.emplace("exciter_bow_level", [&](const int val) { cv_exciter_bow_level = val; });
    pMapPar.emplace("exciter_bow_timbre", [&](const int val) { exciter_bow_timbre = val; });
    pMapCv.emplace("exciter_bow_timbre", [&](const int val) { cv_exciter_bow_timbre = val; });
    pMapPar.emplace("exciter_blow_level", [&](const int val) { exciter_blow_level = val; });
    pMapCv.emplace("exciter_blow_level", [&](const int val) { cv_exciter_blow_level = val; });
    pMapPar.emplace("exciter_blow_meta", [&](const int val) { exciter_blow_meta = val; });
    pMapCv.emplace("exciter_blow_meta", [&](const int val) { cv_exciter_blow_meta = val; });
    pMapPar.emplace("exciter_blow_timbre", [&](const int val) { exciter_blow_timbre = val; });
    pMapCv.emplace("exciter_blow_timbre", [&](const int val) { cv_exciter_blow_timbre = val; });
    pMapPar.emplace("exciter_strike_level", [&](const int val) { exciter_strike_level = val; });
    pMapCv.emplace("exciter_strike_level", [&](const int val) { cv_exciter_strike_level = val; });
    pMapPar.emplace("exciter_strike_meta", [&](const int val) { exciter_strike_meta = val; });
    pMapCv.emplace("exciter_strike_meta", [&](const int val) { cv_exciter_strike_meta = val; });
    pMapPar.emplace("exciter_strike_timbre", [&](const int val) { exciter_strike_timbre = val; });
    pMapCv.emplace("exciter_strike_timbre", [&](const int val) { cv_exciter_strike_timbre = val; });
    pMapPar.emplace("exciter_signature", [&](const int val) { exciter_signature = val; });
    pMapCv.emplace("exciter_signature", [&](const int val) { cv_exciter_signature = val; });
    pMapPar.emplace("resonator_geometry", [&](const int val) { resonator_geometry = val; });
    pMapCv.emplace("resonator_geometry", [&](const int val) { cv_resonator_geometry = val; });
    pMapPar.emplace("resonator_brightness", [&](const int val) { resonator_brightness = val; });
    pMapCv.emplace("resonator_brightness", [&](const int val) { cv_resonator_brightness = val; });
    pMapPar.emplace("resonator_damping", [&](const int val) { resonator_damping = val; });
    pMapCv.emplace("resonator_damping", [&](const int val) { cv_resonator_damping = val; });
    pMapPar.emplace("resonator_position", [&](const int val) { resonator_position = val; });
    pMapCv.emplace("resonator_position", [&](const int val) { cv_resonator_position = val; });
    pMapPar.emplace("resonator_mod_freq", [&](const int val) { resonator_mod_freq = val; });
    pMapCv.emplace("resonator_mod_freq", [&](const int val) { cv_resonator_mod_freq = val; });
    pMapPar.emplace("resonator_mod_offset", [&](const int val) { resonator_mod_offset = val; });
    pMapCv.emplace("resonator_mod_offset", [&](const int val) { cv_resonator_mod_offset = val; });
    pMapPar.emplace("reverb_diffusion", [&](const int val) { reverb_diffusion = val; });
    pMapCv.emplace("reverb_diffusion", [&](const int val) { cv_reverb_diffusion = val; });
    pMapPar.emplace("reverb_lp", [&](const int val) { reverb_lp = val; });
    pMapCv.emplace("reverb_lp", [&](const int val) { cv_reverb_lp = val; });
    pMapPar.emplace("space", [&](const int val) { space = val; });
    pMapCv.emplace("space", [&](const int val) { cv_space = val; });
    pMapPar.emplace("modulation_frequency", [&](const int val) { modulation_frequency = val; });
    pMapCv.emplace("modulation_frequency", [&](const int val) { cv_modulation_frequency = val; });
    pMapPar.emplace("gate", [&](const int val) { gate = val; });
    pMapTrig.emplace("gate", [&](const int val) { trig_gate = val; });
    pMapPar.emplace("easter_egg", [&](const int val) { easter_egg = val; });
    pMapTrig.emplace("easter_egg", [&](const int val) { trig_easter_egg = val; });
    isStereo = true;
    id = "Elements";
}
