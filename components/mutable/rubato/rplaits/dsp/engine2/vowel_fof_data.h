// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' vowel formant tables, vendored for vowel-fof.

#ifndef RPLAITS_DSP_ENGINE2_VOWEL_FOF_DATA_H_
#define RPLAITS_DSP_ENGINE2_VOWEL_FOF_DATA_H_

#include "stmlib/stmlib.h"

namespace rplaits {

// [register][vowel][formant], flattened. 5 x 5 x 5. Braids' own order, vendored
// verbatim -- the OUTER index is the REGISTER, exactly as `formant_f_data`'s
// `// bass` .. `// soprano` block comments have it. This comment used to read
// "[vowel][register][formant]": a transpose that was described but never
// applied, and the root cause of the engine shipping with its two vocal control
// labels swapped. The order is correct; do not transpose it now, because
// vowel_fof_engine.cc indexes it the way Braids does and every saved patch
// depends on that.
extern const int16_t kVowelFofFrequency[125];
extern const int16_t kVowelFofAmplitude[125];

}  // namespace rplaits

#endif  // RPLAITS_DSP_ENGINE2_VOWEL_FOF_DATA_H_
