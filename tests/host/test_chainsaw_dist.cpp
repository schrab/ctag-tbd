/*
 * Host unit tests for the HM-2 "Swedish chainsaw" distortion stage.
 * Reference: https://github.com/Barabas5532/SwedishChainsaw (GPL-3.0).
 */
#include "test_framework.hpp"

#include "helpers/tbd_chainsaw_dist.h"

#include <cmath>
#include <cstring>

TEST(chainsaw_hm2_coefficients_match_reference) {
    // The three fixed biquads from HM2Filter.cpp (MATLAB circuit fit),
    // normalised to a1/a2 = 1 as JUCE's IIR::Coefficients expects.
    const float b[3][3] = {
        { 1.016224171805848f, -1.996244766867333f, 0.980170456681741f },
        { 1.048098514125369f, -1.946884930731663f, 0.914902628855116f },
        { 1.260798129602192f, -1.896438187817481f, 0.674002337997260f },
    };
    const float a2[3] = { 0.996394628487589f, 0.963001142980485f, 0.934800467599452f };
    for (int i = 0; i < 3; ++i) {
        const tbd::chainsaw::BiquadCoeffs &c = tbd::chainsaw::Hm2Coeffs()[i];
        CHECK(fabsf(c.b0 - b[i][0]) < 1e-6f);
        CHECK(fabsf(c.b1 - b[i][1]) < 1e-6f);
        CHECK(fabsf(c.b2 - b[i][2]) < 1e-6f);
        CHECK(fabsf(c.a2 - a2[i]) < 1e-6f);
    }
}

TEST(chainsaw_zero_drive_is_bypass) {
    tbd::ChainsawDist d;
    d.Init(32000.0f);
    float out[32], aux[32];
    for (int i = 0; i < 32; ++i) {
        out[i] = 0.25f * (float)((i & 7) - 4) / 4.0f;
        aux[i] = -out[i];
    }
    float in[32], inAux[32];
    memcpy(in, out, sizeof(in));
    memcpy(inAux, aux, sizeof(aux));
    d.Process(out, aux, 32, 0.0f, 0.0f, 0.5f);
    for (int i = 0; i < 32; ++i) {
        CHECK(fabsf(out[i] - in[i]) < 1e-6f);
        CHECK(fabsf(aux[i] - inAux[i]) < 1e-6f);
    }
}

TEST(chainsaw_clips_and_shapes) {
    tbd::ChainsawDist d;
    d.Init(32000.0f);
    float out[64], aux[64];
    for (int i = 0; i < 64; ++i) {
        out[i] = 0.9f * sinf(2.0f * 3.14159265f * (float)i / 16.0f);
        aux[i] = -out[i];
    }
    d.Process(out, aux, 64, 1.0f, 0.6f, 0.7f);
    float peak = 0.0f;
    for (int i = 0; i < 64; ++i) {
        if (fabsf(out[i]) > peak) peak = fabsf(out[i]);
        CHECK(std::isfinite(out[i]));
        CHECK(std::isfinite(aux[i]));
    }
    // Hard-driven tanh preamp must produce a substantial, bounded signal.
    CHECK(peak > 0.2f);
    CHECK(peak < 8.0f);
}

TEST(chainsaw_fmv_coefficients_finite_and_tone_dependent) {
    tbd::chainsaw::FmvCoeffs lo = tbd::chainsaw::DesignFmv(0.05f, 0.5f, 0.1f, 32000.0f);
    tbd::chainsaw::FmvCoeffs hi = tbd::chainsaw::DesignFmv(0.05f, 0.5f, 0.9f, 32000.0f);
    for (int i = 0; i < 4; ++i) {
        CHECK(std::isfinite(lo.b[i]));
        CHECK(std::isfinite(hi.b[i]));
    }
    for (int i = 0; i < 3; ++i) {
        CHECK(std::isfinite(lo.a[i]));
        CHECK(std::isfinite(hi.a[i]));
    }
    // Treble must actually move the coefficient set.
    CHECK(fabsf(lo.b[0] - hi.b[0]) > 1e-9f ||
          fabsf(lo.b[1] - hi.b[1]) > 1e-9f ||
          fabsf(lo.b[2] - hi.b[2]) > 1e-9f);
}
