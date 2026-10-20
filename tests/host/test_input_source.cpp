/*
 * Host tests for the stored input-source vocabulary (main/InputSource.hpp).
 *
 * SPManager and both config pages share this alias table. If it drifts, a page
 * that displays the canonical "mic"/"line" names will rewrite a legacy stored
 * "line1"/"line2" to the wrong codec input on its next commit -- a silent
 * hardware re-route with no visible UI clue. Pin every accepted spelling.
 */
#include "test_framework.hpp"
#include "InputSource.hpp"

namespace InputSource = CTAG::AUDIO::InputSource;

TEST(input_source_canonical_names) {
    CHECK_EQ(InputSource::COUNT, 2);
    CHECK_STREQ(InputSource::NAMES[0], "mic");
    CHECK_STREQ(InputSource::NAMES[1], "line");
    CHECK_STREQ(InputSource::OPTIONS, "mic,line");
}

TEST(input_source_parses_current_names) {
    CHECK_EQ(InputSource::Parse("mic"), 0);
    CHECK_EQ(InputSource::Parse("line"), 1);
}

TEST(input_source_parses_legacy_names) {
    // "line1" was the pre-rename name for the mono input, "line2" for stereo.
    CHECK_EQ(InputSource::Parse("line1"), 0);
    CHECK_EQ(InputSource::Parse("line2"), 1);
}

TEST(input_source_rejects_unknown) {
    CHECK_EQ(InputSource::Parse(""), -1);
    CHECK_EQ(InputSource::Parse("foo"), -1);
    CHECK_EQ(InputSource::Parse(nullptr), -1);
}
