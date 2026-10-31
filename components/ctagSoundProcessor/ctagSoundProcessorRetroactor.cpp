/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de
(c) 2020/2021 by Robert Manzke. All rights reserved.

(c) 2021 for the "Retroactor"-Plugin by Mathias Brüssel
The idea of Retroactor is to have a simple yet flexible to control feedback / drone-noise generator.
The plugin uses a wavefolder, as diode-ladder filter and a resonant combfilter by Carlos Laguna Ruiz implemented in his VULT language,
for more details on the topic please look here: https://github.com/modlfo/vult

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

The CTAG TBD hardware design is released under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0).
Details here: https://creativecommons.org/licenses/by-nc-sa/4.0/

CTAG TBD is provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.
***************/

#include "ctagSoundProcessorRetroactor.hpp"
#include "plaits/dsp/engine/engine.h"

// --- VULT "Library for TBD" ---
// #include "./vult/vult_formantor.cpp"  // Already defined in ctagSoundProcessorFormantor, so to avoid double defines we omit it here!
// #include "./vult/vultin.cpp"          // Already defined in ctagSoundProcessorFormantor, so to avoid double defines we omit it here!

using namespace CTAG::SP;

#define GATE_HIGH_NEW       2
#define GATE_HIGH           1
#define GATE_LOW            0

// --- Replace function-call of frequency-conversion with macro for increasing speed just a bit ---
#define noteToFreq(incoming_note) (HELPERS::fastpow2 ((incoming_note - 69.f) / 12.f) *440.f)

// --- Additional Macro for automated parameter evaluations ---
#define rescaleMinMax(inname, out_min, out_max) (inname * (out_max-out_min)+out_min) // Rescale random values from 0...1 to needed output
#define MK_PITCH_PAR(outname, inname)     float outname = inname; if(cv_##inname != -1) outname += data.cv[cv_##inname]*60.f;
#define MK_FREQ_PAR(outname, inname) float outname = (inname+2048)/4095.f; if(cv_##inname != -1) outname = data.cv[cv_##inname];
#define MK_TRIG_PAR(outname, inname) int outname = process_param_trig(data, trig_##inname, inname, e_##inname);
#define MK_ADEG_PAR(outname, inname) int outname = process_param_trig(data, trig_##inname, inname, e_##inname, 2);

// --- Process trigger signals and keep their state internally ---
inline int ctagSoundProcessorRetroactor::process_param_trig(const ProcessData &data, int trig_myparm, int my_parm, int enum_trigger_id, int gate_type = 0 )
{
  int trig_status = 0;

  if(trig_myparm != -1)       // Trigger given via CV/Gate or button?
  {
    trig_status = (data.trig[trig_myparm]==0); // HIGH is 0, so we negate for boolean logic
    if( gate_type == 1 )
      return(trig_status);

    if(trig_status)    // Statuschange from HIGH to LOW or LOW to HIGH? Startup-Status for prev_trig_state is -1, so first change is always new
    {
      if( low_reached[enum_trigger_id] )    // We had a trigger low before the new trigger high
      {
        if (prev_trig_state[enum_trigger_id] == GATE_LOW || gate_type==2 )   // Toggle or AD EG Trigger...
        {
          prev_trig_state[enum_trigger_id] = GATE_HIGH;       // Remember status for next round
          low_reached[enum_trigger_id] = false;
          return (GATE_HIGH_NEW);           // New trigger
        }
        else        // previous status was high!
        {
          prev_trig_state[enum_trigger_id] = GATE_LOW;       // Remember status for next round
          low_reached[enum_trigger_id] = false;
          return (GATE_LOW);           // New trigger
        }
      }
    }
    else
      low_reached[enum_trigger_id] = true;
  }
  else                        // We may have a trigger set by activating the button via the GUI
  {
    if (my_parm != prev_trig_state[enum_trigger_id])    // Statuschange from HIGH to LOW or LOW to HIGH?
    {
      prev_trig_state[enum_trigger_id] = my_parm;       // Remember status
      if(my_parm != 0)                   // LOW if 0
        return (GATE_HIGH_NEW);          // New trigger
      else
        return (GATE_LOW);              // Trigger released
    }
  }
  return(prev_trig_state[enum_trigger_id]);            // No change (1 for active, 0 for inactive)
}

