// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT

#ifndef RPLAITS_DSP_ENGINE2_METALWORK_ENGINE_H_
#define RPLAITS_DSP_ENGINE2_METALWORK_ENGINE_H_

#include "rplaits/dsp/engine/engine.h"

namespace rplaits {

const int kMetalworkNumModes = 6;

class MetalworkEngine : public Engine {
 public:
  MetalworkEngine() { }
  ~MetalworkEngine() { }

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
  int object_;
  float phase_a_[kMetalworkNumModes];
  float phase_b_[kMetalworkNumModes];
  float amplitude_[kMetalworkNumModes];
  float strike_envelope_;
  float strike_lowpass_;

  DISALLOW_COPY_AND_ASSIGN(MetalworkEngine);
};

}  // namespace rplaits

#endif  // RPLAITS_DSP_ENGINE2_METALWORK_ENGINE_H_
