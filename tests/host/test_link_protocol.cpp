/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2020 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

The CTAG TBD hardware design is released under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0).
Details here: https://creativecommons.org/licenses/by-nc-sa/4.0/

CTAG TBD is provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.
***************/

// Host tests for the panel↔DSP link protocol (doc/ui-split-sdd.md P1):
// framing, version handshake, and a message round-trip through a loopback
// pair of decoders. Compiles the real protocol component with plain g++.

#include "ctag/link/protocol.hpp"

#include "test_framework.hpp"

#include <cstring>
#include <string>

using namespace CTAG::LINK;

namespace {

struct Decoder {
    std::string buf;
    FrameDecoder dec;
    explicit Decoder(size_t cap) : buf(cap, '\0'), dec(buf.data(), cap) {}
};

// Feed a frame one byte at a time; returns the payload of the last
// completed frame.
std::string FeedSplit(Decoder &d, const std::string &frame) {
    std::string last;
    for (unsigned char c : frame) {
        if (d.dec.Feed(&c, 1)) {
            last.assign(d.dec.Payload(), d.dec.Size());
        }
    }
    return last;
}

} // namespace

TEST(FrameRoundTrip) {
    Decoder d(512);
    const std::string payload = "{\"cmd\":\"/debug/getAudioStats\"}";
    const std::string got = FeedSplit(d, MakeFrame(payload));
    CHECK_STREQ(got, payload);
    CHECK(!d.dec.InFrame());
    CHECK(!d.dec.Dropped());
}

TEST(FrameRoundTripBulkFeed) {
    // Same result when the frame arrives in one chunk (real UART driver
    // delivers bursts).
    Decoder d(512);
    const std::string payload = "{\"cmd\":\"link/heartbeat\",\"up\":1234,\"seq\":7}";
    const std::string frame = MakeFrame(payload);
    CHECK(d.dec.Feed((const uint8_t *)frame.data(), frame.size()));
    CHECK_EQ(d.dec.Size(), payload.size());
    CHECK_STREQ(std::string(d.dec.Payload(), d.dec.Size()), payload);
}

TEST(JunkBetweenFramesIgnored) {
    Decoder d(512);
    const std::string payload = "{\"a\":1}";
    const std::string frame = MakeFrame(payload);
    const std::string noisy = "\x10\xff\x00junk" + frame + "\r\n\x03";
    const std::string got = FeedSplit(d, noisy);
    CHECK_STREQ(got, payload);
}

TEST(MidPayloadStxRestartsFrame) {
    // Matches DebugUI's receiver: an STX inside a payload restarts the
    // frame (cannot happen with RapidJSON-generated JSON, but must not
    // corrupt the stream if it did).
    Decoder d(512);
    const std::string partial = std::string(1, (char)kStx) + "{\"cmd\":\"link/he";
    const std::string good = MakeFrame("{\"cmd\":\"/debug/getConfig\"}");
    const std::string got = FeedSplit(d, partial + good);
    CHECK_STREQ(got, "{\"cmd\":\"/debug/getConfig\"}");
}

TEST(EtxOutsideFrameIgnored) {
    Decoder d(512);
    const std::string got = FeedSplit(d, "\x03\x03\x03" + MakeFrame("{\"a\":2}"));
    CHECK_STREQ(got, "{\"a\":2}");
}

TEST(OversizeFrameDroppedNextFrameSurvives) {
    Decoder d(16); // accepts payloads up to 15 bytes
    const std::string tooBig = MakeFrame("{\"cmd\":\"way too long for the buffer\"}");
    const std::string got = FeedSplit(d, tooBig);
    CHECK(got.empty());          // oversized frame never "completes"
    CHECK(d.dec.Dropped());      // and is reported as dropped
    const std::string ok = MakeFrame("{\"ok\":1}");
    const std::string got2 = FeedSplit(d, ok);
    CHECK_STREQ(got2, "{\"ok\":1}"); // next good frame unaffected
    CHECK(!d.dec.Dropped());
}

