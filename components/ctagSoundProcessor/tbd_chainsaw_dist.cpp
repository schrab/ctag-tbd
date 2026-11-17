/***************
CTAG TBD >>to be determined<< — HM-2 chainsaw distortion implementation.

Out-of-line companion to helpers/tbd_chainsaw_dist.h. Keeping the DSP in this
translation unit (instead of header-inline) avoids perturbing code generation
in the Mutable/Parts TUs that merely include the header for the member type.

Ported from SwedishChainsaw, Copyright (C) 2019 Barabas Raffai, GPL-3.0.
***************/

#include "helpers/tbd_chainsaw_dist.h"

#include <cmath>

namespace tbd {
namespace chainsaw {

// HM2Filter.cpp — three MATLAB-fitted peaking sections, normalised to a0=1.
static const BiquadCoeffs kHm2Coeffs[3] = {
    { 1.016224171805848f, -1.996244766867333f, 0.980170456681741f,
      -1.996244766867333f, 0.996394628487589f },
    { 1.048098514125369f, -1.946884930731663f, 0.914902628855116f,
      -1.946884930731663f, 0.963001142980485f },
    { 1.260798129602192f, -1.896438187817481f, 0.674002337997260f,
      -1.896438187817481f, 0.934800467599452f },
};

const BiquadCoeffs *Hm2Coeffs() { return kHm2Coeffs; }

// FMVDesign.cpp verbatim: JCM800 component values, bilinear transform, then
// JUCE's normalisation by A0. l/m/t are the bass/middle/treble knob values.
FmvCoeffs DesignFmv(float l, float m, float t, float Fs) {
    const float R1 = 220e3f, R2 = 1e6f, R3 = 22e3f, R4 = 33e3f;
    const float C1 = 470e-12f, C2 = 22e-9f, C3 = 22e-9f;

    const float b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2)
                   + (C1 * R3 + C2 * R3);
    const float b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4)
                   - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                   + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                   + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
                   + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                   + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
    const float b3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
                   - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                   + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                   + t * C1 * C2 * C3 * R1 * R3 * R4 - t * m * C1 * C2 * C3 * R1 * R3 * R4
                   + t * l * C1 * C2 * C3 * R1 * R2 * R4;

    const float a0 = 1.0f;
    const float a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4)
                   + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
    const float a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3
                        + C2 * C3 * R3 * R3)
                   + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                   - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                   + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2
                        + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
                   + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4
                    + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
    const float a3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
                   - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                   + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3
                        - C1 * C2 * C3 * R1 * R3 * R4)
                   + l * C1 * C2 * C3 * R1 * R2 * R4 + C1 * C2 * C3 * R1 * R3 * R4;

    const float c = 2.0f * Fs, c2 = c * c, c3 = c2 * c;
    const float B0 = -b1 * c - b2 * c2 - b3 * c3;
    const float B1 = -b1 * c + b2 * c2 + 3.0f * b3 * c3;
    const float B2 =  b1 * c + b2 * c2 - 3.0f * b3 * c3;
    const float B3 =  b1 * c - b2 * c2 + b3 * c3;
    const float A0 = -a0 - a1 * c - a2 * c2 - a3 * c3;
    const float A1 = -3.0f * a0 - a1 * c + a2 * c2 + 3.0f * a3 * c3;
    const float A2 = -3.0f * a0 + a1 * c + a2 * c2 - 3.0f * a3 * c3;
    const float A3 = -a0 + a1 * c - a2 * c2 + a3 * c3;

    FmvCoeffs r;
    const float inv = 1.0f / A0;
    r.b[0] = B0 * inv; r.b[1] = B1 * inv; r.b[2] = B2 * inv; r.b[3] = B3 * inv;
    r.a[0] = A1 * inv; r.a[1] = A2 * inv; r.a[2] = A3 * inv;
    return r;
}

}  // namespace chainsaw

