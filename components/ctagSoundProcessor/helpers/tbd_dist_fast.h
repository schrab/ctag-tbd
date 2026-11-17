/***************
CTAG TBD — ESP32-friendly fast distortion primitives.

The Xtensa LX6 has NO hardware divider or floor instruction. The stock
distortion path used a per-sample float divide for the SoftLimit rational
(SoftClip) AND for the sustain AGC gain, plus floorf() in the fold shaper —
tens of software helper calls per block on every plugin with distortion,
even when the stage was barely audible.

This header replaces those with:
  - FastSoftClip / FastSoftLimit: a 257-entry LUT of the exact stmlib
    SoftLimit curve with linear interpolation (multiply-only per sample).
  - FastRecip: bit-trick initial estimate + 2 Newton-Raphson steps, no
    __divsf3 call.
  - FastFloor01: truncation for values known to be >= 0 (replaces floorf).

Pure C++, no ESP-IDF, so host tests compile it directly.
***************/

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace tbd {
namespace distfast {

// Exact stmlib rational: x*(27+x^2)/(27+9*x^2).
inline float SoftLimitRef(float x) {
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

// LUT of the rational over [-8, 8]; 256 segments, step 0.0625.
static const int kSlN = 256;
static float s_slTable[kSlN + 1];
static bool s_slBuilt = false;

inline void BuildSoftLimitTable() {
    if (s_slBuilt) return;
    for (int i = 0; i <= kSlN; ++i) {
        const float x = -8.0f + 16.0f * (float)i / (float)kSlN;
        s_slTable[i] = SoftLimitRef(x);
    }
    s_slBuilt = true;
}

inline float FastSoftLimit(float x) {
    if (!s_slBuilt) BuildSoftLimitTable();
    if (x <= -8.0f) return s_slTable[0];
    if (x >= 8.0f) return s_slTable[kSlN];
    const float p = (x + 8.0f) * ((float)kSlN / 16.0f);   // 0..256
    const int i = (int)p;
    const float f = p - (float)i;
    return s_slTable[i] + (s_slTable[i + 1] - s_slTable[i]) * f;
}

// stmlib SoftClip clamps |x| > 3 to +/-1 (SoftLimit(3) == 1 exactly).
// Smooth odd polynomial x*P(x^2) fitted to the exact rational on [-3,3]
// with Chebyshev nodes (max error 0.00052). C-infinity: no LUT derivative
// kinks, which became audible as crackle in high-gain fuzz (type 2).
// Horner in u=x^2, 7 multiplies, no divide, no table loads.
inline float FastSoftClip(float x) {
    if (x <= -3.0f) return -1.0f;
    if (x >= 3.0f) return 1.0f;
    const float u = x * x;
    float p = -6.961e-07f;
    p = p * u + 2.71491e-05f;
    p = p * u + -0.0004444363f;
    p = p * u + 0.004029295f;
    p = p * u + -0.0228995818f;
    p = p * u + 0.0911785112f;
    p = p * u + -0.2943995661f;
    p = p * u + 1.0f;
    return x * p;
}

// Fast reciprocal via bit-trick estimate + 2 Newton-Raphson refinements.
// Accurate to ~1e-5 over the positive range used by the sustain AGC.
inline float FastRecip(float x) {
    uint32_t i;
    std::memcpy(&i, &x, sizeof(i));
    i = 0x7EF311C3u - i;              // rough 1/x estimate from exponent bits
    float y;
    std::memcpy(&y, &i, sizeof(y));
    y = y * (2.0f - x * y);           // Newton step 1
    y = y * (2.0f - x * y);           // Newton step 2
    return y;
}

// floorf for values guaranteed >= 0: truncation via int cast.
inline float FastFloorPos(float x) { return (float)(int)x; }

}  // namespace distfast
}  // namespace tbd
