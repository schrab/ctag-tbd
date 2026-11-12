/*
 * Host unit tests for the SEQ modulators (GATE x2 + ARP x2) and the
 * slot-space growth they need (cv 102-105, trig 43-50).
 */
#include "test_framework.hpp"

#include "helpers/ctagSlotBounds.hpp"

TEST(seq_clamp_new_bounds_valid) {
    // cv 102..105 and trig 43..50 must be valid after the extension
    CHECK(CTAG::SP::HELPERS::ClampCVSlot(102) == 102);
    CHECK(CTAG::SP::HELPERS::ClampCVSlot(105) == 105);
    CHECK(CTAG::SP::HELPERS::ClampTrigSlot(43) == 43);
    CHECK(CTAG::SP::HELPERS::ClampTrigSlot(50) == 50);
}

TEST(seq_clamp_at_new_bounds_invalid) {
    // first index past each range clamps to unassigned (stack-safety rule)
    CHECK(CTAG::SP::HELPERS::ClampCVSlot(106) == -1);
    CHECK(CTAG::SP::HELPERS::ClampTrigSlot(51) == -1);
}