TEST(PartialFrameDoesNotComplete) {
    Decoder d(512);
    const std::string frame = MakeFrame("{\"partial\":true}");
    const std::string noEtx = frame.substr(0, frame.size() - 1);
    const std::string got = FeedSplit(d, noEtx);
    CHECK(got.empty());
    CHECK(d.dec.InFrame());
    // Completing the frame later works: the tail arrives in the next burst.
    d.dec.Reset();
    const std::string got2 = FeedSplit(d, frame);
    CHECK_STREQ(got2, "{\"partial\":true}");
}

TEST(EmptyPayloadFrame) {
    Decoder d(64);
    const std::string got = FeedSplit(d, std::string(1, (char)kStx) + std::string(1, (char)kEtx));
    CHECK(got.empty());
    CHECK_EQ(d.dec.Size(), (size_t)0);
}

TEST(HelloRoundTrip) {
    const std::string hello = MakeHello(Role::kPanel, "v1.2.3-abc");
    HelloReply r = ParseHandshake(hello.data(), hello.size());
    CHECK(r.valid);
    CHECK(r.accepted);
    CHECK(r.role == Role::kPanel);
    CHECK_EQ(r.major, kProtoMajor);
    CHECK_EQ(r.minor, kProtoMinor);
}

TEST(HelloAckAcceptedRoundTrip) {
    const std::string ack = MakeHelloAck(Role::kDsp, "v9.9", true, nullptr);
    HelloReply r = ParseHandshake(ack.data(), ack.size());
    CHECK(r.valid);
    CHECK(r.accepted);
    CHECK(r.role == Role::kDsp);
    CHECK_EQ(r.major, 1);
}

TEST(HelloAckRefusedCarriesReason) {
    const std::string ack = MakeHelloAck(Role::kDsp, "v9.9", false, "version mismatch");
    HelloReply r = ParseHandshake(ack.data(), ack.size());
    CHECK(r.valid);
    CHECK(!r.accepted);
    std::string reason;
    CHECK(ScanStringValue(ack.data(), ack.size(), "reason", reason));
    CHECK_STREQ(reason, "version mismatch");
}

TEST(HandshakeRejectsMalformed) {
    const char *bads[] = {
        "{\"cmd\":\"link/other\",\"role\":\"panel\",\"proto\":\"1.0\"}",
        "{\"cmd\":\"link/hello\",\"role\":\"wizard\",\"proto\":\"1.0\"}",
        "{\"cmd\":\"link/hello\",\"role\":\"panel\",\"proto\":\"x.y\"}",
        "{\"cmd\":\"link/hello\",\"role\":\"panel\"}",
        "{\"role\":\"panel\",\"proto\":\"1.0\"}",
        "not json at all",
        "",
    };
    for (const char *bad : bads) {
        HelloReply r = ParseHandshake(bad, strlen(bad));
        CHECK_MSG(!r.valid, std::string("should reject: ") + bad);
    }
    // hello_ack without the accepted flag is invalid
    const char *ackNoFlag = "{\"cmd\":\"link/hello_ack\",\"role\":\"dsp\",\"proto\":\"1.0\"}";
    CHECK(!ParseHandshake(ackNoFlag, strlen(ackNoFlag)).valid);
}

TEST(VersionGate) {
    CHECK(VersionCompatible(1));
    CHECK(!VersionCompatible(0));
    CHECK(!VersionCompatible(2));
}

TEST(ScanHelpers) {
    const char *doc = "{\"cmd\":\"link/stream\",\"vu\":[1,2],\"seq\":42,\"neg\":-7,\"flag\":true,\"off\":false,\"name\":\"panel\"}";
    long long v = 0;
    CHECK(ScanIntValue(doc, strlen(doc), "seq", v) && v == 42);
    CHECK(ScanIntValue(doc, strlen(doc), "neg", v) && v == -7);
    CHECK(!ScanIntValue(doc, strlen(doc), "missing", v));
    bool b = false;
    CHECK(ScanBoolValue(doc, strlen(doc), "flag", b) && b);
    CHECK(ScanBoolValue(doc, strlen(doc), "off", b) && !b);
    CHECK(!ScanBoolValue(doc, strlen(doc), "seq", b)); // number is not bool
    std::string s;
    CHECK(ScanStringValue(doc, strlen(doc), "name", s) && s == "panel");
    CHECK(!ScanStringValue(doc, strlen(doc), "seq", s)); // number is not string
    // Keys match exactly (needle includes the closing quote).
    CHECK(!ScanBoolValue(doc, strlen(doc), "se", b));
    CHECK(!ScanBoolValue(doc, strlen(doc), "seqx", b));
}