void ctagSoundProcessorRetroactor::Process(const ProcessData &data)
{
float f_val_result = 0.f;             // Sum of results for DSP-output
float rough_sine_B_processed = 0.f;   // Simply precalculate the sine-wave outside of the main loop for "distorted sine" effect

  // === Global section ===
  MK_PITCH_PAR(f_MasterPitch, MasterPitch);
  MK_FLT_PAR_ABS_SFT(f_MasterTune, MasterTune, 1200.f, 1.f);
  MK_FLT_PAR_ABS(f_Volume, Volume, 4095.f, 2.f);
  float f_current_note = f_MasterPitch + f_MasterTune;

  // === Voice section ===
  MK_PITCH_PAR(f_PitchSineA, PitchSineA);
  MK_FREQ_PAR(f_FrequSineA, FrequSineA);
  f_FrequSineA = rescaleMinMax(f_FrequSineA, -12.f, 12.f);
  MK_TRIG_PAR(t_SineBisDistorted, SineBisDistorted);
  MK_PITCH_PAR(f_PitchSineB, PitchSineB);
  MK_FREQ_PAR(f_FrequSineB, FrequSineB);
  f_FrequSineB = rescaleMinMax(f_FrequSineB, -12.f, 12.f);

  // === Feedback Loop section  ===
  MK_ADEG_PAR(t_ResetFeedbackLoop, ResetFeedbackLoop);
  MK_FLT_PAR_ABS(f_CombCut, CombCut, 4095.f, 1.f);
  MK_FLT_PAR_ABS(f_CombRes, CombRes, 4095.f, 1.f);
  MK_FLT_PAR_ABS(f_CombTone, CombTone, 4095.f, 1.f);
  MK_TRIG_PAR(t_VintageFilter, VintageFilter);
  MK_FLT_PAR_ABS(f_LadderCut, LadderCut, 4095.f, 1.f);
  MK_FLT_PAR_ABS(f_LadderRes, LadderRes, 4095.f, 5.f);
  MK_TRIG_PAR(t_IsolateFeedback, IsolateFeedback);
  MK_FLT_PAR_ABS(f_WavefolderAmount, WavefolderAmount, 4095.f, 1.f);

  // === Mixer section ===
  MK_TRIG_PAR(t_SineDisable, SineDisable);
  MK_FLT_PAR_ABS(f_VolSineA, VolSineA, 4095.f, 0.25f);
  MK_FLT_PAR_ABS(f_VolSineB, VolSineB, 4095.f, 0.175f);             // ###
  MK_FLT_PAR_ABS(f_VolSineBoost, VolSineBoost, 4095.f, 4.f);
  MK_FREQ_PAR(f_SineMix, SineMix);
  MK_FLT_PAR_ABS(f_VolFeedbackLoop, VolFeedbackLoop, 4095.f, 0.25f);
  MK_FLT_PAR_ABS(f_VolWavefolder, VolWavefolder, 4095.f, 0.25f);

  // === Precalculations for realtime DSP loop ===
  if (t_IsolateFeedback)
    f_Volume *= 1.9f;
  if (t_SineDisable)
  {
    f_VolSineA = 0.f;
    f_VolSineB = 0.f;
  }
  else
  {
    f_VolSineA *= f_VolSineBoost * f_Volume * (1.f - f_SineMix);
    f_VolSineB *= f_VolSineBoost * f_Volume * f_SineMix;
  }
  f_VolWavefolder *= f_Volume;
  f_VolFeedbackLoop *= f_Volume;

  // === Per-sample de-zipper ramps for the output gains ===
  // Block-rate gain application steps the output at every block boundary
  // (measured: a 50%-scale discontinuity per param change). Each gain now
  // moves LINEARLY from its end-of-previous-block value to this block's
  // target, so block boundaries are mathematically continuous and the
  // tracking lag is a single block (~0.7 ms).
  if (g_VolSineA < -0.5f)
  {
    g_VolSineA = f_VolSineA;
    g_VolSineB = f_VolSineB;
    g_VolFeedbackLoop = f_VolFeedbackLoop;
    g_VolWavefolder = f_VolWavefolder;
    g_CombCut = f_CombCut;
    g_CombRes = f_CombRes;
    g_CombTone = f_CombTone;
    g_LadderCut = f_LadderCut;
    g_LadderRes = f_LadderRes;
    g_WavefolderAmount = f_WavefolderAmount;
  }
  float inc_VolSineA = (f_VolSineA - g_VolSineA) / (float)loop_target;
  float inc_VolSineB = (f_VolSineB - g_VolSineB) / (float)loop_target;
  float inc_VolFeedbackLoop = (f_VolFeedbackLoop - g_VolFeedbackLoop) / (float)loop_target;
  float inc_VolWavefolder = (f_VolWavefolder - g_VolWavefolder) / (float)loop_target;
  float inc_CombCut = (f_CombCut - g_CombCut) / (float)loop_target;
  float inc_CombRes = (f_CombRes - g_CombRes) / (float)loop_target;
  float inc_CombTone = (f_CombTone - g_CombTone) / (float)loop_target;
  float inc_LadderCut = (f_LadderCut - g_LadderCut) / (float)loop_target;
  float inc_LadderRes = (f_LadderRes - g_LadderRes) / (float)loop_target;
  float inc_WavefolderAmount = (f_WavefolderAmount - g_WavefolderAmount) / (float)loop_target;

  sine_A.SetFrequency(noteToFreq(f_current_note + f_PitchSineA + f_FrequSineA));
  if( t_SineBisDistorted )
  {
    rough_sine_B.SetFrequency(noteToFreq(f_current_note + f_PitchSineB + f_FrequSineB));
    rough_sine_B_processed = rough_sine_B.Process();      // "Rough Wave B" for performance-optimisation and additional tonalities
  }
  else
    sine_B.SetFrequency(noteToFreq(f_current_note + f_PitchSineB + f_FrequSineB));

  if (t_ResetFeedbackLoop == GATE_HIGH_NEW) // [re]init the feedback-data with an arbitrary value, to start/restart feedback in case the loop has "run dry"...
    m_feedback_process = 0.77f;             // Just an arbitrary value...

  // === Realtime DSP output loop ===
  // Please note: in order to optimize performance we moved major option-logic towards the top, resulting in redundant code for the output-loop, tough!
  if (t_IsolateFeedback)       // Special feedback-line: sine-waves go directly to output / comb-filter->ladder-filter->wavefolder is fed back to comb-filter and output
  {
    if (t_VintageFilter)
    {
      if(t_SineBisDistorted )
      {
        for (uint32_t i = 0; i < loop_target; i++)
        {
          g_VolSineA += inc_VolSineA;
          g_VolSineB += inc_VolSineB;
          g_VolFeedbackLoop += inc_VolFeedbackLoop;
          g_VolWavefolder += inc_VolWavefolder;
          g_CombCut += inc_CombCut;
          g_CombRes += inc_CombRes;
          g_CombTone += inc_CombTone;
          g_LadderCut += inc_LadderCut;
          g_LadderRes += inc_LadderRes;
          g_WavefolderAmount += inc_WavefolderAmount;
          f_val_result = Rescomb_process(rescomb_data, m_feedback_process * g_VolFeedbackLoop, g_CombCut, g_CombTone, g_CombRes);
          f_val_result = Ladder_process_euler(ladder_vintage_data, f_val_result, g_LadderCut, g_LadderRes); // Euler-logic for cutoff-pitch
          m_feedback_process = m_wavefolder = Fold_do(f_val_result, g_WavefolderAmount);
          f_val_result = sine_A.Process() * g_VolSineA + rough_sine_B_processed * g_VolSineB +  f_val_result * g_VolFeedbackLoop + m_wavefolder * g_VolWavefolder;
          data.buf[i] = f_val_result;
          data.buf[++i] = f_val_result;
        }
      }
      else
      {
        for (uint32_t i = 0; i < loop_target; i++)
        {
          g_VolSineA += inc_VolSineA;
          g_VolSineB += inc_VolSineB;
          g_VolFeedbackLoop += inc_VolFeedbackLoop;
          g_VolWavefolder += inc_VolWavefolder;
          g_CombCut += inc_CombCut;
          g_CombRes += inc_CombRes;
          g_CombTone += inc_CombTone;
          g_LadderCut += inc_LadderCut;
          g_LadderRes += inc_LadderRes;
          g_WavefolderAmount += inc_WavefolderAmount;
          f_val_result = Rescomb_process(rescomb_data, m_feedback_process * g_VolFeedbackLoop, g_CombCut, g_CombTone, g_CombRes);
          f_val_result = Ladder_process_euler(ladder_vintage_data, f_val_result, g_LadderCut, g_LadderRes); // Euler-logic for cutoff-pitch
          m_feedback_process = m_wavefolder = Fold_do(f_val_result, g_WavefolderAmount);
          f_val_result = sine_A.Process() * g_VolSineA + sine_B.Process() * g_VolSineB + f_val_result * g_VolFeedbackLoop + m_wavefolder * g_VolWavefolder;
          data.buf[i] = f_val_result;
          data.buf[++i] = f_val_result;
        }
      }
    }
    else    // No Vintage-Filter (but isolated Feedback-loop)
    {
      if(t_SineBisDistorted )
      {
        for (uint32_t i = 0; i < loop_target; i++)
        {
          g_VolSineA += inc_VolSineA;
          g_VolSineB += inc_VolSineB;
          g_VolFeedbackLoop += inc_VolFeedbackLoop;
          g_VolWavefolder += inc_VolWavefolder;
          g_CombCut += inc_CombCut;
          g_CombRes += inc_CombRes;
          g_CombTone += inc_CombTone;
          g_LadderCut += inc_LadderCut;
          g_LadderRes += inc_LadderRes;
          g_WavefolderAmount += inc_WavefolderAmount;
          f_val_result = Rescomb_process(rescomb_data, m_feedback_process * g_VolFeedbackLoop, g_CombCut, g_CombTone, g_CombRes);
          f_val_result = Ladder_process(ladder_data, f_val_result, g_LadderCut, g_LadderRes);               // Heun-logic for cutoff-pitch
          m_feedback_process = m_wavefolder = Fold_do(f_val_result, g_WavefolderAmount);
          f_val_result = sine_A.Process() * g_VolSineA + rough_sine_B_processed * g_VolSineB +  f_val_result * g_VolFeedbackLoop + m_wavefolder * g_VolWavefolder;
          data.buf[i] = f_val_result;
          data.buf[++i] = f_val_result;
        }
      }
      else
      {
        for (uint32_t i = 0; i < loop_target; i++)
        {
          g_VolSineA += inc_VolSineA;
          g_VolSineB += inc_VolSineB;
          g_VolFeedbackLoop += inc_VolFeedbackLoop;
          g_VolWavefolder += inc_VolWavefolder;
          g_CombCut += inc_CombCut;
          g_CombRes += inc_CombRes;
          g_CombTone += inc_CombTone;
          g_LadderCut += inc_LadderCut;
          g_LadderRes += inc_LadderRes;
          g_WavefolderAmount += inc_WavefolderAmount;
          f_val_result = Rescomb_process(rescomb_data, m_feedback_process * g_VolFeedbackLoop, g_CombCut, g_CombTone, g_CombRes);
          f_val_result = Ladder_process(ladder_data, f_val_result, g_LadderCut, g_LadderRes);               // Heun-logic for cutoff-pitch
          m_feedback_process = m_wavefolder = Fold_do(f_val_result, g_WavefolderAmount);
          f_val_result = sine_A.Process() * g_VolSineA + sine_B.Process() * g_VolSineB +  f_val_result * g_VolFeedbackLoop + m_wavefolder * g_VolWavefolder;
          data.buf[i] = f_val_result;
          data.buf[++i] = f_val_result;
        }
      }
    }
  }
  else    // Standard feedback-line: sine-waves go to comb-filter->ladder-filter->wavefolder->output / comb-filter->ladder-filter->wavefolder is fed back to comb-filter
  {
    if (t_VintageFilter)
    {
      if(t_SineBisDistorted )
      {
        for (uint32_t i = 0; i < loop_target; i++)
        {                             // Please note: We need an iniatial feedback (sinewave or ladder-feedback) to get the feedback going first!
          g_VolSineA += inc_VolSineA;
          g_VolSineB += inc_VolSineB;
          g_VolFeedbackLoop += inc_VolFeedbackLoop;
          g_VolWavefolder += inc_VolWavefolder;
          g_CombCut += inc_CombCut;
          g_CombRes += inc_CombRes;
          g_CombTone += inc_CombTone;
          g_LadderCut += inc_LadderCut;
          g_LadderRes += inc_LadderRes;
          g_WavefolderAmount += inc_WavefolderAmount;
          f_val_result = sine_A.Process() * g_VolSineA + rough_sine_B_processed * g_VolSineB +   m_feedback_process * g_VolFeedbackLoop + m_wavefolder * g_VolWavefolder;
          f_val_result = Rescomb_process(rescomb_data, f_val_result, g_CombCut, g_CombTone, g_CombRes);
          f_val_result = m_feedback_process = Ladder_process_euler(ladder_vintage_data, f_val_result, g_LadderCut, g_LadderRes); // Euler-logic for cutoff-pitch
          f_val_result = m_wavefolder = Fold_do(f_val_result, g_WavefolderAmount);
          data.buf[i] = f_val_result;
          data.buf[++i] = f_val_result;
        }
      }
      else
      {
        for (uint32_t i = 0; i < loop_target; i++)
        {                             // Please note: We need an iniatial feedback (sinewave or ladder-feedback) to get the feedback going first!
          g_VolSineA += inc_VolSineA;
          g_VolSineB += inc_VolSineB;
          g_VolFeedbackLoop += inc_VolFeedbackLoop;
          g_VolWavefolder += inc_VolWavefolder;
          g_CombCut += inc_CombCut;
          g_CombRes += inc_CombRes;
          g_CombTone += inc_CombTone;
          g_LadderCut += inc_LadderCut;
          g_LadderRes += inc_LadderRes;
          g_WavefolderAmount += inc_WavefolderAmount;
          f_val_result = sine_A.Process() * g_VolSineA + sine_B.Process() * g_VolSineB +   m_feedback_process * g_VolFeedbackLoop + m_wavefolder * g_VolWavefolder;
          f_val_result = Rescomb_process(rescomb_data, f_val_result, g_CombCut, g_CombTone, g_CombRes);
          f_val_result = m_feedback_process = Ladder_process_euler(ladder_vintage_data, f_val_result, g_LadderCut, g_LadderRes); // Euler-logic for cutoff-pitch
          f_val_result = m_wavefolder = Fold_do(f_val_result, g_WavefolderAmount);
          data.buf[i] = f_val_result;
          data.buf[++i] = f_val_result;
        }
      }
    }
    else  // Vintage Filter and normal feedback-loop
    {
      if(t_SineBisDistorted )
      {
        for (uint32_t i = 0; i < loop_target; i++)
        {                             // Please note: We need an iniatial feedback (sinewave or ladder-feedback) to get the feedback going first!
          g_VolSineA += inc_VolSineA;
          g_VolSineB += inc_VolSineB;
          g_VolFeedbackLoop += inc_VolFeedbackLoop;
          g_VolWavefolder += inc_VolWavefolder;
          g_CombCut += inc_CombCut;
          g_CombRes += inc_CombRes;
          g_CombTone += inc_CombTone;
          g_LadderCut += inc_LadderCut;
          g_LadderRes += inc_LadderRes;
          g_WavefolderAmount += inc_WavefolderAmount;
          f_val_result = sine_A.Process() * g_VolSineA + rough_sine_B_processed * g_VolSineB + m_feedback_process * g_VolFeedbackLoop + m_wavefolder * g_VolWavefolder;
          f_val_result = Rescomb_process(rescomb_data, f_val_result, g_CombCut, g_CombTone, g_CombRes);
          f_val_result = m_feedback_process = Ladder_process(ladder_data, f_val_result, g_LadderCut, g_LadderRes);               // Heun-logic for cutoff-pitch
          f_val_result = m_wavefolder = Fold_do(f_val_result, g_WavefolderAmount);
          data.buf[i] = f_val_result;
          data.buf[++i] = f_val_result;
        }
      }
      else
      {
        for (uint32_t i = 0; i < loop_target; i++)
        {                             // Please note: We need an iniatial feedback (sinewave or ladder-feedback) to get the feedback going first!
          g_VolSineA += inc_VolSineA;
          g_VolSineB += inc_VolSineB;
          g_VolFeedbackLoop += inc_VolFeedbackLoop;
          g_VolWavefolder += inc_VolWavefolder;
          g_CombCut += inc_CombCut;
          g_CombRes += inc_CombRes;
          g_CombTone += inc_CombTone;
          g_LadderCut += inc_LadderCut;
          g_LadderRes += inc_LadderRes;
          g_WavefolderAmount += inc_WavefolderAmount;
          f_val_result = sine_A.Process() * g_VolSineA + sine_B.Process() * g_VolSineB + m_feedback_process * g_VolFeedbackLoop + m_wavefolder * g_VolWavefolder;
          f_val_result = Rescomb_process(rescomb_data, f_val_result, g_CombCut, g_CombTone, g_CombRes);
          f_val_result = m_feedback_process = Ladder_process(ladder_data, f_val_result, g_LadderCut, g_LadderRes);               // Heun-logic for cutoff-pitch
          f_val_result = m_wavefolder = Fold_do(f_val_result, g_WavefolderAmount);
          data.buf[i] = f_val_result;
          data.buf[++i] = f_val_result;
        }
      }
    }
  }
  g_VolSineA = f_VolSineA;
  g_VolSineB = f_VolSineB;
  g_VolFeedbackLoop = f_VolFeedbackLoop;
  g_VolWavefolder = f_VolWavefolder;
  g_CombCut = f_CombCut;
  g_CombRes = f_CombRes;
  g_CombTone = f_CombTone;
  g_LadderCut = f_LadderCut;
  g_LadderRes = f_LadderRes;
  g_WavefolderAmount = f_WavefolderAmount;
}

