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

#include "ctag/link/protocol.hpp"

#include <cstring>

namespace CTAG {
    namespace LINK {

const char *RoleName(Role role) {
    switch (role) {
        case Role::kPanel: return "panel";
        case Role::kDsp: return "dsp";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------

FrameDecoder::FrameDecoder(char *buf, size_t capacity)
    : buf_(buf), capacity_(capacity), size_(0), inFrame_(false),
      dropped_(false) {
    Reset();
}

void FrameDecoder::Reset() {
    size_ = 0;
    inFrame_ = false;
    dropped_ = false;
    if (buf_ != nullptr && capacity_ > 0) buf_[0] = '\0';
}

bool FrameDecoder::Feed(const uint8_t *data, size_t len) {
    bool completed = false;
    for (size_t i = 0; i < len; ++i) {
        const uint8_t c = data[i];
        if (c == kStx) {
            // Start (or restart) a frame; the partial one is abandoned. A
            // fresh frame also gets a fresh loss budget, so one oversized
            // frame cannot mask the next good one.
            size_ = 0;
            inFrame_ = true;
            dropped_ = false;
            continue;
        }
        if (c == kEtx) {
            if (inFrame_) {
                inFrame_ = false;
                if (!dropped_) completed = true;
            }
            // ETX outside a frame: ignore.
            continue;
        }
        if (!inFrame_) continue; // junk between frames
        if (size_ + 1 >= capacity_) {
            // Keep the frame marked in-progress so a trailing ETX cannot
            // "complete" an over-long payload.
            dropped_ = true;
            continue;
        }
        buf_[size_++] = static_cast<char>(c);
        buf_[size_] = '\0';
    }
    return completed;
}

void AppendFrame(const char *payload, size_t len, std::string &out) {
    out.reserve(out.size() + len + 2);
    out.push_back(static_cast<char>(kStx));
    out.append(payload, len);
    out.push_back(static_cast<char>(kEtx));
}

std::string MakeFrame(const char *payload, size_t len) {
    std::string out;
    AppendFrame(payload, len, out);
    return out;
}

std::string MakeFrame(const std::string &payload) {
    return MakeFrame(payload.data(), payload.size());
}

// ---------------------------------------------------------------------------
// Message names
// ---------------------------------------------------------------------------

bool IsCommandName(const char *name) {
    if (name == nullptr) return false;
    return strncmp(name, "/debug/", 7) == 0 || strncmp(name, "/link/", 6) == 0;
}

// ---------------------------------------------------------------------------
// Flat-JSON scanning helpers
// ---------------------------------------------------------------------------

namespace {

// Advance past whitespace; returns nullptr at end of range.
const char *SkipWs(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return p;
}

// Find `"key"` followed by ':' and return the position of the first
// non-whitespace value character, or nullptr. Only flat objects are
// supported by design; nested keys would be matched textually, which is
// fine because the shapes scanned here are ours and fixed.
const char *FindValue(const char *payload, size_t len, const char *key) {
    if (payload == nullptr || len == 0 || key == nullptr) return nullptr;
    const char *end = payload + len;
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    const size_t needleLen = needle.size();
    for (const char *p = payload; p + needleLen < end + 1; ++p) {
        if (memcmp(p, needle.data(), needleLen) != 0) continue;
        const char *v = SkipWs(p + needleLen, end);
        if (v < end && *v == ':') {
            v = SkipWs(v + 1, end);
            if (v < end) return v;
        }
    }
    return nullptr;
}

} // namespace

bool ScanStringValue(const char *payload, size_t len, const char *key,
                     std::string &out) {
    const char *end = payload + len;
    const char *v = FindValue(payload, len, key);
    if (v == nullptr || *v != '"') return false;
    ++v; // opening quote
    out.clear();
    while (v < end && *v != '"') {
        out.push_back(*v++);
    }
    return v < end; // false if the closing quote was missing
}

bool ScanIntValue(const char *payload, size_t len, const char *key,
                  long long &out) {
    const char *end = payload + len;
    const char *v = FindValue(payload, len, key);
    if (v == nullptr || v >= end) return false;
    bool negative = false;
    if (*v == '-') {
        negative = true;
        ++v;
    } else if (*v == '+') {
        ++v;
    }
    if (v >= end || *v < '0' || *v > '9') return false;
    long long value = 0;
    while (v < end && *v >= '0' && *v <= '9') {
        value = value * 10 + (*v - '0');
        ++v;
    }
    out = negative ? -value : value;
    return true;
}

bool ScanBoolValue(const char *payload, size_t len, const char *key,
                   bool &out) {
    const char *end = payload + len;
    const char *v = FindValue(payload, len, key);
    if (v == nullptr) return false;
    if (end - v >= 4 && strncmp(v, "true", 4) == 0) {
        out = true;
        return true;
    }
    if (end - v >= 5 && strncmp(v, "false", 5) == 0) {
        out = false;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Handshake
// ---------------------------------------------------------------------------

namespace {

std::string MakeHandshake(const char *cmd, Role role, const char *fwVersion,
                          const char *acceptedField) {
    std::string s = "{\"cmd\":\"";
    s += cmd;
    s += "\",\"role\":\"";
    s += RoleName(role);
    s += "\",\"proto\":\"";
    s += static_cast<char>('0' + kProtoMajor);
    s += '.';
    s += static_cast<char>('0' + kProtoMinor);
    s += "\",\"fw\":\"";
    s += (fwVersion != nullptr ? fwVersion : "");
    s += "\"";
    if (acceptedField != nullptr) s += acceptedField;
    s += "}";
    return s;
}

} // namespace

std::string MakeHello(Role role, const char *fwVersion) {
    return MakeHandshake(kMsgHello, role, fwVersion, nullptr);
}

std::string MakeHelloAck(Role role, const char *fwVersion, bool accepted,
                         const char *refusalReason) {
    std::string extra = ",\"accepted\":";
    extra += accepted ? "true" : "false";
    if (!accepted && refusalReason != nullptr) {
        extra += ",\"reason\":\"";
        extra += refusalReason;
        extra += "\"";
    }
    return MakeHandshake(kMsgHelloAck, role, fwVersion, extra.c_str());
}

HelloReply ParseHandshake(const char *payload, size_t len) {
    HelloReply r{};
    r.valid = false;
    r.accepted = false;
    r.role = Role::kPanel;
    r.major = 0;
    r.minor = 0;
    if (payload == nullptr) return r;

    std::string cmd;
    if (!ScanStringValue(payload, len, "cmd", cmd)) return r;
    const bool isHello = cmd == kMsgHello;
    const bool isAck = cmd == kMsgHelloAck;
    if (!isHello && !isAck) return r;

    std::string roleStr;
    if (!ScanStringValue(payload, len, "role", roleStr)) return r;
    if (roleStr == "panel") r.role = Role::kPanel;
    else if (roleStr == "dsp") r.role = Role::kDsp;
    else return r;

    std::string proto;
    if (!ScanStringValue(payload, len, "proto", proto)) return r;
    // "M.m" with literal digits only.
    if (proto.size() < 3 || proto[1] != '.') return r;
    if (proto[0] < '0' || proto[0] > '9' || proto[2] < '0' || proto[2] > '9')
        return r;
    r.major = static_cast<uint8_t>(proto[0] - '0');
    r.minor = static_cast<uint8_t>(proto[2] - '0');

    if (isAck) {
        bool accepted = false;
        if (!ScanBoolValue(payload, len, "accepted", accepted)) return r;
        r.accepted = accepted;
    } else {
        r.accepted = true;
    }
    r.valid = true;
    return r;
}

bool VersionCompatible(uint8_t remoteMajor) {
    return remoteMajor == kProtoMajor;
}

std::string MakeHeartbeat(uint32_t uptimeMs, uint32_t seq) {
    std::string s = "{\"cmd\":\"";
    s += kMsgHeartbeat;
    s += "\",\"up\":";
    s += std::to_string(uptimeMs);
    s += ",\"seq\":";
    s += std::to_string(seq);
    s += "}";
    return s;
}

std::string MakeError(const char *what) {
    std::string s = "{\"cmd\":\"";
    s += kMsgError;
    s += "\",\"what\":\"";
    s += (what != nullptr ? what : "");
    s += "\"}";
    return s;
}

    } // namespace LINK
} // namespace CTAG
