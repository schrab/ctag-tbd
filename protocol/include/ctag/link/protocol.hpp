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

// Link protocol between the panel MCU (ESP32-C3, owns OLED/encoders/buttons)
// and the DSP MCU (ESP32-A1S, owns audio/codec/SD). See doc/ui-split-sdd.md §4.
//
// Transport: 921600 8N1 UART. Framing is the proven STX/ETX JSON of the
// UART0 bench channel (DebugUI): one JSON object per frame, STX (0x02)
// before it, ETX (0x03) after. Payloads are produced by RapidJSON, which
// escapes control characters, so raw STX/ETX bytes never occur inside a
// payload; the decoder still treats a mid-payload STX as "restart frame",
// matching DebugUI's receiver.
//
// Message names are the contract (no integer ids): DSP commands reuse the
// "/debug/..." bench names verbatim so one dispatcher serves both
// transports; link-internal messages (handshake, heartbeat, pushes, MIDI
// transport, SD proxy) use the "link/" prefix, and new panel-driven product
// commands use "/link/...". This header is IDF-free: it compiles with plain
// g++ for host tests, and as an IDF component on both firmwares.
//
// All JSON generated here is flat, fixed-shape, and hand-rolled on purpose:
// the protocol layer has no RapidJSON dependency. Command/push payloads are
// opaque strings passed through the framing; their shapes belong to the
// dispatcher (DebugUI::handleCommand) and the DspProxy cache, not here.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace CTAG {
    namespace LINK {

// Protocol version. Major breaks on framing/handshake changes; minor bumps
// are additive (new message names only). The handshake refuses mismatched
// majors so a mismatched firmware pair fails loudly instead of silently
// mis-parsing (SDD §10, dual-flash choreography).
constexpr uint8_t kProtoMajor = 1;
constexpr uint8_t kProtoMinor = 0;

enum class Role : uint8_t { kPanel = 1, kDsp = 2 };

const char *RoleName(Role role);

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------

constexpr uint8_t kStx = 0x02;
constexpr uint8_t kEtx = 0x03;

// Protocol-level sanity limit for one payload. This is not a buffer size:
// owners size their own decoder buffers (the bench channel caps at 4096,
// link boot-metadata pushes carry up to tens of KB, SDD §4.4).
constexpr size_t kMaxPayload = 65536;

// Streaming frame decoder over a caller-owned buffer. One instance per
// stream direction; not reentrant.
class FrameDecoder {
public:
    // buf must outlive this decoder; capacity includes the NUL terminator,
    // so the largest accepted payload is capacity - 1 bytes.
    FrameDecoder(char *buf, size_t capacity);

    // Feed wire bytes. Returns true when a complete frame has arrived; the
    // payload (Payload()/Size()) is then valid until the next Feed() call.
    // Bytes outside a frame are ignored; a mid-payload STX restarts the
    // frame. A payload larger than capacity is discarded (Dropped()).
    bool Feed(const uint8_t *data, size_t len);

    const char *Payload() const { return buf_; }
    size_t Size() const { return size_; }
    bool InFrame() const { return inFrame_; }

    // The frame currently streaming, or the one just completed, was
    // discarded because it exceeded capacity. Cleared by the next STX, so
    // one oversized frame never masks the next good one.
    bool Dropped() const { return dropped_; }

    // Drop a partial frame and both flags.
    void Reset();

private:
    char *buf_;
    size_t capacity_;
    size_t size_;
    bool inFrame_;
    bool dropped_;
};

// Append one framed message (STX + payload + ETX) to out.
void AppendFrame(const char *payload, size_t len, std::string &out);
std::string MakeFrame(const char *payload, size_t len);
std::string MakeFrame(const std::string &payload);

// ---------------------------------------------------------------------------
// Message names
// ---------------------------------------------------------------------------

// Handshake + liveness (both directions).
constexpr const char *kMsgHello     = "link/hello";
constexpr const char *kMsgHelloAck  = "link/hello_ack";
constexpr const char *kMsgHeartbeat = "link/heartbeat";
constexpr const char *kMsgError     = "link/error";

// DSP → panel pushes. Boot metadata burst (once after handshake) plus
// on-change pushes; the panel cache is invalidated by these.
constexpr const char *kMsgPushPluginList   = "link/pushPluginList";
constexpr const char *kMsgPushParams       = "link/pushParams";
constexpr const char *kMsgPushParamValue   = "link/pushParamValue";
constexpr const char *kMsgPushActivePlugin = "link/pushActivePlugin";
constexpr const char *kMsgPushConfig       = "link/pushConfig";
constexpr const char *kMsgPushPresets      = "link/pushPresets";
constexpr const char *kMsgPushFavorites    = "link/pushFavorites";
constexpr const char *kMsgPushMarbles      = "link/pushMarbles";
constexpr const char *kMsgPushIoCaps       = "link/pushIoCaps";

// Streaming @10 Hz (SDD §4.2): VU peaks, Marbles output snapshot bitfield,
// CPU overload flag.
constexpr const char *kMsgStream = "link/stream";

// SD proxy: "/link/fsList" request from the panel, "link/fsList" listing
// push back; mounted state pushed on change.
constexpr const char *kMsgFsList  = "link/fsList";
constexpr const char *kMsgFsState = "link/fsState";

// MIDI transport: raw MIDI bytes as hex arrays. midiIn is panel → DSP
// (DIN in + BLE), midiOut is DSP → panel (DIN out forwarding).
constexpr const char *kMsgMidiIn  = "link/midiIn";
constexpr const char *kMsgMidiOut = "link/midiOut";

// Panel → DSP product commands (thin DspProxy wrappers, SDD §4.1). Served
// by the same dispatcher as the "/debug/..." set.
constexpr const char *kMsgCmdPresetSave       = "/link/presetSave";
constexpr const char *kMsgCmdPresetLoad       = "/link/presetLoad";
constexpr const char *kMsgCmdFavoriteActivate = "/link/favoriteActivate";
constexpr const char *kMsgCmdSdStartPlayback  = "/link/sdStartPlayback";
constexpr const char *kMsgCmdSdStartRecording = "/link/sdStartRecording";
constexpr const char *kMsgCmdSdStop           = "/link/sdStop";
constexpr const char *kMsgCmdMarblesGet       = "/link/marblesGet";
constexpr const char *kMsgCmdMarblesSet       = "/link/marblesSet";
constexpr const char *kMsgCmdMidiFlags        = "/link/midiFlags";

// True for any message a DSP dispatcher should execute: the bench set
// ("/debug/...") and the link product commands ("/link/...").
bool IsCommandName(const char *name);

// ---------------------------------------------------------------------------
// Flat-JSON scanning helpers (fixed shapes we generate ourselves)
// ---------------------------------------------------------------------------

// Find `"key"` at the top level of a flat object and copy its string value
// to out. No escape processing beyond the closing quote; values we emit
// never contain quotes or backslashes.
bool ScanStringValue(const char *payload, size_t len, const char *key,
                     std::string &out);
// Same for an integer (long long) value.
bool ScanIntValue(const char *payload, size_t len, const char *key,
                  long long &out);
// Same for a boolean (true/false) value.
bool ScanBoolValue(const char *payload, size_t len, const char *key, bool &out);

// ---------------------------------------------------------------------------
// Handshake
// ---------------------------------------------------------------------------

// {"cmd":"link/hello","role":"panel","proto":"1.0","fw":"<fw>"}
std::string MakeHello(Role role, const char *fwVersion);
// {"cmd":"link/hello_ack","role":"dsp","proto":"1.0","fw":"<fw>",
//  "accepted":true|false,"reason":"..."}   (reason only when refused)
std::string MakeHelloAck(Role role, const char *fwVersion, bool accepted,
                         const char *refusalReason);

struct HelloReply {
    bool valid;       // well-formed link/hello or link/hello_ack
    bool isAck;       // true when the message was link/hello_ack
    bool accepted;    // hello_ack only; true for a plain hello
    Role role;
    uint8_t major;
    uint8_t minor;
};

// Parse a handshake message. valid=false unless the cmd is link/hello or
// link/hello_ack with a parseable role + proto.
HelloReply ParseHandshake(const char *payload, size_t len);

// Major must match; minor is informational (additive changes only).
bool VersionCompatible(uint8_t remoteMajor);

// {"cmd":"link/heartbeat","up":<ms>,"seq":<n>}
std::string MakeHeartbeat(uint32_t uptimeMs, uint32_t seq);

// {"cmd":"link/error","what":"..."}
std::string MakeError(const char *what);

    } // namespace LINK
} // namespace CTAG