void ctagSoundProcessorRetroactor::Init(std::size_t blockSize, void *blockPtr)
{
  // construct internal data model
  knowYourself();
  model = std::make_unique<ctagSPDataModel>(id, isStereo);
  LoadPreset(0);

  // --- Initialize Oscillators ---
  sine_A.SetSampleRate(44100.f);
  sine_A.SetFrequency(1.f);
  rough_sine_B.SetSampleRate(44100.f/bufSz);    // "Rough Wave B" for performance-optimisation and additional tonalities
  rough_sine_B.SetFrequency(1.f);
  sine_B.SetSampleRate(44100.f);    // "Rough Wave B" for performance-optimisation and additional tonalities
  sine_B.SetFrequency(1.f);

  // --- Initialize VULT stuff ---
  Rescomb_process_init(rescomb_data);   // Modified to use heap_caps_malloc()
  assert(blockSize >= sizeof(float)*675);
  rescomb_data._inst179._inst47a.bufferptr = (float*) blockPtr;
  memset(rescomb_data._inst179._inst47a.bufferptr, 0, sizeof(float)*675);

  Ladder_process_init(ladder_data);     // Diode Ladder Filter with Heun based resonance frequency smoothing
  Ladder_process_euler_init(ladder_vintage_data); // Diode Ladder Filter with Euler based resonance frequency smoothing
}

