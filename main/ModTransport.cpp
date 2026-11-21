#include "ModTransport.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include "stmlib/utils/gate_flags.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#if __has_include(<esp_timer.h>)
#include <esp_log.h>
#include <esp_timer.h>
#define MT_LOGE(...) ESP_LOGE("MODTR", ##__VA_ARGS__)
#define MT_LOGI(...) ESP_LOGI("MODTR", ##__VA_ARGS__)
#else
#define MT_LOGE(...) ((void)0)
#define MT_LOGI(...) ((void)0)
#define MODTRANSPORT_HOST 1
#endif

namespace CTAG::DRIVERS {

namespace {
    std::atomic<float> bpm{120.0f};
    std::atomic<float> midiBpmEma{-1.0f};
    int64_t   lastMidiTickUs = 0;
    std::mutex midiTempoMutex;
    std::atomic<uint32_t> pendingClockEdges{0};  // MIDI task -> audio task
    std::atomic<bool> midiRunning{false};
    std::atomic<bool> startResetPending{false};   // MIDI task -> audio task mailbox
    std::atomic<bool> startResetThisBlock{false}; // per-block FA broadcast (both engines)
    std::atomic<bool> globalClockMidi{false};     // SEQ "Global" sources follow MIDI clock
    std::atomic<int> ticksThisBlock{0};
    std::atomic<uint8_t> extGateFlags{stmlib::GATE_FLAG_LOW};

    float ClampBPM(float v) { return v < 20.0f ? 20.0f : (v > 480.0f ? 480.0f : v); }

    // --- mod-config.jsn section file IO (host-testable via a path hook) ---
    // The file path is overridable so host tests exercise the merge logic on
    // a scratch file without SPIFFS.
    const char* ConfigPath() {
#if defined(MODTRANSPORT_CFG_PATH)
        return MODTRANSPORT_CFG_PATH;
#else
        return "/spiffs/data/mod-config.jsn";
#endif
    }

    std::string ReadFileOrEmpty(const char* path) {
        FILE* fp = fopen(path, "r");
        if (!fp) return std::string();
        std::string data;
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) data.append(buf, n);
        fclose(fp);
        return data;
    }

    // Write to a sibling temp file, then rename over the target — a crash
    // mid-write leaves the previous config intact.
    bool WriteFileAtomic(const char* path, const std::string& data) {
        std::string tmp(path);
        tmp += ".tmp";
        FILE* fp = fopen(tmp.c_str(), "w");
        if (!fp) {
            MT_LOGE("WriteFileAtomic: cannot open %s", tmp.c_str());
            return false;
        }
        size_t n = fwrite(data.data(), 1, data.size(), fp);
        fclose(fp);
        if (n != data.size()) {
            MT_LOGE("WriteFileAtomic: short write on %s", tmp.c_str());
            remove(tmp.c_str());
            return false;
        }
        if (rename(tmp.c_str(), path) != 0) {
            MT_LOGE("WriteFileAtomic: rename %s -> %s failed", tmp.c_str(), path);
            remove(tmp.c_str());
            return false;
        }
        return true;
    }

    // Parse the file and deep-copy member "name" into out. false = file/
    // section missing or malformed. Heap DOM — callers run on the UIMenu or
    // link task, never on the audio path (same discipline as
    // MarblesEngine::LoadConfig today).
    bool ReadSectionDoc(const char* name, rapidjson::Document& out) {
        std::string all = ReadFileOrEmpty(ConfigPath());
        if (all.empty()) return false;
        rapidjson::Document d;
        if (d.Parse(all.c_str()).HasParseError() || !d.IsObject()) return false;
        auto it = d.FindMember(name);
        if (it == d.MemberEnd() || !it->value.IsObject()) return false;
        out.SetObject();
        out.CopyFrom(it->value, out.GetAllocator());
        return true;
    }

    template <typename Doc>
    float ReadBpmFromDoc(const Doc& d) {
        if (d.IsObject() && d.HasMember("bpm") && d["bpm"].IsNumber())
            return ClampBPM((float)d["bpm"].GetDouble());
        return -1.0f;
    }
}

