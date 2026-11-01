// Copyright 2026 Dylan Bolink.
// SPDX-License-Identifier: MIT

#ifndef RPLAITS_DSP_ENGINE2_FRESHETS_FORMANT_SHAPES_H_
#define RPLAITS_DSP_ENGINE2_FRESHETS_FORMANT_SHAPES_H_

#include <stdint.h>

namespace rplaits {

const int kFormantShapeCount = 5;
const float kFormantShapeSize = 128.0f;

extern const int16_t* const kFormantShape[kFormantShapeCount];

}  // namespace rplaits

#endif  // RPLAITS_DSP_ENGINE2_FRESHETS_FORMANT_SHAPES_H_
