/* Host tests for the ESP32 fast distortion primitives. */
#include "test_framework.hpp"

#include "helpers/tbd_dist_fast.h"

#include <cmath>

TEST(dist_fast_softclip_matches_reference) {
    tbd::distfast::BuildSoftLimitTable();
    for (float x = -8.0f; x <= 8.0f; x += 0.037f) {
        const float ref = tbd::distfast::SoftLimitRef(x);
        const float got = tbd::distfast::FastSoftLimit(x);
        CHECK(fabsf(got - ref) < 0.002f);
    }
    // FastSoftClip uses a smooth polynomial (no LUT derivative kinks that
    // crackle in high-gain fuzz). Same accuracy on its [-3,3] domain.
    for (float x = -3.0f; x <= 3.0f; x += 0.011f) {
        const float ref = tbd::distfast::SoftLimitRef(x);
        CHECK(fabsf(tbd::distfast::FastSoftClip(x) - ref) < 0.001f);
    }
    // Smoothness: no derivative jumps -> second difference stays small.
    // A piecewise-linear LUT would spike here, a polynomial does not.
    float maxD2 = 0.0f;
    for (float x = -3.0f; x < 2.99f; x += 0.01f) {
        const float a = tbd::distfast::FastSoftClip(x);
        const float b = tbd::distfast::FastSoftClip(x + 0.01f);
        const float c = tbd::distfast::FastSoftClip(x + 0.02f);
        const float d2 = fabsf(a - 2.0f * b + c);
        if (d2 > maxD2) maxD2 = d2;
    }
    CHECK(maxD2 < 0.002f);
    // Outside the table it clamps to the endpoint values.
    CHECK(fabsf(tbd::distfast::FastSoftLimit(-20.0f) -
                tbd::distfast::SoftLimitRef(-8.0f)) < 1e-6f);
    // SoftClip clamps beyond +/-3 to exactly +/-1.
    CHECK(tbd::distfast::FastSoftClip(5.0f) == 1.0f);
    CHECK(tbd::distfast::FastSoftClip(-5.0f) == -1.0f);
}

TEST(dist_fast_recip_accurate) {
    // Sustain AGC env range: 0.002 .. ~1.5.
    for (float x = 0.002f; x <= 1.5f; x *= 1.17f) {
        const float ref = 1.0f / x;
        const float got = tbd::distfast::FastRecip(x);
        CHECK(fabsf(got - ref) <= ref * 0.0002f);
    }
}

TEST(dist_fast_floor_pos) {
    for (float x = 0.0f; x < 60.0f; x += 0.31f)
        CHECK(tbd::distfast::FastFloorPos(x) == floorf(x));
}
