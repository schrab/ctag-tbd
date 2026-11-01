// Copyright 2026 Rubato Audio.
// SPDX-License-Identifier: MIT
//
// stmlib::Random's generator, run on a local copy of its state.
//
// stmlib::Random keeps its state in a static member, which a per-sample loop
// reloads and stores on every draw. Taking a copy at the start of the loop
// and handing it back with Commit() at the end produces exactly the same
// sequence -- as long as nothing else draws from stmlib::Random in between.

#ifndef RPLAITS_DSP_LOCAL_RANDOM_H_
#define RPLAITS_DSP_LOCAL_RANDOM_H_

#include "stmlib/utils/random.h"

namespace rplaits {

class LocalRandom {
 public:
  LocalRandom() : state_(stmlib::Random::state()) { }

  // stmlib::Random::GetWord().
  inline uint32_t GetWord() {
    state_ = state_ * 1664525L + 1013904223L;
    return state_;
  }

  // stmlib::Random::GetFloat().
  inline float GetFloat() {
    return static_cast<float>(GetWord()) / 4294967296.0f;
  }

  inline void Commit() const { stmlib::Random::Seed(state_); }

 private:
  uint32_t state_;
};

}  // namespace rplaits

#endif  // RPLAITS_DSP_LOCAL_RANDOM_H_