void ModTransport::Init() {
    bpm.store(120.0f, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(midiTempoMutex);
        midiBpmEma.store(-1.0f, std::memory_order_relaxed);
        lastMidiTickUs = 0;
    }
    pendingClockEdges.store(0, std::memory_order_relaxed);
    midiRunning.store(false, std::memory_order_relaxed);
    startResetPending.store(false, std::memory_order_relaxed);
    startResetThisBlock.store(false, std::memory_order_relaxed);
    globalClockMidi.store(false, std::memory_order_relaxed);
    extGateFlags.store(stmlib::GATE_FLAG_LOW, std::memory_order_relaxed);
    ticksThisBlock.store(0, std::memory_order_relaxed);
    LoadConfig();
}

void ModTransport::OnMidiClock() {
#if !defined(MODTRANSPORT_HOST)
    OnMidiClockAt(esp_timer_get_time());
#endif
}

void ModTransport::OnMidiClockAt(int64_t now_us) {
    pendingClockEdges.fetch_add(1, std::memory_order_relaxed);
    // Measured-tempo EMA (2.5..1000 ms/tick => 15..6000 BPM window).
    // Keep the timestamp and EMA as one short MIDI-side critical section;
    // publish scalar results atomically for the UI/audio readers.
    std::lock_guard<std::mutex> lock(midiTempoMutex);
    if (lastMidiTickUs != 0) {
        float ms = (float)(now_us - lastMidiTickUs) / 1000.0f;
        if (ms >= 2.5f && ms <= 1000.0f) {
            float priorEma = midiBpmEma.load(std::memory_order_relaxed);
            float inst = 60000.0f / (ms * 24.0f);
            float nextEma = priorEma < 0.0f ? inst : (0.9f * priorEma + 0.1f * inst);
            midiBpmEma.store(nextEma, std::memory_order_relaxed);
            bpm.store(ClampBPM(nextEma), std::memory_order_relaxed);
        }
    }
    lastMidiTickUs = now_us;
}

void ModTransport::OnMidiStart() {
    midiRunning.store(true, std::memory_order_relaxed);
    startResetPending.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(midiTempoMutex);
        midiBpmEma.store(-1.0f, std::memory_order_relaxed);
        lastMidiTickUs = 0;
    }
}

