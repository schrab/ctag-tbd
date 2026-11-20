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
#include <iostream>
#include <cmath>
#include "stmlib/stmlib.h"
#include "helpers/varg_chord.h"


using namespace CTAG::SP;

namespace {
// Model selector 0..3: Sympathetic / Inharmonic / Quantized (classic
// single-note string resonators) and the plucked-string guitar chord engine.
constexpr int kClassicModelCount = 3;
constexpr int kModelCount = 4;
constexpr rings::ResonatorModel kClassicModels[kClassicModelCount] = {
    rings::RESONATOR_MODEL_SYMPATHETIC_STRING,
    rings::RESONATOR_MODEL_STRING,
    rings::RESONATOR_MODEL_SYMPATHETIC_STRING_QUANTIZED,
};
}  // namespace


void ctagSoundProcessorTBStrings::Init(std::size_t blockSize, void *blockPtr) {
    knowYourself();
    model = std::make_unique<ctagSPDataModel>(id, isStereo);
    LoadPreset(0);

    /* this doesn't fit in the available block
    assert(blockSize >= 32768 * sizeof(uint16_t));
    reverb_buffer = (uint16_t *) blockPtr;
     */

    reverb_buffer = (uint16_t *) heap_caps_malloc(32768 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    assert(reverb_buffer != nullptr);

    void *ssMem = heap_caps_malloc(sizeof(rings::StringSynthPart), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ssMem == nullptr) {
        ssMem = heap_caps_malloc(sizeof(rings::StringSynthPart), MALLOC_CAP_SPIRAM);
    }
    assert(ssMem != nullptr);
    string_synth = new (ssMem) rings::StringSynthPart();

    // 12 Karplus-Strong delay lines for the guitar chord engine. Prefer
    // internal DRAM; PSRAM fallback has sequential access only (cacheable)
    // so it is much cheaper than the per-sample-random case that broke the
    // Elements vocoder.
    constexpr size_t kPluckBytes = rings::kStringSynthVoices
                                   * rings::kPluckedStringLength * sizeof(int16_t);
    pluck_buffer = (int16_t *) heap_caps_malloc(kPluckBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pluck_buffer == nullptr) {
        pluck_buffer = (int16_t *) heap_caps_malloc(kPluckBytes, MALLOC_CAP_SPIRAM);
    }
    assert(pluck_buffer != nullptr);

    strummer.Init(0.01f, 44100.0f / bufSz);
    part.Init(reverb_buffer);
    string_synth->Init(reverb_buffer, pluck_buffer);
    string_synth->set_plucked_strings(true);

    paramAD.SetSampleRate(44100.f / 32.f);
    paramAD.SetModeLin();
}

void ctagSoundProcessorTBStrings::Process(const ProcessData &data) {
    updateParams(data);

    // The engine follows the Model selector (0..2 = classic strings,
    // 3 = plucked-string guitar chords). VARG Chord forces the chord engine in
    // updateParams because only that engine owns the 18-voicing table.
    if (useChordEngine)
        string_synth->set_fx((rings::FxType) fxSelection);
    else
        part.set_model((rings::ResonatorModel) classicModelSelection);

    // Internal excitation only — no external audio path is ever handed to
    // the engines; the Strummer is driven purely by the strum gate flag.
    float in[bufSz], out[bufSz], aux[bufSz];
    for (int i = 0; i < bufSz; i++) {
        in[i] = data.buf[i * 2];
    }
    strummer.Process(NULL, bufSz, &performance_state);
    if (useChordEngine) {
        string_synth->Process(performance_state, patch, in, out, aux, bufSz);
    } else {
        part.Process(performance_state, patch, in, out, aux, bufSz);
    }

    for (int i = 0; i < bufSz; i++) {
        data.buf[i * 2] = out[i];
        data.buf[i * 2 + 1] = aux[i];
    }
}


void ctagSoundProcessorTBStrings::updateParams(const ProcessData &data) {
    // --- Model selector: choose engine, resonator model and FX ------------
    int modelSel = reson_model;
    if (cv_reson_model != -1) {
        modelSel = (int) floorf(data.cv[cv_reson_model] * (float) kModelCount);
    }
    CONSTRAIN(modelSel, 0, kModelCount - 1)
    modelSelection = modelSel;
    if (modelSel < kClassicModelCount) {
        classicModelSelection = kClassicModels[modelSel];
        fxSelection = rings::FX_NONE;
        useChordEngine = false;
    } else {
        classicModelSelection = rings::RESONATOR_MODEL_SYMPATHETIC_STRING;
        // One guitar model: dry plucked strings, no FX of any kind.
        fxSelection = rings::FX_NONE;
        useChordEngine = true;
    }

    // --- AD envelope -------------------------------------------------------
    float fAttack = eg_attack / 4095.f * 5.f;
    if (cv_eg_attack != -1) {
        fAttack = fabsf(data.cv[cv_eg_attack]) * 5.f;
    }
    CONSTRAIN(fAttack, 0.f, 5.f)
    paramAD.SetAttack(fAttack);

    float fDecay = eg_decay / 4095.f * 5.f;
    if (cv_eg_decay != -1) {
        fDecay = fabsf(data.cv[cv_eg_decay]) * 5.f;
    }
    CONSTRAIN(fDecay, 0.f, 5.f)
    paramAD.SetDecay(fDecay);

    bool triggerAD = eg_trigger;
    if (trig_eg_trigger != -1) {
        triggerAD = data.trig[trig_eg_trigger] != 1;
    }
    if (triggerAD && !eg_pre_trigger) {
        paramAD.Trigger();
    }
    eg_pre_trigger = triggerAD;

    bool loopAD = eg_loop;
    if (trig_eg_loop != -1) {
        loopAD = data.trig[trig_eg_loop] != 1;
    }
    paramAD.SetLoop(loopAD);

    // --- Modulation --------------------------------------------------------
    float fAD = paramAD.Process();
    if (cv_brightness != -1) {
        patch.brightness = fabsf(data.cv[cv_brightness]);
    } else {
        patch.brightness = brightness / 4095.f;
    }
    if (cv_mod_brightness != -1) {
        patch.brightness += data.cv[cv_mod_brightness] * mod_brightness / 4095.f;
    } else {
        patch.brightness += fAD * mod_brightness / 4095.f;
    }
    CONSTRAIN(patch.brightness, 0.f, 1.f);
    if (cv_damping != -1) {
        patch.damping = fabsf(data.cv[cv_damping]);
    } else {
        patch.damping = damping / 4095.f;
    }
    if (cv_mod_damping != -1) {
        patch.damping += data.cv[cv_mod_damping] * mod_damping / 4095.f;
    } else {
        patch.damping += fAD * mod_damping / 4095.f;
    }
    CONSTRAIN(patch.damping, 0.f, 1.f);
    if (cv_position != -1) {
        patch.position = fabsf(data.cv[cv_position]);
    } else {
        patch.position = position / 4095.f;
    }
    if (cv_mod_position != -1) {
        patch.position += data.cv[cv_mod_position] * mod_position / 4095.f;
    } else {
        patch.position += fAD * mod_position / 4095.f;
    }
    CONSTRAIN(patch.position, 0.f, 1.f);
    if (cv_structure != -1) {
        patch.structure = fabsf(data.cv[cv_structure]);
    } else {
        patch.structure = structure / 4095.f;
    }
    if (cv_mod_structure != -1) {
        patch.structure += data.cv[cv_mod_structure] * mod_structure / 4095.f;
    } else {
        patch.structure += fAD * mod_structure / 4095.f;
    }
    CONSTRAIN(patch.structure, 0.f, 1.f);

    // --- Distortion stage --------------------------------------------------
    // Type 0 = Off (true bypass); 1 Tube, 2 Fold, 3 Fuzz, 4 Shape,
    // 5 Chainsaw. The DSP guards on type > 0, so drive/mix have no effect
    // at Type 0. Both engines now use the same 0..5 convention.
    int dType = dist_type;
    if (cv_dist_type != -1)
        dType = (int)(floorf(data.cv[cv_dist_type] * 6.f)) % 6;
    CONSTRAIN(dType, 0, 5);
    patch.dist_type = static_cast<float>(dType);

    patch.dist_drive = dist_drive / 4095.f;
    if (cv_dist_drive != -1)
        patch.dist_drive = fabsf(data.cv[cv_dist_drive]);
    CONSTRAIN(patch.dist_drive, 0.f, 1.f);

    patch.dist_bias = dist_bias / 4095.f;
    if (cv_dist_bias != -1)
        patch.dist_bias = fabsf(data.cv[cv_dist_bias]);
    CONSTRAIN(patch.dist_bias, 0.f, 1.f);

    patch.dist_sustain = dist_sustain / 4095.f;
    if (cv_dist_sustain != -1)
        patch.dist_sustain = fabsf(data.cv[cv_dist_sustain]);
    CONSTRAIN(patch.dist_sustain, 0.f, 1.f);

    patch.dist_tone = dist_tone / 4095.f;
    if (cv_dist_tone != -1)
        patch.dist_tone = fabsf(data.cv[cv_dist_tone]);
    CONSTRAIN(patch.dist_tone, 0.f, 1.f);

    patch.dist_mix = dist_mix / 4095.f;
    if (cv_dist_mix != -1)
        patch.dist_mix = fabsf(data.cv[cv_dist_mix]);
    CONSTRAIN(patch.dist_mix, 0.f, 1.f);

    // --- VARG Chord auto-route ---------------------------------------------
    // Toggle ON: pitch, gate, voicing and stroke all come from whichever ARP
    // is running VARG Chord, via that ARP's user-configured slots. With the
    // toggle OFF, FREQ and TRIG stay fully independent manual mappings.
    int vargArp = -1;
    if (vargChord != 0) {
        if (data.vargVoicing[0] >= 0) vargArp = 0;
        else if (data.vargVoicing[1] >= 0) vargArp = 1;
    }
    const bool vargAuto = (vargArp >= 0);
    vargAutoActive = vargAuto;
    if (vargChord != 0) {
        // The toggle itself selects the guitar chord engine (the panel's
        // Model row follows it). A VARG companion additionally supplies
        // pitch/gate/voicing/stroke; without one the manual mappings apply.
        useChordEngine = true;
    }
    const int pitchSlot = vargAuto ? (int)data.vargPitchSlot[vargArp] : (int)cv_frequency;
    const int strumTrigSlot = vargAuto ? (int)data.vargGateSlot[vargArp] : (int)trig_strum_int;

    performance_state.note = frequency / 4032.f * 96.f;
    if (pitchSlot >= 0) {
        performance_state.note += data.cv[pitchSlot] * 5.f * 12.f;
    }
    CONSTRAIN(performance_state.note, 0.f, 96.f);

    if (cv_mod_frequency != -1) {
        performance_state.fm = data.cv[cv_mod_frequency] * mod_frequency / 4095.f * 48.f;
    } else {
        performance_state.fm = fAD * mod_frequency / 4095.f * 48.f;
    }
    CONSTRAIN(performance_state.fm, -48.f, 48.f);

    performance_state.tonic = 12.f + frequency / 4032.f * 60;
    CONSTRAIN(performance_state.tonic, 0.f, 60.f);

    // Internal excitation only. With internal_strum = false the Strummer
    // leaves the gate-derived strum flag untouched, so no note-change or
    // audio-onset analysis ever fires a pluck.
    performance_state.internal_exciter = true;
    performance_state.internal_note = true;
    performance_state.internal_strum = false;

    const bool strumGateEnabled = vargAuto || strum_int != 0;
    if (strumGateEnabled) {
        if (strumTrigSlot >= 0) {
            if (!strum) strum = data.trig[strumTrigSlot] != 1;
        } else {
            if (!strum) strum = 1;
        }
    }
    performance_state.strum = strum && !lastStrum;
    lastStrum = strum;
    strum = false;

    performance_state.chord = chords;
    if (cv_chords != -1) {
        performance_state.chord = (int) floorf(data.cv[cv_chords] * 11.f);
    }
    if (vargAuto) {
        // Exact progression quality from VARG's internal companion.
        performance_state.chord = data.vargVoicing[vargArp];
    } else if (cv_vargChord != -1) {
        // Manual voicing override (VARG Chord toggle OFF).
        int voicing = (int) lroundf(fabsf(data.cv[cv_vargChord])
                                    * (float)(SEQ::kVargVoicingCount - 1));
        if (voicing < 0) voicing = 0;
        if (voicing >= SEQ::kVargVoicingCount) voicing = SEQ::kVargVoicingCount - 1;
        performance_state.chord = voicing;
    }
    CONSTRAIN(performance_state.chord, 0, rings::kNumChords - 1);

    // --- Guitar strum engine -----------------------------------------------
    int strumStyleSel = strumStyle % (int) tbd::STRUM_COUNT;
    if (cv_strumStyle != -1) {
        strumStyleSel = ((int) (fabsf(data.cv[cv_strumStyle])
                                * (float) tbd::STRUM_COUNT))
                        % (int) tbd::STRUM_COUNT;
    }
    if (strumStyleSel < 0) strumStyleSel = 0;
    string_synth->set_strum_style((uint8_t) strumStyleSel);
    float strumSpreadMs = strumSpread / 4095.f * 45.f;
    if (cv_strumSpread != -1) {
        strumSpreadMs = fabsf(data.cv[cv_strumSpread]) * 45.f;
    }
    CONSTRAIN(strumSpreadMs, 0.f, 45.f)
    string_synth->set_strum_spread(
        (uint16_t) (strumSpreadMs * 44.1f));   // samples @ rings kSampleRate
    if (vargAuto) {
        string_synth->set_strum_companion(data.vargStroke[vargArp],
                                          (data.vargFlags[vargArp] & 1u) != 0);
    } else {
        string_synth->set_strum_companion(0, true);
    }

    int poly = 1 << polyphony;
    CONSTRAIN(poly, 1, 4)
    if (poly != part.polyphony()) part.set_polyphony(poly);
    if (poly != string_synth->polyphony()) string_synth->set_polyphony(poly);
}

ctagSoundProcessorTBStrings::~ctagSoundProcessorTBStrings() {
    if (string_synth != nullptr) {
        string_synth->~StringSynthPart();
        heap_caps_free(string_synth);
        string_synth = nullptr;
    }
    heap_caps_free(pluck_buffer);
    pluck_buffer = nullptr;
    heap_caps_free(reverb_buffer);
}

void ctagSoundProcessorTBStrings::knowYourself() {
    // sectionCpp0
    pMapPar.emplace("reson_model", [&](const int val) { reson_model = val; });
    pMapCv.emplace("reson_model", [&](const int val) { cv_reson_model = val; });
    pMapPar.emplace("frequency", [&](const int val) { frequency = val; });
    pMapCv.emplace("frequency", [&](const int val) { cv_frequency = val; });
    pMapPar.emplace("polyphony", [&](const int val) { polyphony = val; });
    pMapPar.emplace("structure", [&](const int val) { structure = val; });
    pMapCv.emplace("structure", [&](const int val) { cv_structure = val; });
    pMapPar.emplace("brightness", [&](const int val) { brightness = val; });
    pMapCv.emplace("brightness", [&](const int val) { cv_brightness = val; });
    pMapPar.emplace("damping", [&](const int val) { damping = val; });
    pMapCv.emplace("damping", [&](const int val) { cv_damping = val; });
    pMapPar.emplace("position", [&](const int val) { position = val; });
    pMapCv.emplace("position", [&](const int val) { cv_position = val; });
    pMapPar.emplace("chords", [&](const int val) { chords = val; });
    pMapCv.emplace("chords", [&](const int val) { cv_chords = val; });
    pMapPar.emplace("vargChord", [&](const int val) { vargChord = val; });
    pMapCv.emplace("vargChord", [&](const int val) { cv_vargChord = val; });
    pMapPar.emplace("strumStyle", [&](const int val) { strumStyle = val; });
    pMapCv.emplace("strumStyle", [&](const int val) { cv_strumStyle = val; });
    pMapPar.emplace("strumSpread", [&](const int val) { strumSpread = val; });
    pMapCv.emplace("strumSpread", [&](const int val) { cv_strumSpread = val; });
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
    pMapPar.emplace("mod_brightness", [&](const int val) { mod_brightness = val; });
    pMapCv.emplace("mod_brightness", [&](const int val) { cv_mod_brightness = val; });
    pMapPar.emplace("mod_frequency", [&](const int val) { mod_frequency = val; });
    pMapCv.emplace("mod_frequency", [&](const int val) { cv_mod_frequency = val; });
    pMapPar.emplace("mod_damping", [&](const int val) { mod_damping = val; });
    pMapCv.emplace("mod_damping", [&](const int val) { cv_mod_damping = val; });
    pMapPar.emplace("mod_structure", [&](const int val) { mod_structure = val; });
    pMapCv.emplace("mod_structure", [&](const int val) { cv_mod_structure = val; });
    pMapPar.emplace("mod_position", [&](const int val) { mod_position = val; });
    pMapCv.emplace("mod_position", [&](const int val) { cv_mod_position = val; });
    pMapPar.emplace("dist_type", [&](const int val) { dist_type = val; });
    pMapCv.emplace("dist_type", [&](const int val) { cv_dist_type = val; });
    pMapPar.emplace("dist_drive", [&](const int val) { dist_drive = val; });
    pMapCv.emplace("dist_drive", [&](const int val) { cv_dist_drive = val; });
    pMapPar.emplace("dist_bias", [&](const int val) { dist_bias = val; });
    pMapCv.emplace("dist_bias", [&](const int val) { cv_dist_bias = val; });
    pMapPar.emplace("dist_sustain", [&](const int val) { dist_sustain = val; });
    pMapCv.emplace("dist_sustain", [&](const int val) { cv_dist_sustain = val; });
    pMapPar.emplace("dist_tone", [&](const int val) { dist_tone = val; });
    pMapCv.emplace("dist_tone", [&](const int val) { cv_dist_tone = val; });
    pMapPar.emplace("dist_mix", [&](const int val) { dist_mix = val; });
    pMapCv.emplace("dist_mix", [&](const int val) { cv_dist_mix = val; });
    isStereo = true;
    id = "TBStrings";
    // sectionCpp0
}
