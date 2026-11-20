/***************
CTAG TBD >>to be determined<< guitar chainsaw distortion stage.

Ported from SwedishChainsaw (https://github.com/Barabas5532/SwedishChainsaw),
Copyright (C) 2019 Barabas Raffai, GPL-3.0. Only the DSP is ported: the
plugin's 4x oversampling is omitted (ESP32 FPU budget) and the guitar-speaker
IR convolution is omitted (no IR asset on the device) — what remains is the
HM-2 preamp + JCM800-style tonestack character.

DECLARATIONS ONLY: the implementation lives in tbd_chainsaw_dist.cpp so that
including this header in a plugin's hot translation unit does not perturb that
TU's code generation (measured ~7-15k extra cycles per block on otherwise
unrelated plugins when this was header-inline). No static mutable state.

Signal path per sample (per channel):
  HP 60 Hz -> drive gain (20..40 dB) -> tanh -> 3 fixed HM-2 biquads
  -> LP 7 kHz -> HP 600 Hz -> amp gain (0..30 dB) -> tanh
  -> FMV tonestack (3rd-order IIR) -> LP 7 kHz
The host plugin then applies the shared tone LP / dry-wet blend.
***************/

#pragma once

#include <cstddef>
#include <cstdint>

namespace tbd {
namespace chainsaw {

struct BiquadCoeffs { float b0, b1, b2, a1, a2; };
struct FmvCoeffs { float b[4]; float a[3]; };

const BiquadCoeffs *Hm2Coeffs();
FmvCoeffs DesignFmv(float l, float m, float t, float Fs);

}  // namespace chainsaw

float ChainsawTanh(float x);

class ChainsawDist {
 public:
    void Init(float sample_rate);
    // drive, bias (amp gain), bass/mid/tone (FMV tonestack) are 0..1.
    void Process(float *out, float *aux, size_t size,
                 float drive, float bias, float bass, float mid,
                 float tone);

 private:
    float sr_{32000.0f};
    float hp_a_{0.0f}, lp_a_{0.0f}, amp_hp_a_{0.0f};
    float hp_x_[2]{}, hp_y_[2]{}, lp_[2]{}, amp_hp_x_[2]{}, amp_hp_y_[2]{}, amp_lp_[2]{};
    float bx_[2][3][2]{}, by_[2][3][2]{};
    float fx_[2][3]{}, fy_[2][3]{};
    // The powf pair and the FMV redesign are libm / ~100-flop work per block
    // from flash, while the knobs move rarely — cache and recompute on change
    // only (the rack_dist.h last_drive pattern). Gains and coefficients are
    // pure functions of (drive, bias, tone), so caching is output-identical.
    // Sentinels live outside the constrained 0..1 range.
    float last_drive_{-1.0f}, last_bias_{-1.0f};
    float last_bass_{-1.0f}, last_mid_{-1.0f}, last_tone_{-1.0f};
    float pedal_gain_{0.0f}, amp_gain_{0.0f};
    chainsaw::FmvCoeffs fmv_{};
};

// Complete chainsaw stage including the shared post-processing (1-pole tone
// LP, dry/wet). `lp` is the caller's existing per-channel tone state. Block
// sizes on both boards are 32 frames; 64 leaves headroom.
void ChainsawStage(float *out, float *aux, size_t size,
                   ChainsawDist &dist, float drive, float bias,
                   float bass, float mid, float tone, float mix,
                   float lp[2]);

}  // namespace tbd
