/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2024 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

The CTAG TBD hardware design is released under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0).
Details here: https://creativecommons.org/licenses/by-nc-sa/4.0/

CTAG TBD is provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.
***************/

#pragma once

// TBD guitar distortion stage, shared by the Rings engines.
//
// The four characters (and the sustainer AGC) are defined by the Elements /
// TBDings Part stages — this header is the same math extracted so the
// StringSynthPart CHORD engine (easter-egg string synth) can run it BEFORE
// its FX section: chords -> drive -> reverb/chorus, the guitar order.
// It mirrors rings/dsp/part.cc's inline block bit-for-bit (type 3's
// waveshaper table is rebuilt here with the SAME seed 0xcafe, so both
// engines sound identical).
//
//   0 tube: SoftClip(k*x)/SoftClip(k) — punch-preserving glue.
//   1 fold: Fold_do(x, d) — Retroactor's Vult/Befaco wavefolder.
//   2 fuzz: biased (SoftClip(k*x+b) - SoftClip(b)) / (1 + SoftClip(b)).
//   3 shape: TBD03's seeded braids waveshaper, drive as pre-gain.
// Then a 1-pole tone LP and a dry/wet blend, same curve on both channels,
// plus an envelope-follower AGC (sustain) that holds decaying tails.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "stmlib/dsp/dsp.h"     // stmlib::SoftClip
#include "stmlib/utils/dsp.h"   // stmlib::Mix

// Retroactor's Vult-generated Befaco wavefolder (vult_formantor.cpp in the
// ctagSoundProcessor component).
extern float Fold_do(float signal, float level);

// braids' per-unit sine table (braids/resources.cc, same component build),
// via a namespace-scoped extern — including braids/resources.h would trip
// the documented LUT-macro collision class.
namespace braids { extern const int16_t wav_sine[]; }

namespace tbd {

class GuitarDist {
 public:
  void Init() {
    lp_[0] = lp_[1] = 0.0f;
    env_[0] = env_[1] = 0.0f;
  }

  // type 0..3, all other amounts 0..1 (0 drive = zero-cost bypass).
  void Process(float* out, float* aux, size_t size,
               int type, float drive, float bias_amt, float tone,
               float mix_amt, float sustain) {
    if (drive > 0.0f) {
      const float d = drive;
      const float mix = mix_amt;
      const float lp_k = 0.04f + 0.96f * tone;
      const int t = type % 4;
      float k = 0.0f, k_norm = 1.0f;
      float bias = 0.0f, bias_sub = 0.0f, bias_norm = 1.0f;
      float ws_gain = 1.0f;
      if (t == 0) {
        k = 1.0f + 9.0f * d * d;
        k_norm = 1.0f / stmlib::SoftClip(k);
      } else if (t == 2) {
        k = 1.0f + 9.0f * d * d;
        bias = bias_amt;
        const float s_b = stmlib::SoftClip(bias);
        bias_sub = s_b;
        bias_norm = 1.0f / (1.0f + s_b);
      } else if (t == 3) {
        ws_gain = 1.0f + 7.0f * d;
      }
      const int32_t* ws = Table();
      const float sus_max = 1.0f + 15.0f * sustain * sustain;
      const bool sustain_on = sus_max > 1.001f;
      float* chans[2] = { out, aux };
      for (size_t c = 0; c < 2; ++c) {
        float lp = lp_[c];
        float env = env_[c];
        if (!sustain_on) {
          env *= 0.995f;
        }
        for (size_t i = 0; i < size; ++i) {
          const float dry = chans[c][i];
          float wet;
          if (t == 1) {
            wet = Fold_do(dry, d);
          } else if (t == 2) {
            wet = (stmlib::SoftClip(k * dry + bias) - bias_sub) * bias_norm;
          } else if (t == 3) {
            float xg = dry * ws_gain;
            if (xg > 1.0f) xg = 1.0f; else if (xg < -1.0f) xg = -1.0f;
            const uint16_t idx = static_cast<uint16_t>(
                static_cast<int16_t>(xg * 32767.0f) + 32768);
            const int32_t a = ws[idx >> 8];
            const int32_t b = ws[(idx >> 8) + 1];
            wet = static_cast<float>(a + ((b - a) * (idx & 0xff) >> 8)) *
                (1.0f / 32768.0f);
          } else {
            wet = stmlib::SoftClip(k * dry) * k_norm;
          }
          if (sustain_on) {
            const float x = fabsf(wet);
            env += (x > env ? 0.5f : 0.000125f) * (x - env);
            float g = 0.5f / (env > 0.002f ? env : 0.002f);
            if (g > sus_max) g = sus_max;
            wet *= g;
          }
          lp += lp_k * (wet - lp);
          chans[c][i] = dry + (lp - dry) * mix;
        }
        lp_[c] = lp;
        env_[c] = env;
      }
    } else {
      env_[0] *= 0.995f;
      env_[1] *= 0.995f;
    }
  }

 private:
  // Type-3 transfer table (braids SignatureWaveshaper math verbatim, seed
  // 0xcafe = TBD03's curve) — identical construction and seed to
  // rings/dsp/part.cc's s_dist_ws_, so the two engines match.
  static const int32_t* Table() {
    static int32_t ws[257];
    static bool built = false;
    if (!built) {
      uint32_t seed = 0xcafe;
      int32_t skew = seed & 15;
      seed >>= 4;
      int32_t sigmoid_strength = seed & 31;
      seed >>= 5;
      int32_t bumplets_frequency = seed & 3;
      seed >>= 2;
      bumplets_frequency += 3;
      int32_t bumplets_width = seed & 7;
      seed >>= 3;
      bumplets_width += 1;
      bumplets_width <<= 7;
      bumplets_width *= bumplets_width;
      for (int i = 0; i < 256; ++i) {
        int16_t x = (i - 128) << 8;
        int16_t x_skew = i * i - 32768;
        x = stmlib::Mix(x, x_skew, skew << 11);
        int16_t sigmoid = x * (8192 + (sigmoid_strength << 10)) /
            (8192 + (sigmoid_strength * abs(x) >> 5));
        int16_t bumplets = braids::wav_sine[(i * bumplets_frequency) & 255];
        uint16_t bumplet_gain = x * x / (bumplets_width) + 16;
        bumplet_gain = 32768 * 128 / (128 + bumplet_gain);
        ws[i] = stmlib::Mix(sigmoid, bumplets, bumplet_gain);
      }
      ws[256] = ws[255];
      built = true;
    }
    return ws;
  }

  float lp_[2];
  float env_[2];
};

}  // namespace tbd