void ModTransport::OnMidiStop() {
    midiRunning.store(false, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(midiTempoMutex);
    lastMidiTickUs = 0;
}

void ModTransport::Process(float dt) {
    (void)dt;
    // Drain ALL pending edges in one pass. The count is published regardless
    // of the running flag: Marbles' RampExtractor syncs to raw F8 bytes (FA
    // only re-phases), exactly like the pre-hoist drain; MIDI-clocked Seq
    // sources apply their own freeze while stopped. Practical rate is
    // <= 192 Hz against a >= 1000 Hz block rate, so 0 or 1 per block;
    // higher is still correct (edges coalesce into one block count).
    uint32_t e = pendingClockEdges.exchange(0, std::memory_order_acq_rel);
    ticksThisBlock.store((int)e, std::memory_order_relaxed);
    // FA start-reset is a per-block BROADCAST, not a consumable latch: both
    // engines must see the same pulse (double-consume is the documented bug
    // class). exchange consumes the mailbox atomically so a concurrent new
    // start remains pending for the next block instead of being overwritten.
    startResetThisBlock.store(startResetPending.exchange(false, std::memory_order_acq_rel),
                              std::memory_order_release);
    // Marbles gate-flag chain: ONE block-high pulse per drained tick max
    // (same collapse rule as the pre-hoist single-edge drain; keeps the
    // RampExtractor feed identical).
    SetExternalGateLevel(e > 0);
}

int ModTransport::TicksThisBlock() { return ticksThisBlock.load(std::memory_order_relaxed); }
bool ModTransport::IsRunning() { return midiRunning.load(std::memory_order_relaxed); }
bool ModTransport::TakeStartReset() { return startResetThisBlock.load(std::memory_order_acquire); }
uint8_t ModTransport::ExternalGateFlags() { return extGateFlags.load(std::memory_order_relaxed); }
void ModTransport::SetExternalGateLevel(bool high) {
    const uint8_t previous = extGateFlags.load(std::memory_order_relaxed);
    const uint8_t next = stmlib::ExtractGateFlags((stmlib::GateFlags)previous, high);
    extGateFlags.store(next, std::memory_order_relaxed);
}
float ModTransport::GetBPM() { return bpm.load(std::memory_order_relaxed); }
void ModTransport::SetBPM(float v) { bpm.store(ClampBPM(v), std::memory_order_relaxed); }
float ModTransport::GetMeasuredBPM() { return midiBpmEma.load(std::memory_order_relaxed); }
bool ModTransport::GetGlobalClockMidi() { return globalClockMidi.load(std::memory_order_relaxed); }
void ModTransport::SetGlobalClockMidi(bool v) { globalClockMidi.store(v, std::memory_order_relaxed); }

// --- mod-config.jsn section IO: read file -> parse -> attach member ->
// write back atomically. Host tests point MODTRANSPORT_CFG_PATH at a
// scratch file; run_tests.sh wipes it before the run. ---

bool ModTransport::ReadSectionJSON(const char *name, char *dst, int cap) {
    dst[0] = 0;
    std::string all = ReadFileOrEmpty(ConfigPath());
    if (all.empty()) return false;
    rapidjson::Document d;
    if (d.Parse(all.c_str()).HasParseError() || !d.IsObject()) return false;
    auto it = d.FindMember(name);
    // Objects are the normal case; legacy mod-config.jsn stores "ccSlots"
    // as a BARE ARRAY — hand it through verbatim so the learned CC bindings
    // survive the upgrade.
    if (it == d.MemberEnd() || (!it->value.IsObject() && !it->value.IsArray()))
        return false;
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    it->value.Accept(w);
    size_t n = sb.GetSize();
    if ((int)n >= cap) return false;
    memcpy(dst, sb.GetString(), n + 1);
    return true;
}

bool ModTransport::WriteSectionJSON(const char *name, const char *json) {
    std::string all = ReadFileOrEmpty(ConfigPath());
    rapidjson::Document root;
    if (all.size() > 2) root.Parse(all.c_str());
    if (root.HasParseError() || !root.IsObject()) root.SetObject();
    rapidjson::Document frag;
    if (frag.Parse(json).HasParseError() || !frag.IsObject()) return false;
    auto it = root.FindMember(name);
    if (it != root.MemberEnd()) {
        it->value.CopyFrom(frag, root.GetAllocator());
    } else {
        rapidjson::Value key(name, root.GetAllocator());
        rapidjson::Value val;
        val.CopyFrom(frag, root.GetAllocator());
        root.AddMember(key, val, root.GetAllocator());
    }
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    root.Accept(w);
    return WriteFileAtomic(ConfigPath(), std::string(sb.GetString(), sb.GetSize()));
}

void ModTransport::LoadConfig() {
    // "transport" is the owner; fall back to legacy marbles.bpm so existing
    // cards keep their tempo. Parsed via heap DOM (the real marbles object
    // is ~800 B serialized — larger than any stack buffer we want on the
    // 4 KB UIMenu task).
    rapidjson::Document sec;
    float v = -1.0f;
    if (ReadSectionDoc("transport", sec)) {
        v = ReadBpmFromDoc(sec);
        if (sec.IsObject() && sec.HasMember("ext") && sec["ext"].IsInt())
            globalClockMidi.store(sec["ext"].GetInt() != 0, std::memory_order_relaxed);
    }
    if (v < 0.0f && ReadSectionDoc("marbles", sec)) {
        v = ReadBpmFromDoc(sec);
    }
    if (v >= 0.0f) {
        bpm.store(v, std::memory_order_relaxed);
        MT_LOGI("LoadConfig: bpm=%.1f", bpm.load(std::memory_order_relaxed));
    }
}

void ModTransport::SaveConfig() {
    const float savedBpm = bpm.load(std::memory_order_relaxed);
    const bool savedGlobalClockMidi = globalClockMidi.load(std::memory_order_relaxed);
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"bpm\":%.1f,\"ext\":%d}",
             static_cast<double>(savedBpm), savedGlobalClockMidi ? 1 : 0);
    WriteSectionJSON("transport", buf);
}

} // namespace CTAG::DRIVERS
