#include "test_framework.hpp"
#include "DspProxy.hpp"
#include "Display.hpp"
#include "LinkClient.hpp"
#include "MidiLink.hpp"
#include "BtMidiReceiver.hpp"
#include "UIMenu.hpp"
#include "menupages/UIMenuPageParams.hpp"
#include "menupages/UIMenuPageSystem.hpp"
#include "menupages/UIMenuPageTape.hpp"
#include "menupages/UIMenuPageMidi.hpp"
#include "menupages/UIMenuPageHome.hpp"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "freertos/FreeRTOS.h"
#include <atomic>
#include <thread>

using namespace CTAG::CTRL;
using namespace CTAG::DRIVERS;
using namespace rapidjson;

namespace {
std::vector<std::string> commands;
std::vector<std::string> drawn;
bool connected = false;
bool requestedSleep = false;

bool Drew(const std::string &text) {
    return std::find(drawn.begin(), drawn.end(), text) != drawn.end();
}
void Push(const std::string &frame) { DspProxy::OnLinkFrame(frame.c_str(), frame.size()); }
void PushString(const char *command, const char *key, const std::string &value) {
    StringBuffer buffer;
    Writer<StringBuffer> w(buffer);
    w.StartObject(); w.Key("cmd"); w.String(command);
    w.Key("ch"); w.Int(0); w.Key(key); w.String(value.c_str()); w.EndObject();
    Push(buffer.GetString());
}
void Params(const char *entries, int part = 0, bool last = true) {
    StringBuffer buffer;
    Writer<StringBuffer> w(buffer);
    w.StartObject(); w.Key("cmd"); w.String("link/pushParams");
    w.Key("ch"); w.Int(0); w.Key("part"); w.Int(part);
    w.Key("last"); w.Int(last); w.Key("params"); w.String(entries); w.EndObject();
    Push(buffer.GetString());
}
void PushValue(int value) {
    Push("{\"cmd\":\"link/pushParamValue\",\"ch\":0,\"id\":\"tune\",\"k\":\"current\",\"current\":" +
         std::to_string(value) + "}");
}
const char *tune = "[{\"id\":\"tune\",\"name\":\"Tune\",\"type\":\"int\",\"min\":0,\"max\":100,\"current\":10,\"cv\":-1}]";
void ActivePlugin() {
    Push("{\"cmd\":\"link/pushActivePlugin\",\"id0\":\"Test\",\"id1\":\"\",\"st0\":1,\"st1\":0}");
}
}

namespace CTAG::CTRL {
void LinkClient::Start() {}
bool LinkClient::IsOnline() { return true; }
bool LinkClient::SendCommand(const std::string &command) { commands.push_back(command); return true; }
bool LinkClient::RequestFsList(const char *, std::string &entries, int) { entries = "[]"; return true; }
bool LinkClient::RequestDebug(const std::string &, std::string &reply, int) {
    reply = "{\"name\":\"tape_071.wav\"}"; return true;
}
void LinkClient_NotifyFsList(uint32_t, const char *) {}
void LinkClient_NotifyDebug(const char *, size_t, const char *, uint32_t) {}
void MidiLink::OnMidiOut(const uint8_t *, size_t) {}
void UIMenu::RequestSleep() { requestedSleep = true; }
void UIMenu::RequestPanelEdit(int, int) {}
}
namespace CTAG::DRIVERS {
void Display::Clear() { drawn.clear(); }
void Display::Flush() {}
void Display::Contrast(int) {}
void Display::DrawString(int, int, const char *s, Font) { drawn.emplace_back(s); }
void Display::DrawStringRight(int, int, const char *s, Font) { drawn.emplace_back(s); }
void Display::DrawPopup(const char *, Font) {}
void Display::InvertRect(int, int, int, int) {}
void Display::DrawScrollbar(int, int, int, int, int) {}
int Display::FontAdvance(Font, unsigned char) { return 6; }
int BtMidiReceiver::GetDeviceCount() { return 0; }
const BtDeviceInfo *BtMidiReceiver::GetDevice(int) { return nullptr; }
bool BtMidiReceiver::IsConnected() { return connected; }
bool BtMidiReceiver::IsScanning() { return false; }
void BtMidiReceiver::StartScan() {}
void BtMidiReceiver::StopScan() {}
void BtMidiReceiver::Connect(int) {}
void BtMidiReceiver::Disconnect() { connected = false; }
}

