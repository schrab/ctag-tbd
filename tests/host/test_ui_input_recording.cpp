#include "test_framework.hpp"
#include "../../uimcu/main/input/InputEventBuffer.hpp"
#include "RecordingFile.hpp"
#include <filesystem>
#include <fstream>
#include <sys/stat.h>

using namespace CTAG::CTRL;
using namespace CTAG::AUDIO;

TEST(ui_input_coalesces_turns_without_crossing_buttons_or_other_encoder) {
    InputEventBuffer queue;
    CHECK(queue.Push({InputEvent::ENC_DELTA, 3}));
    CHECK(queue.Push({InputEvent::ENC_DELTA, 4}));
    CHECK(queue.Push({InputEvent::BTN2_SHORT, 0}));
    CHECK(queue.Push({InputEvent::ENC_DELTA, -2}));
    CHECK(queue.Push({InputEvent::ENC2_DELTA, 1}));
    InputEvent event{};
    CHECK(queue.Pop(event)); CHECK_EQ(event.type, InputEvent::ENC_DELTA); CHECK_EQ(event.delta, 7);
    CHECK(queue.Pop(event)); CHECK_EQ(event.type, InputEvent::BTN2_SHORT);
    CHECK(queue.Pop(event)); CHECK_EQ(event.delta, -2);
    CHECK(queue.Pop(event)); CHECK_EQ(event.type, InputEvent::ENC2_DELTA);
    CHECK(!queue.Pop(event));
}

TEST(ui_input_button_survives_a_full_encoder_queue) {
    InputEventBuffer queue;
    CHECK(queue.Push({InputEvent::BTN1_SHORT, 0}));
    for (int i = 0; i < 1000; ++i)
        CHECK(queue.Push({i % 2 ? InputEvent::ENC_DELTA : InputEvent::ENC2_DELTA, 1}));
    CHECK(queue.Push({InputEvent::BTN2_LONG, 0}));
    InputEvent event{};
    CHECK(queue.Pop(event));
    CHECK_EQ(event.type, InputEvent::BTN1_SHORT);
    int buttons = 0;
    while (queue.Pop(event)) if (event.type == InputEvent::BTN2_LONG) ++buttons;
    CHECK_EQ(buttons, 1);
}

TEST(ui_input_full_button_queue_preserves_every_release) {
    InputEventBuffer queue;
    for (int i = 0; i < InputEventBuffer::Capacity; ++i)
        CHECK(queue.Push({InputEvent::BTN2_SHORT, 0}));
    CHECK(!queue.Push({InputEvent::ENC_DELTA, 1}));
    CHECK(!queue.Push({InputEvent::BTN1_SHORT, 0}));
    InputEvent event{};
    int count = 0;
    while (queue.Pop(event)) { CHECK_EQ(event.type, InputEvent::BTN2_SHORT); ++count; }
    CHECK_EQ(count, InputEventBuffer::Capacity);
}

TEST(ui_button_requires_a_stable_press_and_release) {
    ButtonDebouncer button;
    CHECK(!button.Update(1, 100, 2));
    CHECK(!button.Update(0, 101, 2)); // bounce cancels candidate
    CHECK(!button.Update(1, 102, 2));
    CHECK(!button.Update(1, 103, 2));
    CHECK(button.Update(1, 104, 2));
    CHECK(!button.Update(0, 200, 2)); // isolated release after a long hold
    CHECK(!button.Update(1, 201, 2));
    CHECK_EQ(button.State(), 1);
    CHECK(!button.Update(0, 300, 2));
    CHECK(!button.Update(0, 301, 2));
    CHECK(button.Update(0, 302, 2));
    CHECK_EQ(button.State(), 0);
}

TEST(ui_button_adc_error_cancels_an_unconfirmed_edge) {
    ButtonDebouncer button;
    CHECK(!button.Update(1, 100, 2));
    CHECK(!button.Update(-1, 101, 2));
    CHECK(!button.Update(1, 102, 2));
    CHECK(!button.Update(1, 103, 2));
    CHECK(button.Update(1, 104, 2));
}

TEST(ui_button_debounce_handles_tick_wrap) {
    ButtonDebouncer button;
    CHECK(!button.Update(1, UINT32_MAX - 1, 2));
    CHECK(!button.Update(1, UINT32_MAX, 2));
    CHECK(button.Update(1, 0, 2));
}

namespace {
struct RecordingDirectory {
    char path[64] = "/tmp/opencode/tape-test-XXXXXX";
    RecordingDirectory() { CHECK(mkdtemp(path) != nullptr); }
    ~RecordingDirectory() { std::filesystem::remove_all(path); }
    std::string File(const char *name) const { return std::string(path) + "/" + name; }
};
}

TEST(tape_recording_name_scans_past_32_entries_and_matches_case) {
    RecordingDirectory dir;
    for (int i = 1; i <= 70; ++i) {
        char name[24];
        snprintf(name, sizeof(name), "TAPE_%03d.WAV", i);
        std::ofstream(dir.File(name)) << "existing recording";
    }
    char name[16];
    CHECK(NextRecordingName(dir.path, name, sizeof(name)));
    CHECK_STREQ(name, "tape_071.wav");
    // A directory also occupies a recording's filename.
    CHECK_EQ(mkdir(dir.File(name).c_str(), 0700), 0);
    CHECK(NextRecordingName(dir.path, name, sizeof(name)));
    CHECK_STREQ(name, "tape_072.wav");
}

TEST(tape_recording_exclusive_create_never_truncates_existing_data) {
    RecordingDirectory dir;
    std::string path = dir.File("tape_001.wav");
    std::ofstream(path) << "user audio";
    CHECK(OpenNewRecording(path.c_str()) == nullptr);
    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), {});
    CHECK_STREQ(content, "user audio");
    path = dir.File("tape_002.wav");
    FILE *file = OpenNewRecording(path.c_str());
    CHECK(file != nullptr);
    if (file) fclose(file);
    CHECK(OpenNewRecording(path.c_str()) == nullptr);
}

TEST(tape_recording_name_reports_exhaustion) {
    RecordingDirectory dir;
    for (int i = 1; i <= 999; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "tape_%03d.wav", i);
        std::ofstream(dir.File(name));
    }
    char name[16] = "stale";
    CHECK(!NextRecordingName(dir.path, name, sizeof(name)));
    CHECK_STREQ(name, "");
}