ctagSoundProcessorRetroactor::~ctagSoundProcessorRetroactor()
{
}

void ctagSoundProcessorRetroactor::knowYourself(){
    // autogenerated code here
    // sectionCpp0
	pMapPar.emplace("MasterPitch", [&](const int val){ MasterPitch = val;});
	pMapCv.emplace("MasterPitch", [&](const int val){ cv_MasterPitch = val;});
	pMapPar.emplace("MasterTune", [&](const int val){ MasterTune = val;});
	pMapCv.emplace("MasterTune", [&](const int val){ cv_MasterTune = val;});
	pMapPar.emplace("Volume", [&](const int val){ Volume = val;});
	pMapCv.emplace("Volume", [&](const int val){ cv_Volume = val;});
	pMapPar.emplace("PitchSineA", [&](const int val){ PitchSineA = val;});
	pMapCv.emplace("PitchSineA", [&](const int val){ cv_PitchSineA = val;});
	pMapPar.emplace("FrequSineA", [&](const int val){ FrequSineA = val;});
	pMapCv.emplace("FrequSineA", [&](const int val){ cv_FrequSineA = val;});
	pMapPar.emplace("SineBisDistorted", [&](const int val){ SineBisDistorted = val;});
	pMapTrig.emplace("SineBisDistorted", [&](const int val){ trig_SineBisDistorted = val;});
	pMapPar.emplace("PitchSineB", [&](const int val){ PitchSineB = val;});
	pMapCv.emplace("PitchSineB", [&](const int val){ cv_PitchSineB = val;});
	pMapPar.emplace("FrequSineB", [&](const int val){ FrequSineB = val;});
	pMapCv.emplace("FrequSineB", [&](const int val){ cv_FrequSineB = val;});
	pMapPar.emplace("ResetFeedbackLoop", [&](const int val){ ResetFeedbackLoop = val;});
	pMapTrig.emplace("ResetFeedbackLoop", [&](const int val){ trig_ResetFeedbackLoop = val;});
	pMapPar.emplace("CombCut", [&](const int val){ CombCut = val;});
	pMapCv.emplace("CombCut", [&](const int val){ cv_CombCut = val;});
	pMapPar.emplace("CombRes", [&](const int val){ CombRes = val;});
	pMapCv.emplace("CombRes", [&](const int val){ cv_CombRes = val;});
	pMapPar.emplace("CombTone", [&](const int val){ CombTone = val;});
	pMapCv.emplace("CombTone", [&](const int val){ cv_CombTone = val;});
	pMapPar.emplace("LadderCut", [&](const int val){ LadderCut = val;});
	pMapCv.emplace("LadderCut", [&](const int val){ cv_LadderCut = val;});
	pMapPar.emplace("LadderRes", [&](const int val){ LadderRes = val;});
	pMapCv.emplace("LadderRes", [&](const int val){ cv_LadderRes = val;});
	pMapPar.emplace("VintageFilter", [&](const int val){ VintageFilter = val;});
	pMapTrig.emplace("VintageFilter", [&](const int val){ trig_VintageFilter = val;});
	pMapPar.emplace("WavefolderAmount", [&](const int val){ WavefolderAmount = val;});
	pMapCv.emplace("WavefolderAmount", [&](const int val){ cv_WavefolderAmount = val;});
	pMapPar.emplace("IsolateFeedback", [&](const int val){ IsolateFeedback = val;});
	pMapTrig.emplace("IsolateFeedback", [&](const int val){ trig_IsolateFeedback = val;});
	pMapPar.emplace("SineDisable", [&](const int val){ SineDisable = val;});
	pMapTrig.emplace("SineDisable", [&](const int val){ trig_SineDisable = val;});
	pMapPar.emplace("VolSineBoost", [&](const int val){ VolSineBoost = val;});
	pMapCv.emplace("VolSineBoost", [&](const int val){ cv_VolSineBoost = val;});
	pMapPar.emplace("VolSineA", [&](const int val){ VolSineA = val;});
	pMapCv.emplace("VolSineA", [&](const int val){ cv_VolSineA = val;});
	pMapPar.emplace("VolSineB", [&](const int val){ VolSineB = val;});
	pMapCv.emplace("VolSineB", [&](const int val){ cv_VolSineB = val;});
	pMapPar.emplace("SineMix", [&](const int val){ SineMix = val;});
	pMapCv.emplace("SineMix", [&](const int val){ cv_SineMix = val;});
	pMapPar.emplace("VolFeedbackLoop", [&](const int val){ VolFeedbackLoop = val;});
	pMapCv.emplace("VolFeedbackLoop", [&](const int val){ cv_VolFeedbackLoop = val;});
	pMapPar.emplace("VolWavefolder", [&](const int val){ VolWavefolder = val;});
	pMapCv.emplace("VolWavefolder", [&](const int val){ cv_VolWavefolder = val;});
	isStereo = true;
	id = "Retroactor";
	// sectionCpp0
}