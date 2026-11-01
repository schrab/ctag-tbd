// Copyright 2012 Emilie Gillet.
// Copyright 2018 Tom Burns.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT

#ifndef RPLAITS_DSP_ENGINE2_WAVETABLE_CHORD_ENGINE_H_
#define RPLAITS_DSP_ENGINE2_WAVETABLE_CHORD_ENGINE_H_

#include "rplaits/dsp/engine/engine.h"
#include "rplaits/dsp/engine2/scale_voices.h"

namespace rplaits {

class WavetableChordEngine : public Engine {
 public:
  WavetableChordEngine() { }
  ~WavetableChordEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return true; }

#if PLAITS_BUILD_EXTENDED_TZFM
  // Qualified separately from the stock Plaits catalog's CPU policy.
  virtual bool linear_tzfm_capable() const { return true; }
#endif

 private:
  ScaleVoiceBank voices_;

  DISALLOW_COPY_AND_ASSIGN(WavetableChordEngine);
};

}  // namespace rplaits

#endif  // RPLAITS_DSP_ENGINE2_WAVETABLE_CHORD_ENGINE_H_