TEST(panel_parameter_chunks_are_hidden_until_complete_and_reject_missing_parts) {
    Params(tune, 0, false);
    CHECK(!DspProxy::IsParamTableReady(0));
    CHECK_EQ(DspProxy::GetParamEntryCount(0), 0);
    DspProxy::DspParamEntry entry;
    CHECK(!DspProxy::GetParamEntry(0, 0, entry));
    Params("[]", 2, true); // part 1 was lost
    CHECK(!DspProxy::IsParamTableReady(0));
    Params("[]", 1, true);
    CHECK(DspProxy::IsParamTableReady(0));
    CHECK_EQ(DspProxy::GetParamEntryCount(0), 1);
    CHECK(DspProxy::GetParamEntry(0, 0, entry));
    CHECK_STREQ(entry.id, "tune");
}

TEST(panel_parameter_grouping_does_not_duplicate_group_leaves_in_general) {
    ActivePlugin();
    Params("[{\"type\":\"group\",\"name\":\"Envelope\"},"
           "{\"id\":\"attack\",\"name\":\"Attack\",\"type\":\"int\",\"group\":0},"
           "{\"type\":\"group\",\"name\":\"Filter\"},"
           "{\"id\":\"cutoff\",\"name\":\"Cutoff\",\"type\":\"int\",\"group\":2}]");
    UIMenuPageParams page{};
    page.init(); page.onDeepEnter(0); page.doRedraw();
    CHECK(Drew("Envelope (1)")); CHECK(Drew("Filter (1)")); CHECK(!Drew("General (2)"));
    page.deinit();
}

TEST(panel_long_parameter_id_survives_the_table_and_value_edit) {
    ActivePlugin();
    Params("[{\"id\":\"ADSRenvelopeResonanceAmount\",\"name\":\"Amount\",\"type\":\"int\",\"min\":0,\"max\":4095,\"current\":10}]");
    UIMenuPageParams page{};
    page.init(); page.onDeepEnter(0); page.onValueEncoder(1);
    Document request; request.Parse(commands.back().c_str());
    CHECK_STREQ(request["id"].GetString(), "ADSRenvelopeResonanceAmount");
    page.deinit();
}

TEST(panel_incremental_update_never_loses_the_final_notification) {
    Params(tune);
    uint32_t version = DspProxy::GetParamTableVersion();
    TestHostSetTick(100); PushValue(11);
    CHECK(DspProxy::GetParamTableVersion() != version);
    version = DspProxy::GetParamTableVersion();
    TestHostSetTick(101); PushValue(12);
    CHECK(DspProxy::GetParamTableVersion() != version);
}

TEST(panel_focused_parameter_accepts_external_updates_without_a_local_edit) {
    ActivePlugin(); Params(tune);
    UIMenuPageParams page{};
    page.init(); page.onDeepEnter(0);
    PushValue(77); page.onTick();
    CHECK(Drew("77"));
    page.onValueEncoder(1);
    Document request; request.Parse(commands.back().c_str());
    CHECK_EQ(request["value"].GetInt(), 78);
    page.deinit();
}

TEST(panel_pending_local_edit_expires_even_without_another_push) {
    ActivePlugin(); Params(tune); TestHostSetTick(100);
    UIMenuPageParams page{};
    page.init(); page.onDeepEnter(0); page.onValueEncoder(1);
    PushValue(10); page.onTick(); // delayed echo must not undo the latest detent
    CHECK(Drew("11"));
    TestHostSetTick(125); page.onTick();
    CHECK(Drew("10")); // no acknowledgement: authoritative value wins
    page.deinit();
}

TEST(panel_acknowledged_edit_does_not_mask_a_later_external_change) {
    ActivePlugin(); Params(tune); TestHostSetTick(200);
    UIMenuPageParams page{};
    page.init(); page.onDeepEnter(0); page.onValueEncoder(1);
    PushValue(11); page.onTick();
    PushValue(55); page.onTick();
    CHECK(Drew("55"));
    page.deinit();
}