TEST(IsCommandName) {
    CHECK(IsCommandName("/debug/getAudioStats"));
    CHECK(IsCommandName("/link/presetSave"));
    CHECK(!IsCommandName("link/heartbeat"));   // push, not a command
    CHECK(!IsCommandName("debug/missing-slash"));
    CHECK(!IsCommandName("/debugx/foo"));
    CHECK(!IsCommandName("/linkx/foo"));
    CHECK(!IsCommandName(""));
}

TEST(HeartbeatAndErrorShapes) {
    const std::string hb = MakeHeartbeat(98765, 12);
    long long up = 0, seq = 0;
    CHECK(ScanIntValue(hb.data(), hb.size(), "up", up) && up == 98765);
    CHECK(ScanIntValue(hb.data(), hb.size(), "seq", seq) && seq == 12);
    std::string cmd;
    CHECK(ScanStringValue(hb.data(), hb.size(), "cmd", cmd));
    CHECK_STREQ(cmd, "link/heartbeat");

    const std::string err = MakeError("dsp offline");
    CHECK(ScanStringValue(err.data(), err.size(), "what", cmd));
    CHECK_STREQ(cmd, "dsp offline");
}

TEST(LoopbackPairHandshakeAndCommand) {
    // Full P1 choreography over one wire in each direction, the shape the
    // real firmware tasks will run:
    //   panel --hello--> dsp: dsp parses, checks version, acks
    //   dsp --hello_ack--> panel: panel parses, accepted
    //   panel --command--> dsp: dsp's shared dispatcher (stubbed here)
    //                       answers, panel gets the response
    Decoder panelRx(4096), dspRx(4096);

    // panel -> dsp
    CHECK(dspRx.dec.Feed((const uint8_t *)MakeFrame(MakeHello(Role::kPanel, "uimcu-test")).data(),
                         MakeFrame(MakeHello(Role::kPanel, "uimcu-test")).size()));
    HelloReply hello = ParseHandshake(dspRx.dec.Payload(), dspRx.dec.Size());
    CHECK(hello.valid);
    CHECK(VersionCompatible(hello.major));
    CHECK(hello.role == Role::kPanel);

    // dsp -> panel
    const std::string ack = MakeFrame(MakeHelloAck(Role::kDsp, "dsp-test", true, nullptr));
    CHECK(panelRx.dec.Feed((const uint8_t *)ack.data(), ack.size()));
    HelloReply ackReply = ParseHandshake(panelRx.dec.Payload(), panelRx.dec.Size());
    CHECK(ackReply.valid);
    CHECK(ackReply.accepted);
    CHECK(ackReply.role == Role::kDsp);

    // panel command -> dsp dispatch -> response -> panel
    const std::string cmdPayload = "{\"cmd\":\"/debug/getAudioStats\"}";
    CHECK(dspRx.dec.Feed((const uint8_t *)MakeFrame(cmdPayload).data(),
                         MakeFrame(cmdPayload).size()));
    std::string cmdName;
    CHECK(ScanStringValue(dspRx.dec.Payload(), dspRx.dec.Size(), "cmd", cmdName));
    CHECK(IsCommandName(cmdName.c_str()));
    // The real handler is DebugUI::handleCommand; here a fixed stub reply.
    const std::string response = "{\"cpuOverload\":0}";
    CHECK(panelRx.dec.Feed((const uint8_t *)MakeFrame(response).data(),
                           MakeFrame(response).size()));
    CHECK_STREQ(std::string(panelRx.dec.Payload(), panelRx.dec.Size()), response);
}

TEST(MakeFrameMatchesBenchChannelLayout) {
    // The framing must stay byte-compatible with DebugUI's writeFramed():
    // STX, raw payload, ETX — no escaping, no length prefix.
    const std::string payload = "{\"cmd\":\"/debug/getUiState\"}";
    const std::string frame = MakeFrame(payload);
    CHECK_EQ(frame.size(), payload.size() + 2);
    CHECK_EQ(frame.front(), (char)0x02);
    CHECK_EQ(frame.back(), (char)0x03);
    CHECK_STREQ(frame.substr(1, payload.size()), payload);
}