// Fast tanh-compatible saturator (unity small-signal slope, +/-1 asymptote).
float ChainsawTanh(float x) {
    const float a = fabsf(x);
    if (a > 8.0f) return x > 0.0f ? 1.0f : -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

void ChainsawDist::Init(float sample_rate) {
    sr_ = sample_rate;
    const float w60 = 6.2831853f * 60.0f / sample_rate;
    hp_a_ = 1.0f / (1.0f + w60);
    const float w7k = 6.2831853f * 7000.0f / sample_rate;
    lp_a_ = w7k / (1.0f + w7k);
    const float w600 = 6.2831853f * 600.0f / sample_rate;
    amp_hp_a_ = 1.0f / (1.0f + w600);
    for (int c = 0; c < 2; ++c) {
        hp_x_[c] = hp_y_[c] = 0.0f;
        lp_[c] = amp_hp_x_[c] = amp_hp_y_[c] = amp_lp_[c] = 0.0f;
        for (int i = 0; i < 3; ++i)
            bx_[c][i][0] = bx_[c][i][1] = by_[c][i][0] = by_[c][i][1] = 0.0f;
        for (int i = 0; i < 3; ++i) fx_[c][i] = fy_[c][i] = 0.0f;
    }
}

void ChainsawDist::Process(float *out, float *aux, size_t size,
                           float drive, float bias_amt, float tone) {
    if (drive <= 0.0f) return;   // zero cost, like the other dist types
    const float pedal_gain = powf(10.0f, (20.0f + 20.0f * drive) / 20.0f);
    const float amp_gain = powf(10.0f, (30.0f * bias_amt) / 20.0f);
    const chainsaw::FmvCoeffs fmv = chainsaw::DesignFmv(0.05f, 0.5f, tone, sr_);
    const chainsaw::BiquadCoeffs *hm2 = chainsaw::Hm2Coeffs();
    float *chans[2] = { out, aux };
    for (int c = 0; c < 2; ++c) {
        float *x = chans[c];
        for (size_t i = 0; i < size; ++i) {
            float v = x[i];
            float y = hp_a_ * (hp_y_[c] + v - hp_x_[c]);
            hp_x_[c] = v; hp_y_[c] = y;
            v = ChainsawTanh(y * pedal_gain);
            for (int b = 0; b < 3; ++b) {
                const chainsaw::BiquadCoeffs &k = hm2[b];
                const float xn = v;
                float yn = k.b0 * xn + k.b1 * bx_[c][b][0] + k.b2 * bx_[c][b][1]
                         - k.a1 * by_[c][b][0] - k.a2 * by_[c][b][1];
                bx_[c][b][1] = bx_[c][b][0]; bx_[c][b][0] = xn;
                by_[c][b][1] = by_[c][b][0]; by_[c][b][0] = yn;
                v = yn;
            }
            lp_[c] += lp_a_ * (v - lp_[c]);
            v = lp_[c];
            y = amp_hp_a_ * (amp_hp_y_[c] + v - amp_hp_x_[c]);
            amp_hp_x_[c] = v; amp_hp_y_[c] = y;
            v = ChainsawTanh(y * amp_gain);
            float xn3[3] = { fx_[c][0], fx_[c][1], fx_[c][2] };
            float yn3 = fmv.b[0] * v + fmv.b[1] * xn3[0] + fmv.b[2] * xn3[1]
                      + fmv.b[3] * xn3[2] - fmv.a[0] * fy_[c][0]
                      - fmv.a[1] * fy_[c][1] - fmv.a[2] * fy_[c][2];
            fx_[c][2] = fx_[c][1]; fx_[c][1] = fx_[c][0]; fx_[c][0] = v;
            fy_[c][2] = fy_[c][1]; fy_[c][1] = fy_[c][0]; fy_[c][0] = yn3;
            amp_lp_[c] += lp_a_ * (yn3 - amp_lp_[c]);
            x[i] = amp_lp_[c];
        }
    }
}

void ChainsawStage(float *out, float *aux, size_t size,
                   ChainsawDist &dist, float drive, float bias,
                   float tone, float mix, float sustain,
                   float lp[2], float env[2]) {
    if (size > 64) size = 64;
    float dry_l[64], dry_r[64];
    for (size_t i = 0; i < size; ++i) {
        dry_l[i] = out[i];
        dry_r[i] = aux[i];
    }
    dist.Process(out, aux, size, drive, bias, tone);

    const float sus_max = 1.0f + 15.0f * sustain * sustain;
    const bool sustain_on = sus_max > 1.001f;
    const float lp_k = 0.04f + 0.96f * tone;
    float *chans[2] = { out, aux };
    float *drys[2] = { dry_l, dry_r };
    for (int c = 0; c < 2; ++c) {
        float f_lp = lp[c];
        float f_env = env[c];
        if (!sustain_on) f_env *= 0.995f;
        for (size_t i = 0; i < size; ++i) {
            float wet = chans[c][i];
            if (sustain_on) {
                const float x = fabsf(wet);
                f_env += (x > f_env ? 0.5f : 0.000125f) * (x - f_env);
                float g = 0.5f / (f_env > 0.002f ? f_env : 0.002f);
                if (g > sus_max) g = sus_max;
                wet *= g;
            }
            f_lp += lp_k * (wet - f_lp);
            chans[c][i] = drys[c][i] + (f_lp - drys[c][i]) * mix;
        }
        lp[c] = f_lp;
        env[c] = f_env;
    }
}

}  // namespace tbd