TEST(panel_store_appends_after_a_capped_64_preset_listing) {
    ActivePlugin(); Params(tune);
    std::string presets = "{\"presets\":[";
    for (int i = 0; i < 75; ++i)
        presets += (i ? "," : "") + std::string("{\"name\":\"Patch\",\"number\":") + std::to_string(i) + "}";
    presets += "]}";
    PushString("link/pushPresets", "presets", presets);
    UIMenuPageParams page{};
    page.init(); page.onEncoder(2); page.onButton(2, false);
    page.onEncoder(100); page.onButton(2, false);
    Document request; request.Parse(commands.back().c_str());
    CHECK_STREQ(request["cmd"].GetString(), "/link/presetSave");
    CHECK(request.HasMember("append") && request["append"].GetBool());
    CHECK(!request.HasMember("number"));
    page.deinit();
}

TEST(panel_manual_mapping_disarms_cc_learn) {
    ActivePlugin(); Params(tune);
    UIMenuPageParams page{};
    page.init(); page.onDeepEnter(0); page.onButton(2, false);
    page.onValueEncoder(-1); page.onButton(2, false); // None -> Learn CC
    CHECK(DspProxy::IsLearning());
    page.onValueEncoder(1); page.onButton(2, false); // choose None instead
    CHECK(!DspProxy::IsLearning());
    page.deinit();
}

TEST(panel_cache_snapshots_survive_concurrent_replacement) {
    PushString("link/pushConfig", "config", "{\"keep\":\"original\"}");
    std::string snapshot = DspProxy::GetJSONConfiguration();
    std::atomic<bool> good{true};
    std::thread writer([&] {
        for (int i = 0; i < 500; ++i)
            PushString("link/pushConfig", "config", "{\"keep\":\"" + std::string(300 + i % 10, 'x') + "\"}");
    });
    for (int i = 0; i < 500; ++i) {
        std::string copy = DspProxy::GetJSONConfiguration();
        Document parsed; parsed.Parse(copy.c_str());
        if (parsed.HasParseError() || !parsed.IsObject()) good = false;
    }
    writer.join();
    CHECK(good.load()); CHECK_STREQ(snapshot, "{\"keep\":\"original\"}");
}

TEST(panel_system_gain_commits_the_native_key_used_by_mix) {
    Push("{\"cmd\":\"link/pushIoCaps\",\"inlvlmax\":63,\"inlvldef\":40}");
    PushString("link/pushConfig", "config", "{\"input_gain_lvl\":\"40\",\"input_gain\":\"7\"}");
    UIMenuPageSystem page{};
    page.init(); page.onEncoder(9); page.onValueEncoder(1); page.onButton(2, false);
    Document config; config.Parse(DspProxy::GetJSONConfiguration().c_str());
    CHECK_STREQ(config["input_gain_lvl"].GetString(), "43");
    CHECK_STREQ(config["input_gain"].GetString(), "7");
    page.deinit();
}

TEST(panel_config_command_escapes_quotes_and_backslashes) {
    const std::string config = "{\"name\":\"a\\\"b\\\\c\"}";
    DspProxy::SetConfigurationFromJSON(config);
    Document request; request.Parse(commands.back().c_str());
    CHECK(!request.HasParseError());
    CHECK_STREQ(request["config"].GetString(), config.c_str());
}

TEST(panel_tape_timecode_uses_the_transport_rate) {
    Push("{\"cmd\":\"link/stream\",\"sd\":{\"p\":0,\"r\":1,\"pos\":1920000,\"tot\":0,\"rate\":32000}}");
    UIMenuPageTape page{};
    page.init(); page.doRedraw(); CHECK(Drew("REC 01:00"));
}

TEST(panel_midi_connection_status_updates_without_a_scan_change) {
    UIMenuPageMidi page{};
    connected = false; page.init(); page.onTick();
    CHECK(Drew("Status: IDLE"));
    connected = true; page.onTick(); CHECK(Drew("Status: CONNECTED"));
    connected = false; page.onTick(); CHECK(Drew("Status: IDLE"));
}

TEST(panel_home_busy_feedback_and_sleep_action_work) {
    Push("{\"cmd\":\"link/pushIoCaps\",\"sd\":1,\"cd\":1}");
    UIMenuPageHome page{};
    page.init(); page.onEncoder(5); page.onButton(2, false);
    CHECK(Drew("BUSY"));
    requestedSleep = false;
    page.onEncoder(1); page.onButton(2, false);
    CHECK(requestedSleep);
    page.deinit();
}
