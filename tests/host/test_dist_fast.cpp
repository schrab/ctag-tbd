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
