#include "test_framework.hpp"
#include <functional>
#include <thread>
#include <condition_variable>
#include <atomic>

// Compile the real request/wait/notify implementation. Only UART and the
// RTOS primitives are replaced; no firmware tasks are started on the host.
#include "../../uimcu/main/LinkClient.cpp"

using namespace CTAG::CTRL;
using namespace CTAG::LINK;

namespace CTAG::CTRL {
void DspProxy::OnLinkFrame(const char *, size_t) {}
}

namespace {
std::function<void(const std::string &, uint32_t)> onWrite;
void Reply(const char *text, const char *command, uint32_t id) {
    CTAG::CTRL::LinkClient_NotifyDebug(text, strlen(text), command, id);
}
void StartRelay() {
    LinkClient::Start();
    CTAG::CTRL::s_online = true;
}
}

int MockRelayWrite(const char *bytes, size_t size) {
    std::string payload(bytes + 1, size - 2);
    std::string command;
    long long id = 0;
    CHECK(ScanStringValue(payload.data(), payload.size(), "cmd", command));
    CHECK(ScanIntValue(payload.data(), payload.size(), "_rid", id));
    onWrite(command, static_cast<uint32_t>(id));
    return size;
}

TEST(panel_relay_matches_command_and_id_and_accepts_json_whitespace) {
    StartRelay();
    onWrite = [](const std::string &command, uint32_t id) {
        Reply("wrong id", command.c_str(), id - 1);
        Reply("wrong command", "/debug/other", id);
        Reply("correct", command.c_str(), id);
    };
    std::string reply;
    CHECK(LinkClient::RequestDebug("{ \"cmd\" : \"/debug/sdInfo\" }", reply, 100));
    CHECK_STREQ(reply, "correct");
}

TEST(panel_relay_late_reply_cannot_complete_a_new_request_for_the_same_command) {
    StartRelay();
    uint32_t oldId = 0;
    onWrite = [&](const std::string &, uint32_t id) { oldId = id; };
    std::string reply;
    CHECK(!LinkClient::RequestDebug("{\"cmd\":\"/debug/sdInfo\"}", reply, 10));
    onWrite = [&](const std::string &command, uint32_t id) {
        Reply("late", command.c_str(), oldId);
        Reply("new", command.c_str(), id);
    };
    CHECK(LinkClient::RequestDebug("{\"cmd\":\"/debug/sdInfo\"}", reply, 100));
    CHECK_STREQ(reply, "new");
}

TEST(panel_relay_serializes_ui_and_bench_transactions) {
    StartRelay();
    std::mutex mutex;
    std::condition_variable written;
    int writes = 0;
    uint32_t firstId = 0;
    onWrite = [&](const std::string &command, uint32_t id) {
        std::lock_guard<std::mutex> lock(mutex);
        ++writes;
        if (writes == 1) firstId = id;
        else Reply("bench", command.c_str(), id);
        written.notify_one();
    };
    std::string uiReply, benchReply;
    bool uiOk = false, benchOk = false;
    std::thread ui([&] {
        uiOk = LinkClient::RequestDebug("{\"cmd\":\"/debug/sdInfo\"}", uiReply, 1000);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(written.wait_for(lock, std::chrono::seconds(1), [&] { return writes == 1; }));
    }
    std::thread bench([&] {
        benchOk = LinkClient::RequestDebug("{\"cmd\":\"/debug/getAudioStats\"}", benchReply, 1000);
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(!written.wait_for(lock, std::chrono::milliseconds(20), [&] { return writes > 1; }));
        Reply("ui", "/debug/sdInfo", firstId);
    }
    ui.join(); bench.join();
    CHECK(uiOk); CHECK(benchOk);
    CHECK_STREQ(uiReply, "ui"); CHECK_STREQ(benchReply, "bench");
}
