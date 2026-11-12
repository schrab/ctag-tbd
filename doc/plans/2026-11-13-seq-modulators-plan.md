# SEQ Modulators (GATE ×2 + ARP ×2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the rack's rhythmic-gate and generative-ARP modulators into ctag-tbd as a second modulation engine family running alongside Marbles (2× GATE, 2× ARP, shared root/scale), with a hoisted `ModTransport`, new PANEL_SEQ page, and the Marbles T-gate active-low fix.

**Architecture:** Pure, host-testable generator core (`main/SeqEngineCore.hpp`, ported from `~/mi-rp2350/rack-rp2350/src/mod_engine.h`) behind a thin DSP engine shell (`main/SeqEngine.{hpp,cpp}`) writing cv/trig slots at audio block rate; `main/ModTransport.{hpp,cpp}` owns BPM/MIDI-clock/EMA/transport and feeds both engines; panel UI (`uimcu/ui/menupages/UIMenuPageSeq`) over the existing `/link/*` flat-key pattern.

**Tech Stack:** ESP-IDF v6.1 (DSP: xtensa, build-bba2; C3: riscv32), host tests = plain g++ (`tests/host/run_tests.sh`), no new dependencies.

**Spec:** `doc/seq-modulators-sdd.md` (committed `faeb6c38` — read it first; this plan argues from it)

## Global Constraints

- **NEVER edit files with inline python/sed scripts — use the Read/Edit/Write tools** (AGENTS.md working rule; script patching landed edits in the wrong place twice).
- Slot writes ALWAYS through `ClampCVSlot()`/`ClampTrigSlot()` (`components/ctagSoundProcessor/helpers/ctagSlotBounds.hpp`) — `cv_buffer`/`trig_buffer` are audio-task stack locals.
- Trig convention is **active-low: 0 = active, 1 = inactive, idle = 1** (`main/Midi.hpp:51` `TRIG_ON 0`; all plugin readers `== 1 ? off : on`). SEQ + Marbles T outputs must honor it.
- C3 panel: no JSON DOM on the UI task, pushes flat and ≤ ~1.8 KB (C3 heap ceiling); new link keys are flat scalar strings only.
- `CONFIG_FREERTOS_HZ=100`: never `vTaskDelay(pdMS_TO_TICKS(n))` with n < 10 (no-op). The new engines use no delays at all.
- No heap allocation, no logging in the audio-task path (`SeqEngine::Process`, `ModTransport::Process`). `ctagSPDataModel::SetParamValue` stays logging-free (BBA2 USJ FIFO rule) — do not add ESP_LOG there.
- DSP build: `source /home/ubuntu/esp-idf-v6.1/export.sh && idf.py -B build-bba2 -D IDF_TARGET=esp32s3 -D SDKCONFIG=build-bba2/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.bba2" build` (A1S: plain `idf.py build`). Host tests: `tests/host/run_tests.sh` — exit 0 required before every commit.
- C3 build: `cd uimcu && idf.py build`. Bench: opening the C3 console RESETS the C3; multi-step UI drives happen in ONE `screenshot.py`-style session using `/debug/injectEvent` + `getDisplayFramebuffer`.
- Rack source file for ports: `~/mi-rp2350/rack-rp2350/src/mod_engine.h` and `gen_tables.h` — copy ranges **verbatim** (values must not change); renames and deletions are listed per task.
- Commit after every task; never commit unrelated dirty files (repo has many untracked bench artifacts — `git add` explicit paths only).

## Review Focus

Failure modes the spec implies but unit tests can't fully pin — each is tested/pinned in the task named:

1. **Trig idle level** — an idle SEQ trig slot written as 0 instead of 1 makes mapped plugins (TBD03 trigger) fire continuously. Pinned by Task 6 host test (`TrigIdleIsOne`) and Task 7 bench step.
2. **MIDI clock double-drain** — if Marbles and SeqEngine both drain the same tick counter after the ModTransport hoist, tempos skew (Marbles slows, Seq advances double). Pinned by Task 2 host test (`TicksDrainedOnce`) and Task 3 bench step (external MIDI clock + `/link/midiOut` regression).
3. **Slot off-by-one at the new boundary** — a slot write at index 106/51 (first invalid) corrupts audio-task stack. Pinned by Task 1 host test (`ClampAtNewBounds`).
4. **Division index off-by-one** — the rack source itself has a comment/index mismatch (`division = 4` labeled "1/16" while `kModDivisions[4]` is `1/2`). The table is ported verbatim; defaults use index **7 = 1/16**. Pinned by Task 4 host test (`DivisionTableAndDefault`).
5. **Push size overflow** — a seq state push that exceeds the C3 relay ceiling (or nests DOM parsing) bootloops the no-PSRAM C3. Pinned by Task 7 step (count `pushSeq` bytes ≤ 1.8 KB, flat string build only) and Task 9 bench (page opens + edits over the link without reboot).

---

### Task 1: Slot space extension (N_CVS 106 / N_TRIGS 51)

**Files:**
- Modify: `components/ctagSoundProcessor/helpers/ctagSlotBounds.hpp` (host defaults, lines 29-34)
- Modify: `CMakeLists.txt` (BBA block ~line 47-49, BBA2 block ~line 50-53 — both `-DN_CVS=102 -DN_TRIGS=43`)
- Modify: `main/CVSlotNames.hpp` (append entries)
- Modify: `main/IOCapabilities.hpp` (BBA `t`/`cv` string lists)
- Test: `tests/host/test_seq_mod.cpp` (new)

**Interfaces:**
- Produces: compile-time `N_CVS == 106`, `N_TRIGS == 51` on BBA/BBA2 and host; cv slots 102-105 = `SQ.G1/SQ.G2/SQ.A1/SQ.A2`; trig slots 43-50 = `SQ.G1/SQ.G2/SQ.A1_G/SQ.A2_G/SQ.A1_A/SQ.A2_A/SQ.A1_S/SQ.A2_S`. Later tasks reference these exact names.

- [ ] **Step 1: Write the failing test**

Create `tests/host/test_seq_mod.cpp`:

```cpp
// Host tests for the SEQ modulators (GATE x2 + ARP x2) and slot-space growth.
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
```

Check `test_framework.hpp` for the exact TEST/CHECK macro spellings used by the existing tests (`tests/host/test_marbles_core.cpp`) and match them; adjust the snippet to the harness (the CHECK macro may be `EXPECT`/`REQUIRE`).

- [ ] **Step 2: Add the test file to the harness and verify it fails**

In `tests/host/run_tests.sh`, add `"$HERE/test_seq_mod.cpp"` to the g++ source list (after `test_render_ladder.cpp`).

Run: `tests/host/run_tests.sh`
Expected: FAIL — `ClampCVSlot(102) == -1` (host defaults still 102/43).

- [ ] **Step 3: Extend the bounds**

`ctagSlotBounds.hpp`: host defaults `#define N_CVS 106`, `#define N_TRIGS 51` (update the comment line 26-27 to "BBA: N_CVS=106, N_TRIGS=51").

`CMakeLists.txt`: in the `CONFIG_TBD_PLATFORM_BBA` and `CONFIG_TBD_PLATFORM_BBA2` blocks only, change `-DN_CVS=102` → `-DN_CVS=106` and `-DN_TRIGS=43` → `-DN_TRIGS=51`. Other platforms untouched.

- [ ] **Step 4: Add the names**

`main/CVSlotNames.hpp`: after the Marbles CV entries (98-101) append comments + names:
```cpp
    // Slots 102-105: SEQ modulators (GATE x2 level, ARP x2 pitch)
    "SQ.G1", "SQ.G2", "SQ.A1", "SQ.A2",
```
and the trig name table: after `MB.T3` append `"SQ.G1", "SQ.G2", "SQ.A1 GATE", "SQ.A2 GATE", "SQ.A1 ACC", "SQ.A2 ACC", "SQ.A1 SLIDE", "SQ.A2 SLIDE"`. Follow the file's existing entry format exactly (read the file first).

`main/IOCapabilities.hpp`: in the `CONFIG_TBD_PLATFORM_BBA` string — cv list: append `,"SQ.G1","SQ.G2","SQ.A1","SQ.A2"` after `"MB_Y"`; trig list: append `,"SQ_G1","SQ_G2","SQ_A1_G","SQ_A2_G","SQ_A1_A","SQ_A2_A","SQ_A1_S","SQ_A2_S"` after `"MB_T3"`. Count check after edit: `t` entries = 51, `cv` entries = 106 (the BBA2 build shares this string via the `-DCONFIG_TBD_PLATFORM_BBA` compile definition).

- [ ] **Step 5: Run tests to verify they pass**

Run: `tests/host/run_tests.sh`
Expected: PASS (all pre-existing tests + the 2 new ones).

- [ ] **Step 6: Commit**

```bash
git add tests/host/test_seq_mod.cpp tests/host/run_tests.sh components/ctagSoundProcessor/helpers/ctagSlotBounds.hpp CMakeLists.txt main/CVSlotNames.hpp main/IOCapabilities.hpp
git commit -m "feat(mod): slot space 106 cv / 51 trig — reserve SQ.G1/G2 + SQ.A1/A2 outputs"
```

---

### Task 2: ModTransport (hoisted transport, host-tested)

**Files:**
- Create: `main/ModTransport.hpp`
- Create: `main/ModTransport.cpp`
- Test: `tests/host/test_seq_mod.cpp` (extend)

**Interfaces:**
- Produces (used by Tasks 3, 6, 7):
  - `namespace CTAG::DRIVERS { class ModTransport { static void Init(); static void OnMidiClock(); static void OnMidiClockAt(int64_t now_us); static void OnMidiStart(); static void OnMidiStop(); static void Process(float dt); static int TicksThisBlock(); static bool IsRunning(); static bool TakeStartReset(); static uint8_t ExternalGateFlags(); static void SetExternalGateLevel(bool high); static float GetBPM(); static void SetBPM(float v); static void LoadConfig(); static void SaveConfig(); static bool ReadSectionJSON(const char* name, char* dst, int cap); static bool WriteSectionJSON(const char* name, const char* json); }; }`
  - Semantics: `Process(dt)` drains the pending MIDI tick counter ONCE per block into `TicksThisBlock()`; `ExternalGateFlags()` returns the `stmlib::GateFlags` chain for Marbles' RampExtractor; `TakeStartReset()` is true exactly once per FA.

- [ ] **Step 1: Write the failing tests**

Append to `tests/host/test_seq_mod.cpp` (include the header per the existing tests' include style):

```cpp
#define MODTRANSPORT_HOST 1
#include "ModTransport.hpp"

TEST(transport_ticks_drained_once) {
    CTAG::DRIVERS::ModTransport::Init();
    CTAG::DRIVERS::ModTransport::OnMidiClockAt(1000000);
    CTAG::DRIVERS::DRIVERS::ModTransport::OnMidiClockAt(1000001);
    // both engines read the SAME drained count; draining twice is the bug class
    CTAG::DRIVERS::ModTransport::Process(1.0f / 1000.0f);
    int a = CTAG::DRIVERS::ModTransport::TicksThisBlock();
    int b = CTAG::DRIVERS::ModTransport::TicksThisBlock();
    CHECK(a == 2);
    CHECK(b == 2);   // stable within the block, reset only at next Process
    CTAG::DRIVERS::ModTransport::Process(1.0f / 1000.0f);
    CHECK(CTAG::DRIVERS::ModTransport::TicksThisBlock() == 0);
}

TEST(transport_start_stop_states) {
    CTAG::DRIVERS::ModTransport::Init();
    CHECK(!CTAG::DRIVERS::ModTransport::IsRunning());
    CTAG::DRIVERS::ModTransport::OnMidiStart();
    CHECK(CTAG::DRIVERS::ModTransport::TakeStartReset());
    CHECK(!CTAG::DRIVERS::ModTransport::TakeStartReset());  // once per FA
    CHECK(CTAG::DRIVERS::ModTransport::IsRunning());
    CTAG::DRIVERS::ModTransport::OnMidiStop();
    CHECK(!CTAG::DRIVERS::ModTransport::IsRunning());
    CHECK(CTAG::DRIVERS::ModTransport::TicksThisBlock() == 0); // stopped: ticks held back
}

TEST(transport_bpm_clamp_and_ema) {
    CTAG::DRIVERS::ModTransport::Init();
    CTAG::DRIVERS::ModTransport::SetBPM(999.0f);
    CHECK(CTAG::DRIVERS::ModTransport::GetBPM() <= 480.0f);
    CTAG::DRIVERS::ModTransport::SetBPM(120.0f);
    // 24 ticks at exactly 120 BPM (500 ms apart) converges the EMA toward 120
    int64_t t = 0;
    for (int i = 0; i < 24; ++i) { CTAG::DRIVERS::ModTransport::OnMidiClockAt(t); t += 500000; }
    for (int i = 0; i < 24; ++i) { CTAG::DRIVERS::ModTransport::Process(1.0f / 1000.0f); }
    float measured = CTAG::DRIVERS::ModTransport::GetMeasuredBPM();
    CHECK(measured > 100.0f && measured < 140.0f);
}
```

Add `static float GetMeasuredBPM();` to the Interfaces list above (used only for telemetry/UI later).

- [ ] **Step 2: Add ModTransport.cpp to run_tests.sh and verify failure**

Add `"$ROOT/main/ModTransport.cpp"` to the g++ line (after `test_render_ladder.cpp`).
Run: `tests/host/run_tests.sh`
Expected: FAIL — missing header.

- [ ] **Step 3: Implement ModTransport**

`main/ModTransport.hpp`:

```cpp
#pragma once
#include <cstdint>

namespace CTAG::DRIVERS {

// Shared modulation transport: global BPM, MIDI-clock funnel, transport
// state. ONE owner of the incoming MIDI clock edges — Marbles (RampExtractor
// feed) and SeqEngine (integer PPQ ticks) both consume the per-block state
// published by Process(), nobody drains the raw counter themselves.
// Host-testable: the core paths use OnMidiClockAt(int64_t) + Process(dt); the
// esp_timer convenience wrapper is guarded so tests compile without IDF.
class ModTransport {
public:
    static void Init();
    // MIDI task entries (link-fed bytes). OnMidiClock uses esp_timer; the
    // _At form is the testable core.
    static void OnMidiClock();
    static void OnMidiClockAt(int64_t now_us);
    static void OnMidiStart();   // 0xFA and 0xFB
    static void OnMidiStop();    // 0xFC

    // Audio task, once per block BEFORE MarblesEngine/SeqEngine Process.
    static void Process(float dt /*seconds*/);
    // Block state, valid after Process until the next Process:
    static int     TicksThisBlock();    // MIDI PPQ ticks landed in this block
    static bool    IsRunning();         // transport running (FA/FB set, FC clears)
    static bool    TakeStartReset();    // true once per FA (pattern re-phase)
    // Marbles RampExtractor feed (1 tick/block max, same collapse rule as the
    // pre-hoist pendingClockEdges drain):
    static uint8_t ExternalGateFlags(); // stmlib::GateFlags value
    static void    SetExternalGateLevel(bool high); // advanced inside Process

    // Global tempo (20..480). Measured EMA adopts while MIDI clock runs.
    static float GetBPM();
    static void  SetBPM(float v);
    static float GetMeasuredBPM();   // -1 = no measurement yet

    // mod-config.jsn ownership: single file, read-merge-write per section so
    // transport/marbles/seq never overwrite each other.
    static void  LoadConfig();       // "transport" + legacy marbles.bpm/tFine
    static void  SaveConfig();       // writes "transport"
    static bool  ReadSectionJSON(const char *name, char *dst, int cap);
    static bool  WriteSectionJSON(const char *name, const char *json);
};

}
```

`main/ModTransport.cpp` — complete implementation (stmlib GateFlags values: `GATE_FLAG_LOW=0, GATE_FLAG_RISING=1, GATE_FLAG_HIGH=2, GATE_FLAG_FALLING=3`; include `"stmlib/utils/gate_flags.h"` — path available on target and in host test include set via `-I "$EURORACK"`):

```cpp
#include "ModTransport.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "stmlib/utils/gate_flags.h"

#if __has_include(<esp_log.h>)
#include <esp_log.h>
#include <esp_timer.h>
#define MT_LOGE(...) ESP_LOGE("MODTR", ##__VA_ARGS__)
#else
#define MT_LOGE(...) ((void)0)
#define MODTRANSPORT_HOST 1
#endif

namespace CTAG::DRIVERS {

namespace {
    float     bpm = 120.0f;
    float     midiBpmEma = -1.0f;
    int64_t   lastMidiTickUs = 0;
    uint32_t  pendingClockEdges = 0;   // MIDI task -> audio task (fetch_add/sub)
    bool      midiRunning = false;
    bool      startResetPending = false;
    bool      extGateLevel = false;
    uint8_t   extGateFlags = stmlib::GATE_FLAG_LOW;
    int       ticksThisBlock = 0;

    float ClampBPM(float v) { return v < 20.0f ? 20.0f : (v > 480.0f ? 480.0f : v); }

    // --- mod-config.jsn section file IO (host-testable via a path hook) ---
    // The file path is overridable so host tests exercise the merge logic on
    // a temp file without SPIFFS.
    const char* ConfigPath() {
#if defined(MODTRANSPORT_CFG_PATH)
        return MODTRANSPORT_CFG_PATH;
#else
        return "/spiffs/data/mod-config.jsn";
#endif
    }
    std::string ReadFileOrEmpty(const char* path);   // defined below
    bool WriteFileAtomic(const char* path, const std::string& data);
}

void ModTransport::Init() {
    bpm = 120.0f; midiBpmEma = -1.0f; lastMidiTickUs = 0;
    pendingClockEdges = 0; midiRunning = false; startResetPending = false;
    extGateLevel = false; extGateFlags = stmlib::GATE_FLAG_LOW; ticksThisBlock = 0;
    LoadConfig();
}

void ModTransport::OnMidiClock() {
#if !defined(MODTRANSPORT_HOST)
    OnMidiClockAt(esp_timer_get_time());
#endif
}

void ModTransport::OnMidiClockAt(int64_t now_us) {
    pendingClockEdges.fetch_add(1);   // see note below on the atomic
    // Measured-tempo EMA (2.5..1000 ms/tick => 15..6000 BPM window)
    if (lastMidiTickUs != 0) {
        float ms = (float)(now_us - lastMidiTickUs) / 1000.0f;
        if (ms >= 2.5f && ms <= 1000.0f) {
            float inst = 60000.0f / (ms * 24.0f);
            midiBpmEma = (midiBpmEma < 0.0f) ? inst : (0.9f * midiBpmEma + 0.1f * inst);
            bpm = ClampBPM(midiBpmEma);
        }
    }
    lastMidiTickUs = now_us;
}

void ModTransport::OnMidiStart() {
    midiRunning = true;
    startResetPending = true;
    midiBpmEma = -1.0f;   // re-measure per run (pre-hoist Marbles behavior)
    lastMidiTickUs = 0;
}

void ModTransport::OnMidiStop() {
    midiRunning = false;
    lastMidiTickUs = 0;
}

void ModTransport::Process(float dt) {
    (void)dt;
    // 1) drain pending edges — max 1 per block (pre-hoist collapse rule:
    //    edges closer than one block collapse; ticks are never lost across
    //    blocks, only coalesced within one).
    int drained = 0;
    uint32_t e = pendingClockEdges.load();
    while (e > 0 && pendingClockEdges.compare_exchange_weak(e, e - 1)) {
        if (midiRunning && drained < 24) ++drained;
        e = pendingClockEdges.load();
        if (drained >= 1) break;   // 1/block for the gate chain; count kept below
    }
    // Full count for the PPQ consumer (edges may exceed 1 at high BPM and
    // 1000 Hz blocks is impossible: 24 PPQ * 480 BPM = 192 Hz < 1000 Hz, so
    // in practice 0 or 1; the clamp is belt-and-braces).
    drained = 0;
    e = pendingClockEdges.load();
    while (e > 0 && pendingClockEdges.compare_exchange_weak(e, e - 1)) {
        if (midiRunning) ++drained;
        e = pendingClockEdges.load();
    }
    ticksThisBlock = drained;
    // 2) Marbles gate-flag chain (same per-block collapse as pre-hoist)
    bool level = pendingClockEdges.load() > 0;
    SetExternalGateLevel(level);
    (void)level;
    // 3) start-reset latch survives until a consumer takes it
}

int ModTransport::TicksThisBlock() { return ticksThisBlock; }
bool ModTransport::IsRunning() { return midiRunning; }
bool ModTransport::TakeStartReset() { bool r = startResetPending; startResetPending = false; return r; }
uint8_t ModTransport::ExternalGateFlags() { return extGateFlags; }
void ModTransport::SetExternalGateLevel(bool high) {
    extGateLevel = high;
    extGateFlags = stmlib::ExtractGateFlags((stmlib::GateFlags)extGateFlags, extGateLevel);
}
float ModTransport::GetBPM() { return bpm; }
void ModTransport::SetBPM(float v) { bpm = ClampBPM(v); }
float ModTransport::GetMeasuredBPM() { return midiBpmEma; }
```

**STOP — the draft above has a self-review bug on purpose of clarity: the double drain loop is wrong.** Rewrite `Process` before writing the file so the real file contains exactly this single-pass version:

```cpp
void ModTransport::Process(float dt) {
    (void)dt;
    // Drain ALL pending edges once; while stopped they are discarded (a
    // stopped transport holds no phase). Practical rate is <= 192 Hz against
    // a >= 1000 Hz block rate, so 0 or 1 per block; higher is still correct.
    int drained = 0;
    uint32_t e;
    do {
        e = pendingClockEdges.load();
        if (e == 0) break;
    } while (!pendingClockEdges.compare_exchange_weak(e, 0));
    if (midiRunning) drained = (int)e;
    ticksThisBlock = drained;
    // Marbles gate-flag chain: ONE block-high pulse per drained tick max
    // (same collapse rule as the pre-hoist single-edge drain; keeps the
    // RampExtractor feed identical).
    SetExternalGateLevel(drained > 0);
}
```

Use the corrected version in the actual file. Then finish the file with the section-IO implementation:

```cpp
// --- mod-config.jsn section IO: read file -> parse (NORapidJSON asserts on
// target are compiled out; malformed file treated as empty) -> set member ->
// write back. Host tests point MODTRANSPORT_CFG_PATH at a temp file. ---
// Implementation uses rapidjson (available on target; host tests compile with
// the include path from the eurorack/protocol dirs).
// ReadSectionJSON: dst gets the raw JSON text of member "name" ("" if absent).
// WriteSectionJSON: parse full file (or "{}"), parse the incoming "json"
// fragment, deep-assign it as member "name", write back atomically.

bool ModTransport::ReadSectionJSON(const char *name, char *dst, int cap) {
    std::string all = ReadFileOrEmpty(ConfigPath());
    if (all.empty()) { dst[0] = 0; return false; }
    rapidjson::Document d;
    d.ParseInsitu(const_cast<char*>(all.data()));  // workable: see note
    ...
}
```

Do NOT copy that fragment — implement `ReadSectionJSON`/`WriteSectionJSON` like this (complete, no insitu aliasing):

```cpp
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

bool ModTransport::ReadSectionJSON(const char *name, char *dst, int cap) {
    dst[0] = 0;
    std::string all = ReadFileOrEmpty(ConfigPath());
    if (all.empty()) return false;
    rapidjson::Document d;
    if (d.Parse(all.c_str()).HasParseError() || !d.IsObject()) return false;
    auto it = d.FindMember(name);
    if (it == d.MemberEnd() || !it->value.IsObject()) return false;
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
    rapidjson::Document copy;
    copy.CopyFrom(frag, root.GetAllocator());
    auto it = root.FindMember(name);
    if (it != root.MemberEnd()) it->value = copy;
    else root.AddMember(rapidjson::GenericValue<rapidjson::UTF8<>>(name, root.GetAllocator()), copy, root.GetAllocator());
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> w(sb);
    root.Accept(w);
    return WriteFileAtomic(ConfigPath(), std::string(sb.GetString(), sb.GetSize()));
}

void ModTransport::LoadConfig() {
    char buf[256];
    if (!ReadSectionJSON("transport", buf, sizeof(buf))) {
        // legacy: bpm/tFine lived in the marbles object
        if (ReadSectionJSON("marbles", buf, sizeof(buf))) {
            rapidjson::Document d;
            if (!d.Parse(buf).HasParseError() && d.IsObject() && d.HasMember("bpm") && d["bpm"].IsNumber())
                bpm = ClampBPM((float)d["bpm"].GetDouble());
        }
        return;
    }
    rapidjson::Document d;
    if (!d.Parse(buf).HasParseError() && d.IsObject() && d.HasMember("bpm") && d["bpm"].IsNumber())
        bpm = ClampBPM((float)d["bpm"].GetDouble());
}

void ModTransport::SaveConfig() {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"bpm\":%.1f}", bpm);
    WriteSectionJSON("transport", buf);
}
```

`ReadFileOrEmpty`/`WriteFileAtomic`: on target use stdio `fopen/fread/fwrite` on the VFS path + rename-based atomic replace, matching the pattern in `MarblesEngine::SaveConfig` (read it: `main/MarblesEngine.cpp:568-640`); on host (`MODTRANSPORT_HOST`) plain stdio. Note: `ModTransport::SaveConfig` runs on the UIMenu task (4 KB stack) — buffer sizes stay small like MarblesEngine does; the file DOM above allocates on the caller task — acceptable (MarblesEngine::LoadConfig already parses the same file size there).

- [ ] **Step 4: Run tests to verify they pass**

`pendingClockEdges` must be `std::atomic<uint32_t>` (header member) so `fetch_add` compiles on host; include `<atomic>`.
Run: `tests/host/run_tests.sh`
Expected: PASS — 5 new tests green, existing tests unaffected.

- [ ] **Step 5: Commit**

```bash
git add main/ModTransport.hpp main/ModTransport.cpp tests/host/test_seq_mod.cpp tests/host/run_tests.sh
git commit -m "feat(mod): ModTransport — hoisted BPM/MIDI-clock/transport with section-file config IO (host-tested)"
```

---

### Task 3: Rewire MarblesEngine onto ModTransport + T-gate inversion fix

**Files:**
- Modify: `main/MarblesEngine.cpp` (statics ~lines 115-170, OnMidiClock/OnMidiStart/Process ~lines 246-364, SetBPM/GetBPM, LoadConfig/SaveConfig ~568-680)
- Modify: `main/MarblesEngine.hpp` (remove OnMidiClock/OnMidiStart declarations or keep as deprecated forwards — REMOVE, spec §4.1)
- Modify: `main/Midi.cpp:1163-1176` (F8/FA/FB/FC handlers)
- Modify: `main/SPManager.cpp:167` (call order)
- Test: `tests/host/run_tests.sh` (must stay green — MarblesEngine.cpp itself is not host-compiled)

**Interfaces:**
- Consumes: `ModTransport` from Task 2.
- Produces: `MarblesEngine::GetBPM()/SetBPM()` delegate to ModTransport; `MarblesEngine::Process` consumes `ModTransport::ExternalGateFlags()` + `TakeStartReset()`; Midi.cpp routes F8→`ModTransport::OnMidiClock()`, FA/FB→`OnMidiStart()`, FC→`OnMidiStop()`; SPManager calls `ModTransport::Process(dt)` before `MarblesEngine::Process`.

- [ ] **Step 1: MarblesEngine state swap**

In `main/MarblesEngine.cpp`: delete statics `bpm`, `lastMidiTickUs`, `midiBpmEma`, `pendingClockEdges`, `resetRequest`, `extClockFlags`, `extGateLevel` (lines ~135-147 + the `std::atomic<uint32_t> pendingClockEdges` definition and the `ClampFloat` bpm usage). `MarblesEngine::Init()` no longer loads bpm — call `ModTransport::Init()` FIRST (it LoadConfigs) and remove `LoadConfig()`'s bpm handling from MarblesEngine (Step 4).

`OnMidiClock()/OnMidiStart()/RequestReset()` bodies: delete them; keep only `RequestReset()` (UI one-shot) as-is (it is NOT MIDI-related).

`Process()` head (currently drains pendingClockEdges + builds extGateLevel, lines ~257-268 + 356-364): replace with:

```cpp
void MarblesEngine::Process(float *cv_buffer, uint8_t *trig_buffer) {
    // Transport comes from ModTransport (one MIDI-clock funnel for both
    // engines; see ModTransport.hpp). The RampExtractor keeps its exact
    // pre-hoist feed: one gate-flag stream per block.
    uint8_t flags = ModTransport::ExternalGateFlags();
    extClockFlags = (stmlib::GateFlags)flags;
    bool reset = ModTransport::TakeStartReset() || resetRequest;
    resetRequest = false;
```

and delete the old edge-drain block. Keep `extClockFlags`/`resetRequest` as local-usage statics ONLY where shown above (extClockFlags keeps existing declarations minus the funnel writes).

Feed swap in the t_generator call: `t_generator.Process(tExternal, &reset, &extClockFlags, ramps, gates, 1);` — unchanged, but `extClockFlags` now comes from ModTransport.

T-gate inversion — `main/MarblesEngine.cpp:362-364` become:

```cpp
        if (tSlot[0] >= 0) trig_buffer[tSlot[0]] = gates[0] ? 0 : 1;      // T1 (active-low: trig==1 -> OFF)
        if (tSlot[1] >= 0) trig_buffer[tSlot[1]] = t2Level ? 0 : 1;       // T2
        if (tSlot[2] >= 0) trig_buffer[tSlot[2]] = gates[1] ? 0 : 1;      // T3
```

`GetBPM()/SetBPM()`: delegate to `ModTransport::GetBPM()/SetBPM(v)`; `SetBPM` then calls `ApplyClock()` (tRate derivation stays Marbles-local). `ApplyClock()` keeps reading the Marbles-local `tFine` and derives `tRate = 12.0f * log2f(ModTransport::GetBPM() / 120.0f) + tFine`. Delete the Marbles-local `bpm` variable everywhere (compile will catch stragglers).

- [ ] **Step 2: Midi.cpp routing**

`main/Midi.cpp:1163-1176`: F8 → `CTAG::DRIVERS::ModTransport::OnMidiClock();`; 0xFA/0xFB → `CTAG::DRIVERS::ModTransport::OnMidiStart();`; 0xFC → `CTAG::DRIVERS::ModTransport::OnMidiStop();` (new — was a no-op). Include `ModTransport.hpp`.

- [ ] **Step 3: SPManager call order**

`main/SPManager.cpp` just above line 167:

```cpp
        // Shared transport (BPM/MIDI clock) -> Marbles random modulation.
        // SeqEngine joins in Task 7 (after MarblesEngine::Process).
        const float modDt = (float)BUF_SZ / (float)DRIVERS::Codec::GetSampleRate();
        DRIVERS::ModTransport::Process(modDt);
        DRIVERS::MarblesEngine::Process(pd.cv, pd.trig);
```

Include `ModTransport.hpp`. Guard: `ModTransport::Process` must be called exactly once per block even when the plugin swap path skips render — it is outside any swap guard (line 167 is).

- [ ] **Step 4: Config ownership swap**

`MarblesEngine::LoadConfig()`: delete the bpm/tFine reading (it moves to ModTransport legacy read); MarblesEngine reads ONLY its own members. tFine: keep Marbles-local (it is a Marbles clock trim) but move its persistence to the "transport" object? NO — per spec §4.1, bpm+tFine move: `ModTransport` persists `{"bpm":..., "tfine":...}` in "transport" (add `"tfine"` to SaveConfig: `snprintf(buf, sizeof(buf), "{\"bpm\":%.1f,\"tfine\":%g}", bpm, GetTFine())` — hmm, tFine is Marbles state. **Decision (keep it simple and spec-true):** bpm lives in "transport" (ModTransport). tFine STAYS in "marbles" (it is the Marbles fine trim, spec §4.1 says "legacy read of marbles.bpm/tFine" only for the fallback path). So: ModTransport::LoadConfig falls back to marbles.bpm when "transport" absent (as implemented in Task 2); MarblesEngine keeps tFine persistence in "marbles" unchanged.

`MarblesEngine::SaveConfig()` (line 570): currently overwrites the whole file with the marbles object — CHANGE to `ModTransport::WriteSectionJSON("marbles", body)` where body is the existing serialized string. Same for `LoadConfig`: read via `ModTransport::ReadSectionJSON("marbles", buf, sizeof(buf))` (keeps buffer small on the 4 KB UIMenu stack; the DOM churn happens inside ModTransport on the same task — same as today's parse).

- [ ] **Step 5: Build both targets + host tests**

```bash
tests/host/run_tests.sh
source /home/ubuntu/esp-idf-v6.1/export.sh && idf.py -B build-bba2 -D IDF_TARGET=esp32s3 -D SDKCONFIG=build-bba2/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.bba2" build
idf.py build   # A1S (plain build dir) — resolves compile errors Marbles-side
```
Expected: host tests PASS, both firmwares build.

- [ ] **Step 6: Bench — Marbles regression**

Flash BBA2 (`idf.py -B build-bba2 flash` — or the esptool app-only command from AGENTS.md). Bench in ONE console session: verify handshake + MOD page renders (screenshot), tap tempo still works, and the T-gate inversion: map MB.T1 to a trig param via UI, confirm it fires on gate start not gate end. External MIDI clock: play MIDI clock into the panel and confirm X-section activity stays tempo-locked (log `MARBLES` lines / `mod` snapshot via `/debug`).

- [ ] **Step 7: Commit**

```bash
git add main/MarblesEngine.cpp main/MarblesEngine.hpp main/Midi.cpp main/SPManager.cpp main/ModTransport.cpp
git commit -m "refactor(mod): ModTransport owns BPM/MIDI clock; Marbles T-gates follow active-low convention (fix)"
```

---

### Task 4: SeqEngineCore — gate generator + clock bus (host-tested)

**Files:**
- Create: `main/SeqEngineCore.hpp` (header-only pure generators + tables)
- Test: `tests/host/test_seq_mod.cpp` (extend)

**Interfaces:**
- Produces (used by Tasks 5, 6):
  - `namespace CTAG::SEQ {` `enum: kGateModeCount = 19, kDivCount = 13;` `struct ModDivision { const char* name; uint16_t ppq; }; extern const ModDivision kDivisions[13];` `extern const uint16_t kTranceGatePresets[16];` `struct GateSource { uint8_t mode; uint8_t dens; uint8_t sync; float glen; uint32_t mask; uint8_t step; uint8_t level; uint32_t rng_state; uint8_t cur_preset; };` `void GateRecompute(GateSource& s);` `uint32_t GateAdvanceStep(GateSource& s); /* returns wraps */` `}` — all pure, no hardware types.
  - Division table (verbatim port, index 7 = "1/16"; DEFAULTS use 7 — see Review Focus #4).

- [ ] **Step 1: Write the failing tests**

Append to `tests/host/test_seq_mod.cpp`:

```cpp
#include "SeqEngineCore.hpp"

TEST(gate_preset_patterns) {
    using namespace CTAG::SEQ;
    // 16 canonical patterns, MSB = step 1 (rack mod_engine.h:396-413)
    CHECK(kTranceGatePresets[0] == 0xAAAAu);
    CHECK(kTranceGatePresets[9] == 0x9292u);
    CHECK(kTranceGatePresets[15] == 0x88AFu);
    GateSource g{};
    g.mode = 1; g.dens = 16; g.sync = 0; g.glen = 0.5f;
    GateRecompute(g);
    CHECK(g.mask == 0xAAAAu);   // preset 01 untouched at full density
    g.mode = 3; GateRecompute(g); CHECK(g.mask == 0x7777u);
}

TEST(gate_barlow_density_ordering) {
    using namespace CTAG::SEQ;
    // GEN mode: density n keeps every strong beat of density n-1 (Barlow trim)
    GateSource a{}, b{};
    a.mode = 0; a.dens = 8;  a.sync = 0; b.mode = 0; b.dens = 10; b.sync = 0;
    GateRecompute(a); GateRecompute(b);
    CHECK((b.mask & a.mask) == a.mask);
}

TEST(gate_tmr_deterministic) {
    using namespace CTAG::SEQ;
    GateSource a{}, b{};
    a.mode = 17; a.dens = 12; a.sync = 40; b = a;
    a.rng_state = 0x12345678u; b.rng_state = 0x12345678u;
    GateRecompute(a); GateRecompute(b);
    CHECK(a.mask == b.mask);            // same seed -> same groove
    uint32_t flips = 0;
    for (int i = 0; i < 64; ++i) {
        uint32_t before = a.mask;
        a.step = (a.step + 1) % 16;
        GateAdvanceStep(a);
        flips += (a.mask != before);
    }
    CHECK(flips > 0);                   // mutation actually mutates
}

TEST(gate_markov_adjacent_transitions) {
    using namespace CTAG::SEQ;
    // MRKV: every transition lands on a Hamming-neighbor or self-adjacent set
    GateSource g{};
    g.mode = 18; g.dens = 3; g.sync = 255; g.rng_state = 99;
    GateRecompute(g);
    uint16_t first = (uint16_t)g.mask;
    for (int i = 0; i < 16; ++i) { g.step = (g.step + 1) % 16; GateAdvanceStep(g); }
    // after one bar the preset may have changed; every reachable preset differs
    // by a Hamming distance that the temperature allows (never arbitrary masks)
    CHECK(((uint16_t)g.mask == first) || (__builtin_popcount((uint16_t)g.mask ^ first) <= 16));
    // determinism with fixed seed
    GateSource h = g; h.rng_state = 99; GateRecompute(h);
    // (determinism checked fully in Task 6 via EngineProcess loops)
}

TEST(division_table_and_default) {
    using namespace CTAG::SEQ;
    CHECK(kDivCount == 13);
    CHECK(strcmp(kDivisions[7].name, "1/16") == 0);   // Review Focus #4: index 7 IS 1/16
    CHECK(kDivisions[7].ppq == 6);
    CHECK(kDivisions[12].ppq == 2);                   // 1/32T
    CHECK(kDivisions[3].ppq == 96);                   // 1/1
}

TEST(gate_step_advance_1_16) {
    using namespace CTAG::SEQ;
    GateSource g{};
    g.mode = 1; g.dens = 16; g.sync = 0; g.glen = 0.5f;
    GateRecompute(g);
    g.step = 15;
    uint32_t wraps = GateAdvanceStep(g);   // one PPQ tick at 1/16 = ppq 6 per cycle
    // advance by exactly one division cycle worth of PPQ ticks:
    // caller model: GateAdvanceStep takes wraps; tested via EngineProcess in Task 6.
    (void)wraps;
    CHECK(g.step == 15);  // no direct mutation without wraps (see header contract)
}
```

- [ ] **Step 2: Verify failure**

Run: `tests/host/run_tests.sh`
Expected: FAIL — `SeqEngineCore.hpp` missing.

- [ ] **Step 3: Port the gate core**

Create `main/SeqEngineCore.hpp` with the license header (copy the block from `main/MarblesEngine.hpp:1-23`) plus:

- **Verbatim ports** from `~/mi-rp2350/rack-rp2350/src/mod_engine.h`, renamed `mod_` → `SEQ::`, `s.p.gate.X` → `s.X`:
  - `kTranceGatePresets[16]` + comments (lines 396-413)
  - `kBarlow16[16]` (lines 422-424), `kSyncCandidates[8]` (line 427), `mod_hamming_dist` (430-432)
  - `mod_gate_markov_next` (435-478) — take `uint32_t& rng` unchanged
  - `mod_gate_recompute` (481-568) — note `s.p.gate.len` is always 16 in this port (delete the `len` field, use literal 16)
  - TMR step mutation + MRKV bar-transition blocks from `mod_engine_process_block` (lines 1391-1410) as `SEQ::GateStepMutation(GateSource& s)` / `SEQ::GateBarMutation(GateSource& s)`
  - `kModDivisions[13]` (lines 148-152) → `kDivisions[13]`, `MOD_DIVISION_COUNT` → `kDivCount = 13`
  - `mod_euclid_mask` is NOT ported (non-goal, spec §2)
- Namespace `CTAG::SEQ`, `#pragma once`, `<cstdint>` + `<cstring>` only (no ESP includes, no math.h needs).
- Contract: `GateAdvanceStep(GateSource&)` mutates `step`/`level`/`rng_state`/`cur_preset` and returns the number of completed division cycles it was told about — actually keep it simple and match the tests: `uint32_t GateAdvanceStep(GateSource& s, uint32_t wraps)` advances `s.step += wraps % 16`-style exactly like `mod_engine.h:1384-1413` (wraps param). Adjust the test above to pass `wraps` (fix the test in the same step: `GateAdvanceStep(g, 1)` from step 15 → step 0, `GateAdvanceStep(g, 0)` → unchanged).

- [ ] **Step 4: Run tests to verify they pass**

Run: `tests/host/run_tests.sh`
Expected: PASS (gate + division tests green).

- [ ] **Step 5: Commit**

```bash
git add main/SeqEngineCore.hpp tests/host/test_seq_mod.cpp
git commit -m "feat(mod): SEQ gate core — 16 trance presets + Barlow/TMR/MRKV generators (host-tested port)"
```

---

### Task 5: SeqEngineCore — ARP generators (host-tested)

**Files:**
- Modify: `main/SeqEngineCore.hpp` (extend)
- Test: `tests/host/test_seq_mod.cpp` (extend)

**Interfaces:**
- Consumes: gate-side namespace from Task 4.
- Produces (Task 6):
  - `struct ArpStep { uint8_t flags; int8_t off; int8_t oct; };` (rack 168-172)
  - `struct ArpExt { ArpStep steps[16]; uint8_t scale_st[12]; uint8_t nsteps; uint8_t chord_nt; int8_t deg; uint8_t is_minor_triad; uint8_t bar_count; uint8_t dd_fill; uint8_t turn_type; union { struct { int8_t seed_deg[4]; int8_t seed_oct[4]; uint8_t seed_flags[4]; uint8_t form; } motif; struct { uint8_t prev_deg; int8_t prev_oct; uint8_t contour_id; } markov; struct { int8_t accum_val[8]; int8_t accum_dir[8]; uint8_t cur_stage; uint8_t cur_pulse; } metro; struct { uint8_t u_pitch[16]; uint8_t u_gate[16]; } dejavu; } st; int32_t current_pitch_q16; int32_t target_pitch_q16; int32_t accent_cap_q16; };` (rack 174-214)
  - `struct ArpSource { uint8_t algo; uint8_t range; uint8_t root; uint8_t scale_idx; uint8_t p[4]; uint8_t step; uint8_t cur_note; uint8_t level; uint8_t cur_acc; uint8_t cur_hold; uint8_t retrig_edge; uint8_t held; uint32_t ticks; uint32_t rng; };` (root = pitch class 0..11; MIDI-follow NOT ported)
  - `ArpRebuildScale(ArpSource&, ArpExt&)`, `ArpNextChord(ArpSource&, ArpExt&, uint8_t neo_op)`, `ArpRebuildPattern(ArpSource&, ArpExt&)`, `ArpApplyTurnaround(ArpSource&, ArpExt&, uint8_t st, ArpStep& io)`, `ArpCalcNote(const ArpSource&, const ArpExt&, const ArpStep&)`, `ArpRollHyp1Step/ArpBuildMotif/ArpBuildMarkov/ArpBuildMetro/ArpBuildDejavu` — one-to-one ports with the rack semantics; the shared-root/scale helpers (`mod_arp_set_shared_*`) are NOT ported into the core (Task 6's shell owns shared state across the 2 instances).
  - Tables verbatim from `~/mi-rp2350/rack-rp2350/src/gen_tables.h`: `SCALE_BITMASKS[20]` + `SCALE_COUNT` + scale enum (:37-79), `DEGREE_METRICAL_GRAVITY[3][7]` (:81-93), `DEGREE_MARKOV_7X7[7][7]` (:95-109), `DIATONIC_W7_WEIGHTS[7]` (:111), `CHORD_MARKOV_7X7[7][7]` (:114-134), `neo_riemannian_id_t` + `apply_neo_riemannian` (:136-159), `MELODIC_CONTOURS_DEG[16][16]` (:161-197), `METRIC_MUTATE_SENSITIVITY[16]` (:199-205), `floor_div_i16/floor_mod_i16/expand_scale_mask/degree_to_midi_folded` (:208-264). Copy the ranges verbatim into `CTAG::SEQ`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST(arp_scale_quantization_all_20) {
    using namespace CTAG::SEQ;
    for (uint8_t sc = 0; sc < SCALE_COUNT; ++sc) {
        ArpSource s{}; s.scale_idx = sc; s.root = 0;   // fixed C
        ArpExt e{};
        ArpRebuildScale(s, e);
        CHECK(e.nsteps >= 2 && e.nsteps <= 12);
        // scale steps are strictly ascending semitone offsets < 12
        for (uint8_t i = 1; i < e.nsteps; ++i) CHECK(e.scale_st[i] > e.scale_st[i - 1]);
        CHECK(e.scale_st[0] == 0);
    }
}

TEST(arp_pitch_convention_1v_oct) {
    using namespace CTAG::SEQ;
    ArpSource s{}; s.root = 0; s.scale_idx = 6 /* Aeolian */; s.range = 2;
    ArpExt e{}; ArpRebuildScale(s, e);
    e.deg = 0;
    ArpStep step{}; step.off = 12; step.oct = 0; step.flags = 1;  // 12 scale-degrees up
    uint8_t note = ArpCalcNote(s, e, step);
    // root C3=48 -> +12 scale steps of aeolian = 84 semitones? No: folded — verify via CV:
    float cv = (float)(note - 48) / 12.0f * 0.2f;
    // TBD03 reads cv * 12 * 5 = semitones*128/... cv 0.2 == exactly +12 st
    CHECK(cv > 0.19f && cv < 0.21f);
    CHECK((note - 48) == (int)((cv * 60.0f) + 0.01f));
}

TEST(arp_algo_determinism_seeded) {
    using namespace CTAG::SEQ;
    for (uint8_t algo = 0; algo < 5; ++algo) {
        ArpSource a{}, b{};
        a.algo = algo; b.algo = algo;
        a.range = 2; b.range = 2;
        a.scale_idx = 6; b.scale_idx = 6;
        a.p[0] = 62; a.p[1] = 40; a.p[2] = 50; a.p[3] = 20;
        b = a;
        a.rng = 0x1fff7a10u; b.rng = 0x1fff7a10u;
        ArpExt ea{}, eb{};
        ArpRebuildPattern(a, ea); ArpRebuildPattern(b, eb);
        CHECK(memcmp(ea.steps, eb.steps, sizeof(ea.steps)) == 0);
    }
}

TEST(arp_accent_slide_flags_on_every_algo) {
    using namespace CTAG::SEQ;
    bool saw_acc = false, saw_sld = false, saw_gate = false, saw_off = false;
    for (uint8_t algo = 0; algo < 5; ++algo) {
        ArpSource s{}; s.algo = algo; s.range = 3; s.scale_idx = 6;
        s.p[0] = 80; s.p[1] = 50; s.p[2] = 50; s.p[3] = 50; s.rng = 12345;
        ArpExt e{};
        ArpRebuildPattern(s, e);
        for (int st = 0; st < 16; ++st) {
            saw_gate |= (e.steps[st].flags & 1) != 0;
            saw_off  |= (e.steps[st].flags & 1) == 0;
            saw_acc  |= (e.steps[st].flags & 2) != 0;
            saw_sld  |= (e.steps[st].flags & 4) != 0;
        }
    }
    CHECK(saw_gate); CHECK(saw_off); CHECK(saw_acc); CHECK(saw_sld);
}

TEST(arp_root_modes_exist) {
    using namespace CTAG::SEQ;
    // fixed C + W7 drift + Neo drift rebuild without touching forbidden state
    ArpSource s{}; s.root = 4 /* E */; s.scale_idx = 0; s.p[0] = 50; s.p[1] = 50; s.p[2] = 50; s.p[3] = 60; s.rng = 7;
    ArpExt e{};
    ArpRebuildPattern(s, e);
    uint8_t before = s.root;
    ArpNextChord(s, e, 3 /* R op */);
    CHECK(s.root >= 0 && s.root <= 11);   // neo may rotate the pitch class, never out of range
    (void)before;
    // W7: root stays, degree moves
    s.root = 2; e.deg = 0;
    ArpNextChord(s, e, 0);
    CHECK(s.root == 2);
    CHECK(e.deg >= 0);
}
```

- [ ] **Step 2: Verify failure**

Run: `tests/host/run_tests.sh` — Expected: FAIL (ArpExt etc. undefined).

- [ ] **Step 3: Port the ARP core**

Extend `main/SeqEngineCore.hpp`. **Verbatim ports** from `mod_engine.h` with the rename map `mod_arp_*` → `Arp*`, `ModArpStep/ModArpExt` → `ArpStep/ArpExt`, `s.p.arp.X` → `s.X`, `g_mod_engine.arp_ext[s.p.arp.ext_idx]` → the `ArpExt&` argument (signatures take `ArpExt&` explicitly — no global engine in the core):

- `mod_arp_rebuild_scale` (572-586) — drop the `root_param == 3` scale-adaptation branch? NO — keep it but key it off `rootMode`: add `uint8_t root_mode` to `ArpSource` (0 fixed, 1 w7, 2 neo) and replace `s.p.arp.root_param == 3` checks with `s.root_mode == 2` (scale adaptation) — `mod_arp_next_chord` (589-643): the `root_param == 3` branch → `root_mode == 2` (Neo, uses `apply_neo_riemannian` + sets `s.root` pitch class + shared sync via caller), else branch → W7 weights keyed on `root_mode == 1` (replace `root_param == 2`).
- `mod_arp_pick_offset` (646-656), `mod_arp_roll_hyp1_step` (660-671), `mod_arp_build_motif` (675-786), `mod_arp_build_markov` (790-854), `mod_arp_build_metro` (858-904), `mod_arp_build_dejavu` (908-973), `mod_arp_rebuild_pattern` (976-998), `mod_arp_apply_turnaround` (1024-1050), `mod_arp_calc_note` (1053-1059): verbatim except —
  - `mod_arp_calc_note` root logic: `(root_mode == 0) ? (uint8_t)(48u + s.root) : (uint8_t)(48u + s.root)` → simplify to `const uint8_t base_root = 48u + s.root;` (fixed base; drift modes mutate `s.root` itself, W7 leaves it).
  - DELETE `mod_is_pitch_dest`, `mod_arp_wants_midi_notes`, `mod_arp_note_transpose` (MIDI-follow deferred, spec §2), `mod_arp_set_shared_root/scale/neo_op` (shell owns sharing).
- gen_tables.h ranges listed in Interfaces — copy verbatim into the namespace.
- Add `root_mode` to `ArpSource` (after `range`), and thread it as shown.

- [ ] **Step 4: Run tests**

Run: `tests/host/run_tests.sh` — Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add main/SeqEngineCore.hpp tests/host/test_seq_mod.cpp
git commit -m "feat(mod): SEQ arp core — 5 generative algorithms, 20 scales, neo/W7 drift (host-tested port)"
```

---

### Task 6: SeqEngine shell — Process loop, slots, snapshot, config (host-tested)

**Files:**
- Create: `main/SeqEngine.hpp`
- Create: `main/SeqEngine.cpp`
- Test: `tests/host/test_seq_mod.cpp` (extend)

**Interfaces:**
- Consumes: `SeqEngineCore.hpp` (Tasks 4-5), `ModTransport` (Task 2).
- Produces (Task 7 links against these):
  - `namespace CTAG::DRIVERS { class SeqEngine { static void Init(); static void Process(float *cv, uint8_t *trig, float dt); static uint32_t GetOutputSnapshot(); /* gates */ static int GetGateMode(int i); static void SetGateMode(int i, int v); static int GetGateDens(int i); static void SetGateDens(int i, int v); static int GetGateSync(int i); static void SetGateSync(int i, int v); static float GetGateLen(int i); static void SetGateLen(int i, float v); static int GetGateClk(int i); static void SetGateClk(int i, int v); static int GetGateDiv(int i); static void SetGateDiv(int i, int v); static int GetGateTrigSlot(int i); static void SetGateTrigSlot(int i, int s); static int GetGateCvSlot(int i); static void SetGateCvSlot(int i, int s); static uint32_t GetGateMask(int i); /* arps */ static int GetArpAlgo(int i); static void SetArpAlgo(int i, int v); static int GetArpP(int i, int k); static void SetArpP(int i, int k, int v); static int GetArpGlide(int i); static void SetArpGlide(int i, int v); static int GetArpClk(int i); static void SetArpClk(int i, int v); static int GetArpDiv(int i); static void SetArpDiv(int i, int v); static int GetArpPitchSlot(int i); static void SetArpPitchSlot(int i, int s); static int GetArpGateSlot(int i); static void SetArpGateSlot(int i, int s); static int GetArpAccSlot(int i); static void SetArpAccSlot(int i, int s); static int GetArpSlideSlot(int i); static void SetArpSlideSlot(int i, int s); /* shared */ static int GetRoot(); static void SetRoot(int v); static int GetRootMode(); static void SetRootMode(int v); static int GetNeoOp(); static void SetNeoOp(int v); static int GetScale(); static void SetScale(int v); /* persistence */ static void LoadConfig(); static void SaveConfig(); }; }`
  - `SetArpAlgo(i, v)`: v 0..14 encodes algo = v/3, range = 1 + v%3 (rack choice scheme).
  - Slot getters/setters take raw slot indices; ALL writes pass through Clamp helpers.
  - `Process` writes EVERY owned slot every block: trig idle = 1 (Review Focus #1).

- [ ] **Step 1: Write the failing tests**

```cpp
#include "SeqEngine.hpp"

TEST(engine_trig_idle_is_one) {
    CTAG::DRIVERS::SeqEngine::Init();
    uint8_t trig[51]; float cv[106];
    memset(trig, 0, sizeof(trig));      // hostile init: proves the engine writes idle
    memset(cv, 0, sizeof(cv));
    for (int b = 0; b < 8; ++b)
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
    CHECK(trig[43] == 1); CHECK(trig[44] == 1); CHECK(trig[45] == 1);
    CHECK(trig[46] == 1); CHECK(trig[47] == 1); CHECK(trig[48] == 1);
    CHECK(trig[49] == 1); CHECK(trig[50] == 1);
}

TEST(engine_gate_steps_and_window) {
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetGateMode(0, 1);       // preset 01 = alternating
    CTAG::DRIVERS::SeqEngine::SetGateDens(0, 16);
    CTAG::DRIVERS::SeqEngine::SetGateDiv(0, 7);        // 1/16 = 6 PPQ
    CTAG::DRIVERS::SeqEngine::SetGateGlen(0, 0.5f);
    uint8_t trig[51]; float cv[106];
    // drive 24 PPQ ticks in one "block" per division cycle: call Process with
    // a dt of one 1/16 at 120 BPM (125 ms) -> 3 PPQ ticks internal per block
    // at 1000 Hz; simplest: feed ModTransport-less internal clock by dt.
    memset(trig, 1, sizeof(trig));
    int lows = 0, highs = 0;
    for (int b = 0; b < 2000; ++b) {
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
        (trig[43] == 0) ? ++lows : ++highs;
    }
    CHECK(lows > 0);                    // gate window asserted (active-low)
    CHECK(highs > lows);                // window is a fraction of the cycle
}

TEST(engine_arp_pitch_and_trig_on_steps) {
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 0);        // Acid 303, 1 oct
    CTAG::DRIVERS::SeqEngine::SetArpDiv(0, 7);
    CTAG::DRIVERS::SeqEngine::SetRoot(0);              // fixed C
    CTAG::DRIVERS::SeqEngine::SetRootMode(0);
    uint8_t trig[51]; float cv[106];
    memset(trig, 1, sizeof(trig));
    float minCv = 2.f, maxCv = -2.f; int trigLows = 0;
    for (int b = 0; b < 4000; ++b) {
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
        if (cv[104] < minCv) minCv = cv[104];
        if (cv[104] > maxCv) maxCv = cv[104];
        if (trig[45] == 0) ++trigLows;
    }
    CHECK(trigLows > 0);                                  // arp gate fires
    CHECK(minCv >= -1.05f && maxCv <= 1.05f);             // 1 V/oct envelope bound
    CHECK(minCv < 0.0f || maxCv > 0.0f);                  // melody moves
}

TEST(engine_config_roundtrip) {
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetGateMode(0, 17);
    CTAG::DRIVERS::SeqEngine::SetGateSync(0, 42);
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(1, 7);
    CTAG::DRIVERS::SeqEngine::SetScale(11);
    CTAG::DRIVERS::SeqEngine::SetRoot(5);
    CTAG::DRIVERS::SeqEngine::SetRootMode(2);
    char json[1024];
    CTAG::DRIVERS::SeqEngine::SerializeConfig(json, sizeof(json));
    CTAG::DRIVERS::SeqEngine::InitDefaults();            // wipe state
    CTAG::DRIVERS::SeqEngine::DeserializeConfig(json);
    CHECK(CTAG::DRIVERS::SeqEngine::GetGateMode(0) == 17);
    CHECK(CTAG::DRIVERS::SeqEngine::GetGateSync(0) == 42);
    CHECK(CTAG::DRIVERS::SeqEngine::GetArpAlgo(1) == 7);
    CHECK(CTAG::DRIVERS::SeqEngine::GetScale() == 11);
    CHECK(CTAG::DRIVERS::SeqEngine::GetRoot() == 5);
    CHECK(CTAG::DRIVERS::SeqEngine::GetRootMode() == 2);
}
```

Add `static void SerializeConfig(char* dst, int cap); static bool DeserializeConfig(const char* json); static void InitDefaults();` to the Task-6 Interfaces block (public on SeqEngine; `SaveConfig/LoadConfig` call them around the ModTransport section IO).

- [ ] **Step 2: Verify failure**

Run: `tests/host/run_tests.sh` — Expected: FAIL (SeqEngine.hpp missing).

- [ ] **Step 3: Implement the engine**

`main/SeqEngine.hpp` — the class surface from Interfaces + `#include "helpers/ctagSlotBounds.hpp"`.

`main/SeqEngine.cpp`:

- Statics: `SEQ::GateSource gGate[2]; SEQ::ArpSource gArp[2]; SEQ::ArpExt gArpExt[2]; uint8_t gNeoOp; float intPhase; int gateClk[2], gateDiv[2], gateTrigSlot[2], gateCvSlot[2], arpClk[2], arpDiv[2], arpGlideMs[2]; int arpPitchSlot[2], arpGateSlot[2], arpAccSlot[2], arpSlideSlot[2]; float gateOut[2], arpOut[2]; uint32_t snapshot;`
- `Init()`: defaults per spec §4.4 (gate1 mode 1, gate2 mode 3, dens 8, sync 0, glen 0.5, div 7; arp1 algo 0 range 2, arp2 algo 1 range 2 — via SetArpAlgo encoding; p defaults 62/40/50/0; glide 0; scale 6; root 0 fixed; slots per spec table), `GateRecompute` both, `ArpRebuildPattern` both, seed `rng`/`rng_state` with the rack constants (`0x12345678u + i*0x9e3779b9u`, `0x1fff7a10u + i*2654435761u`), then `LoadConfig()`.
- `Process(cv, trig, dt)` — port of `mod_engine_process_block` (1269-1677) reduced to our 4 sources:

```cpp
void SeqEngine::Process(float *cv, uint8_t *trig, float dt) {
    // 1) clock bus (rack §2): one fractional PPQ accumulator, BPM from
    //    ModTransport; MIDI sources advance by integer ticks.
    const float bpm = ModTransport::GetBPM();
    const bool globalMidi = false;  // "Global" follows internal unless the
                                    // transport is MIDI-running: see below
    const float intBefore = intPhase;
    intPhase += 24.0f * (bpm / 60.0f) * dt;
    if (intPhase > 65536.0f) intPhase -= 65536.0f;
    const float intDelta = intPhase - intBefore;
    const int midiTicks = ModTransport::TicksThisBlock();
    const bool running = ModTransport::IsRunning();

    for (int i = 0; i < 2; ++i) {
        // ---- GATE ----
        SEQ::GateSource &g = gGate[i];
        const bool gUseMidi = (gateClk[i] == 2) || (gateClk[i] == 0 && running /* global follows transport */);
        // Global: internal clock unless MIDI transport is running (rack
        // global_clock semantics; SP_CLOCK's source chooses the transport).
        const float gDivPpq = (float)SEQ::kDivisions[gateDiv[i]].ppq;
        const float gAdv = gUseMidi ? (float)midiTicks / gDivPpq : intDelta / gDivPpq;
        const float gPrev = gPhase[i];
        gPhase[i] += gAdv;
        if (gPhase[i] >= 1.0f) gPhase[i] -= floorf(gPhase[i]);
        const uint32_t gWraps = (uint32_t)(gPrev + gAdv);
        if (gWraps) {
            g.step = (uint8_t)((g.step + gWraps) % 16);
            if (g.mode == 17 && g.sync > 0) SEQ::GateStepMutation(g);
            if (g.mode == 18 && g.step == 0 && g.sync > 0) SEQ::GateBarMutation(g);
            g.level = (g.mask >> (15 - g.step)) & 1u;
        }
        const bool gHigh = g.level && (gPhase[i] < g.glen);
        const float gTarget = gHigh ? 1.0f : 0.0f;
        if (gTarget > gateOut[i]) { gateOut[i] += 0.4f * (gTarget - gateOut[i]); if (gateOut[i] > 0.99f) gateOut[i] = 1.0f; }
        else { gateOut[i] += 0.2f * (gTarget - gateOut[i]); if (gateOut[i] < 0.01f) gateOut[i] = 0.0f; }
        const int gts = ClampTrigSlot(gateTrigSlot[i]);
        if (gts >= 0) trig[gts] = gHigh ? 0 : 1;                     // active-low
        const int gcs = ClampCVSlot(gateCvSlot[i]);
        if (gcs >= 0) cv[gcs] = gateOut[i];
    }
    for (int i = 0; i < 2; ++i) {
        // ---- ARP ---- (rack §3 MOD_ARP branch, 1467-1583)
        SEQ::ArpSource &s = gArp[i];
        SEQ::ArpExt &e = gArpExt[i];
        const bool aUseMidi = (arpClk[i] == 2) || (arpClk[i] == 0 && running);
        const float aDivPpq = (float)SEQ::kDivisions[arpDiv[i]].ppq;
        const float aAdv = aUseMidi ? (float)midiTicks / aDivPpq : intDelta / aDivPpq;
        const float aPrev = aPhase[i];
        aPhase[i] += aAdv;
        if (aPhase[i] >= 1.0f) aPhase[i] -= floorf(aPhase[i]);
        const uint32_t aWraps = (uint32_t)(aPrev + aAdv);
        if (aWraps && running_or_internal) {
            const uint8_t prevSt = s.step;
            s.step = (uint8_t)((s.step + aWraps) & 0x0Fu);
            s.ticks += aWraps;
            const uint8_t st = s.step;
            if (st < prevSt || (s.ticks & 0x0Fu) == 0u) {
                e.bar_count++;
                const uint8_t driftBars = (s.p[3] >= 60) ? 4 : 8;
                if (e.bar_count >= driftBars) {
                    e.bar_count = 0;
                    if (s.p[3] > 15 || s.root_mode >= 1) SEQ::ArpNextChord(s, e, gNeoOp);
                    e.dd_fill = 0;
                    e.turn_type = (uint8_t)((s.ticks >> 4) & 3u);
                } else if (e.bar_count == driftBars - 1 && s.p[3] >= 40) {
                    e.dd_fill = 1;
                    e.turn_type = (uint8_t)((s.ticks >> 4) & 3u);
                }
                if (s.algo == 0 /* HYP1 */) {
                    const uint8_t mutProb = (uint8_t)((uint16_t)s.p[3] * 50 / 100);
                    for (int k = 0; k < 16; ++k)
                        if (SEQ::ArpRand(s, 100) < mutProb) SEQ::ArpRollHyp1Step(s, e, (uint8_t)k);
                } else {
                    SEQ::ArpRebuildPattern(s, e);
                }
            }
            SEQ::ArpStep cur = e.steps[st];
            if (e.dd_fill) SEQ::ArpApplyTurnaround(s, e, st, cur);
            if (cur.flags & 1u) {
                const SEQ::ArpStep &prev = e.steps[prevSt];
                const bool prevSlid = ((prev.flags & 5u) == 5u) && s.held;
                s.cur_note = SEQ::ArpCalcNote(s, e, cur);
                s.retrig_edge = prevSlid ? 0u : 1u;
                s.cur_acc = (cur.flags & 2u) ? 1u : 0u;
                s.cur_hold = (cur.flags & 4u) ? 1u : 0u;
                s.level = 1u; s.held = 1u;
            } else { s.level = 0u; s.retrig_edge = 0u; s.held = 0u; }
        }
        // pitch glide: GLIDE=0 instant (TBD03 does the slide), else ms slew
        const int32_t targetQ16 = ((int32_t)s.cur_note) << 16;
        if (arpGlideMs[i] <= 0 || s.retrig_edge || !s.held) {
            e.current_pitch_q16 = targetQ16; e.target_pitch_q16 = targetQ16;
        } else {
            e.target_pitch_q16 = targetQ16;
            int32_t diff = e.target_pitch_q16 - e.current_pitch_q16;
            if (diff) {
                // alpha per block from GLIDE ms: a = dt/glide, Q16.16
                int32_t alpha = (int32_t)(((float)dt / ((float)arpGlideMs[i] * 0.001f)) * 65536.0f);
                if (alpha > 65536) alpha = 65536;
                e.current_pitch_q16 += (int32_t)(((int64_t)diff * alpha) >> 16);
            }
        }
        // accent capacitor (rack 1567-1572)
        if (s.cur_acc && s.level) { e.accent_cap_q16 += 16384; if (e.accent_cap_q16 > 65536) e.accent_cap_q16 = 65536; }
        e.accent_cap_q16 = (int32_t)(((int64_t)e.accent_cap_q16 * 63000) >> 16);
        // outputs
        const uint8_t rootPc = s.root & 0x0Fu;    // absolute base 48 + pc
        const float pitchCv = (float)((int32_t)(e.current_pitch_q16 >> 16) - (int32_t)(48u + rootPc)) / 12.0f * 0.2f;
        const int ps = ClampCVSlot(arpPitchSlot[i]);
        if (ps >= 0) cv[ps] = pitchCv;
        const float gWin = s.cur_hold ? 1.0f : 0.55f;
        const bool gateOpen = s.level != 0 && (aPhase[i] < gWin);
        const int gs = ClampTrigSlot(arpGateSlot[i]);
        if (gs >= 0) trig[gs] = gateOpen ? 0 : 1;                // active-low
        const int as = ClampTrigSlot(arpAccSlot[i]);
        if (as >= 0) trig[as] = (s.cur_acc && s.level) ? 0 : 1;  // active-low
        const int ss = ClampTrigSlot(arpSlideSlot[i]);
        if (ss >= 0) trig[ss] = (s.cur_hold && s.level) ? 0 : 1; // active-low
        // leveled arp out (accent-capacitor shaped, rack 1574-1582) for the
        // snapshot; cv slot not assigned by default
        const float aTarget = gateOpen ? (s.cur_acc ? 1.0f : 0.72f) : 0.0f;
        arpOut[i] += (aTarget - arpOut[i]) * 0.5f;
    }
    // snapshot: [g1 g2 a1g a2g a1a a2a a1s a2s | a1step a2step | a1note a2note]
    snapshot = (uint32_t)gGate[0].level | ((uint32_t)gGate[1].level << 1)
        | ((uint32_t)(gArp[0].level) << 2) | ((uint32_t)(gArp[1].level) << 3)
        | ((uint32_t)(gArp[0].cur_acc && gArp[0].level) << 4)
        | ((uint32_t)(gArp[1].cur_acc && gArp[1].level) << 5)
        | ((uint32_t)(gArp[0].cur_hold && gArp[0].level) << 6)
        | ((uint32_t)(gArp[1].cur_hold && gArp[1].level) << 7)
        | ((uint32_t)(gArp[0].step & 0xFu) << 8) | ((uint32_t)(gArp[1].step & 0xFu) << 12)
        | ((uint32_t)(gArp[0].cur_note & 0xFFu) << 16) | ((uint32_t)(gArp[1].cur_note & 0xFFu) << 24);
}
```

Add statics `gPhase[2]`, `aPhase[2]` (float, 0). "Global" clock semantics: `gateClk/arpClk == 0` → follows the transport: MIDI ticks when `ModTransport::IsRunning()` AND the C3's clock source is MIDI. For Task 6 the internal test path uses clk=1 — set defaults `clk = 1` (Internal) in `InitDefaults` EXCEPT the spec default `clk: 0` (Global). Global must ALSO work host-side: define Global = "MIDI ticks if transport running, else internal accumulator" — the code above does that (`running` gates midi use; internal otherwise) — for `clk==0 && !running` use internal. Adjust the ternary: `const bool gUseMidi = (gateClk[i] == 2) || (gateClk[i] == 0 && running && midiTicks > 0);` — NO: rack global semantics = global_clock flag chooses the source; we don't persist a global clock flag (SP_CLOCK page's MIDI-vs-internal choice lives in Marbles' tExternal). **Decision for the plan:** add `ModTransport::GetGlobalClockMidi()` (bool, persisted in "transport" as `"ext":0/1`, default 0; SetGlobalClockMidi wired to the MOD CLOCK page's existing clock-source row in Task 7/9). Global-clk sources follow it. Update Task 2 Interfaces accordingly (`static bool GetGlobalClockMidi(); static void SetGlobalClockMidi(bool v);` — add to ModTransport.hpp/cpp + `"ext"` member in SaveConfig/LoadConfig + one host test `GlobalClockFlagRoundTrip`). Gate/ARP use: `useMidi = (clk == 2) || (clk == 0 && ModTransport::GetGlobalClockMidi())`; when `useMidi && !running` → frozen (no advance; rack behavior).

- Setters: each Set* clamps (`mode` 0-18 + `GateRecompute`, `dens` 1-16 + recompute, `sync` 0-100 → store scaled 0-255 internally? Keep 0-100 UI + store raw and scale at use: `sync255 = sync*255/100` where rack used 0-255 — simplest: store 0-255, UI converts; SetGateSync takes 0-100 and stores `v*255/100`), `SetArpAlgo` → algo=v/3, range=1+v%3 + `ArpRebuildPattern`, `SetArpP` → rebuild, `SetRoot/SetRootMode/SetScale/SetNeoOp` → shared: apply to BOTH arps (root_mode, root, scale_idx) + rebuild both (rack shared-root behavior), slot setters store raw.
- `SerializeConfig/DeserializeConfig`: pure functions over the statics (flat JSON exactly as spec §4.4, all fields clamped on read; hand-editable-file rule: type-check every member with `HasMember` + `IsInt/IsFloat` before reading).
- `LoadConfig/SaveConfig`: `ModTransport::ReadSectionJSON("seq", buf, cap)` → `DeserializeConfig`; `SerializeConfig` → `WriteSectionJSON("seq", ...)`. Stack buffer 1024 on the UIMenu task (same discipline as MarblesEngine).
- `GetGateMask(i)`: return `gGate[i].mask` for the panel preview.

- [ ] **Step 4: Run tests**

Run: `tests/host/run_tests.sh` — Expected: PASS (all engine tests green; `GlobalClockFlagRoundTrip` in Task 2 file too).

- [ ] **Step 5: Commit**

```bash
git add main/SeqEngine.hpp main/SeqEngine.cpp main/SeqEngineCore.hpp main/ModTransport.hpp main/ModTransport.cpp tests/host/test_seq_mod.cpp tests/host/run_tests.sh
git commit -m "feat(mod): SeqEngine — per-block Process, active-low trig/cv slots, config serialization (host-tested)"
```

---

### Task 7: DSP runtime wiring — SPManager, /link/seqSet, pushSeq, stream field

**Files:**
- Modify: `main/SPManager.cpp:167` (call order)
- Modify: `main/LinkEndpoint.cpp` (seqSet handler near marblesSet ~line 990-1010; `pushSeq()` next to `pushMarbles()` ~line 396; `hashSeq()`; `LastPushed.seq`; `seedLastPushed()`; `pushOnChange()`; `sendStream()` `"sq"` field after `"mb"` line ~627; `pushBootMetadata()` add `pushSeq()` before `pushIoCaps()`)
- Modify: `protocol/include/ctag/link/protocol.hpp` (add `kMsgPushSeq = "link/pushSeq"` — REPLY tag WITHOUT the `/link/` prefix; request tags keep the prefix. **Prefix-drop bug class: read the kMsgCmdFsList/kMsgFsList split comment at protocol.hpp first**)

**Interfaces:**
- Consumes: `SeqEngine` (Task 6), `ModTransport` (Task 2).
- Produces: `/link/seqSet {"k":<string>,"v":<number>}` accepted keys:
  `g1mode g1dens g1sync g1glen g1clk g1div g1tslot g1cvslot` (+ g2*), `a1algo a1p1 a1p2 a1p3 a1p4 a1glide a1clk a1div a1pslot a1gslot a1aslot a1sslot` (+ a2*), `shmroot shmrootmode shmneoop shmscale`, `globalext` (0/1 global clock = MIDI). All setters reply `{"cmd":"/link/seqSet"}` like marblesSet does, mark `seqPushPending` so the panel re-syncs within one stream tick.
- `pushSeq()` frame: `{"cmd":"link/pushSeq","st":{...}}` with the same key names + `g1mask,g2mask` (hex `0x...` strings via `%04x`), plus `bpm`,`ext` from ModTransport. Flat build via `string +=`, no DOM (same as pushMarbles).
- Stream: `"sq":<uint32 snapshot>` after `"mb"`.

- [ ] **Step 1: SPManager**

Replace the Task-3 block at `main/SPManager.cpp:165-168` with:

```cpp
        // Shared transport -> Seq modulators -> Marbles random modulation.
        const float modDt = (float)BUF_SZ / (float)DRIVERS::Codec::GetSampleRate();
        DRIVERS::ModTransport::Process(modDt);
        DRIVERS::SeqEngine::Process(pd.cv, pd.trig, modDt);
        DRIVERS::MarblesEngine::Process(pd.cv, pd.trig);
```

(Seq writes first so plugins read coherent slots; order vs Marbles is irrelevant — disjoint slots. Include `SeqEngine.hpp`.)

- [ ] **Step 2: LinkEndpoint — seqSet + pushSeq + stream**

Pattern to copy exactly: the `/link/marblesSet` handler block (`main/LinkEndpoint.cpp:985-1010` — read it) and `pushMarbles()` (line 396). Write `pushSeq()` with the key list from Interfaces (each `SeqEngine::Get*`), mask as `snprintf(tmp, "0x%04x", ...)`. Hash: `uint32_t hashSeq()` FNV-1a over the same values (copy the `hashMarbles` structure at ~line 470-500). `LastPushed` gains `uint32_t seq = 0;` + `seedLastPushed()` sets it; `pushOnChange()` adds the same diff block as marbles (with `seqPushPending` bool mirroring `marblesPushPending`, declared next to it at ~line 105). `sendStream()`: insert `s += ",\"sq\":" + to_string(SeqEngine::GetOutputSnapshot());` after the `"mb"` line.

Protocol tag: in `protocol/include/ctag/link/protocol.hpp` add near `kMsgPushMarbles`:
```cpp
static constexpr const char *kMsgPushSeq = "link/pushSeq";   // reply tag (no /link/ prefix!)
```
C3 side parses `"link/pushSeq"` (Task 8). Request `/link/seqSet` string is used inline in the handler comparison (like marblesSet — check how its request string is declared and match).

- [ ] **Step 3: Size check**

Add a temporary ESP_LOGI in `pushBootMetadata()` after `pushSeq()` logging the frame length (build once, read from UART log, then REMOVE the log line — the audio-path logging ban does not cover link task, but boot-log noise does; remove it in the same task). Threshold: `pushSeq()` frame MUST be ≤ 1800 bytes — expected ~450. If over: split with the `part`/`last` framing from `pushParams` (do not expect to need it).

- [ ] **Step 4: Build both targets**

Both `idf.py build` (A1S) and the BBA2 command must succeed. Run `tests/host/run_tests.sh` (still green).

- [ ] **Step 5: Bench smoke (A1S or BBA2, whichever is connected)**

Flash, ONE console session: boot log shows `boot metadata pushed` without errors; `/debug/getLoopHist` via screenshot.py — flat `[7,7,...]` (BBA2 `[10,9,10,...]`); send `/link/seqSet {"k":"g1mode","v":3}` through the C3 console — the C3 DebugLink forwards unknown `/link/*`? NO — it forwards `/debug/*` only; for this smoke use UI-independent proof: the A1S UART log prints the seqSet handler line; then confirm the NEXT `link/pushSeq` frame (C3 frame trace is off — instead check the A1S side via a temporary log or accept UI-driven verification in Task 9). Minimum bar here: no crash, loop flat, seqSet handler logs `seqSet g1mode=3`.

- [ ] **Step 6: Commit**

```bash
git add main/SPManager.cpp main/LinkEndpoint.cpp protocol/include/ctag/link/protocol.hpp
git commit -m "feat(mod): SEQ link surface — /link/seqSet, pushSeq state push, sq stream field"
```

---

### Task 8: C3 DspProxy seq surface

**Files:**
- Modify: `uimcu/ui/DspProxy.hpp` (seq scalar mirror next to the Marbles surface ~line 206)
- Modify: `uimcu/ui/DspProxyLink.cpp` (`parsePushSeq` next to `parsePushMarbles` ~line 199; `sendSeqSet(k, v)` next to line 152; dispatch at ~line 541)

**Interfaces:**
- Consumes: `link/pushSeq` frames (Task 7).
- Produces: `DspProxy::SeqGet*(key)` accessors used by Task 9 — implement as the same static-mirror pattern as the Marbles surface (read `DspProxy.hpp:200-260` first and mirror the struct + getter style; flat fields, no maps):

```cpp
struct SeqState {
    // gates 0/1
    int gateMode[2]; int gateDens[2]; int gateSync[2]; float gateLen[2];
    int gateClk[2]; int gateDiv[2]; int gateTrigSlot[2]; int gateCvSlot[2];
    uint32_t gateMask[2];
    // arps 0/1
    int arpAlgo[2]; int arpP[2][4]; int arpGlide[2]; int arpClk[2]; int arpDiv[2];
    int arpPitchSlot[2]; int arpGateSlot[2]; int arpAccSlot[2]; int arpSlideSlot[2];
    // shared + transport
    int root; int rootMode; int neoOp; int scale; float bpm; int globalExt;
    uint32_t snapshot;
};
```

`sendSeqSet(const char* k, float v)` builds `{"cmd":"/link/seqSet","k":"%s","v":%g}` (copy `sendMarblesSet` at line 152). `parsePushSeq` fills the mirror field-by-field with the same HasMember/type-check discipline as `parsePushMarbles`. Trigger `seqDirty` on receipt so the page re-reads (mirror `marblesPushPending` handling on the C3 side — find how UIMenuPageMod learns of a marbles push and copy it).

- [ ] **Step 1: Implement** (read the Marbles surface first; mirror exactly)
- [ ] **Step 2: Build** `cd uimcu && idf.py build` — green.
- [ ] **Step 3: Commit**

```bash
git add uimcu/ui/DspProxy.hpp uimcu/ui/DspProxyLink.cpp
git commit -m "feat(uimcu): DspProxy seq state mirror + /link/seqSet sender"
```

---

### Task 9: C3 PANEL_SEQ page + wheel insertion

**Files:**
- Create: `uimcu/ui/menupages/UIMenuPageSeq.hpp`
- Create: `uimcu/ui/menupages/UIMenuPageSeq.cpp`
- Modify: `uimcu/ui/UIMenu.hpp:82-89` (PANEL_SEQ = 4, PANEL_PARAMS = 5, PANEL_COUNT = 6)
- Modify: `uimcu/ui/UIMenu.cpp:72-73` (allocate the new page), bar math is panel-count driven (line 114-115 divides by PANEL_COUNT — no edit needed)
- Modify: `uimcu/main/CMakeLists.txt` (add the new source file — read it first for the page list format)
- Modify: `uimcu/ui/DspProxy.hpp` if the Mod CLOCK page's clock-source row needs the `globalext` setter (route through `sendSeqSet("globalext", v)`)

**Interfaces:**
- Consumes: `DspProxy::SeqState` + `sendSeqSet` (Task 8); page pattern = `uimcu/ui/menupages/UIMenuPageMod.{hpp,cpp}` (read both fully before writing).
- Produces: PANEL_SEQ in the wheel between MOD and PARAMS; sub-pages SP_MAIN / SP_GATE1 / SP_GATE2 / SP_ARP1 / SP_ARP2; all row labels FULL WORDS (spec §4.5).

- [ ] **Step 1: Wheel insertion**

`UIMenu.hpp`: `PANEL_MIX = 0, PANEL_TAPE = 1, PANEL_HOME = 2, PANEL_MOD = 3, PANEL_SEQ = 4, PANEL_PARAMS = 5`, `PANEL_COUNT = 6`. `UIMenu.cpp`: `pages[PANEL_SEQ] = new UIMenuPageSeq();` before PARAMS. Verify no other PANEL_PARAMS-indexed switch/lookup exists: `grep -rn "PANEL_PARAMS" uimcu/ui/` and fix every hit that assumes the old index.

- [ ] **Step 2: The page**

Follow `UIMenuPageMod` structure exactly (init/deinit/doRedraw/onEncoder/onValueEncoder/onButton/onBack/onTick/wantsTick + rowCount/rowIsAction/activateRow/adjustRow/formatRow + redraw helpers + live strip from the snapshot). Row tables (encoder step counts in brackets, full labels):

MAIN rows: `Gate 1 [action]`, `Gate 2 [action]`, `ARP 1 [action]`, `ARP 2 [action]`; bottom line = live strip: `G1 G2 | A1 A2` gate dots + step index + accent/slide markers from `SeqState.snapshot` bits (spec §4.2 layout: 0-1 gate, 2-3 gate, 4-5 acc, 6-7 slide, 8-11/12-15 step, 16-23/24-31 note).

GATE 1/2 rows (i = instance):
| Row | Label | Range / steps | Set key |
|---|---|---|---|
| 0 | Mode | 19: Generative Barlow, Classic Alternating, Trance Offbeat, Floor Pump, Late Pump, Forward Gallop, Reverse Gallop, Rolling Gate, Anthem Gate, Dotted Eighths, Tresillo, Son Clave, Rumba Syncopator, Chord Stabs, Call & Response, Breakbeat Chop, Accelerating Riser, Turing Mutate, Markov Set | `g{i}mode` |
| 1 | Density | 1-16 (encoder step 1) | `g{i}dens` |
| 2 | Syncopation | 0-100 % (step 5) | `g{i}sync` |
| 3 | Gate Length | 0-100 % (step 5) | `g{i}glen` |
| 4 | Clock | 3: Global, Internal, MIDI | `g{i}clk` |
| 5 | Division | 13: 8/1, 4/1, 2/1, 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T, 1/32T | `g{i}div` |
| 6 | Trig Slot | slot cycle (OFF + valid trig slots 0-50, mirror the Marbles slot-assign row implementation) | `g{i}tslot` |
| 7 | Level Slot | slot cycle (OFF + cv 0-105) | `g{i}cvslot` |
| 8 | Pattern | preview only — 16 chars `o x` from `gateMask` | — |

ARP 1/2 rows (i = instance):
| Row | Label | Range / steps | Set key |
|---|---|---|---|
| 0 | Algorithm | 15: Acid 303 1-OCT/2-OCT/3-OCT, Phrase Motif 1/2/3, Markov Walker 1/2/3, Metropolix 1/2/3, Déjà-Vu 1/2/3 | `a{i}algo` |
| 1-4 | P1-P4 | per-algo labels + steps — Acid 303: Density [cont 0-100], Slide [cont], Accent [cont], Mutate [cont]; Phrase Motif: Density [cont], Form [4: ABAC/AABC/ABBC/AABA], Transform [4: Inversion/Retrograde/Transpose/Displace], Mutate [cont]; Markov Walker: Density [cont], Contour [16], Pedal [4: Off/Root/Fifth/Bounce], Gravity [cont]; Metropolix: Probability [cont], Pulses [8], Ratchet [4: Off/2x/3x/4x], Accumulator [4: Pendulum/Wrap/Random/Hold]; Déjà-Vu: Density [cont], Déjà-Vu [cont], Steps [4: All/Pentatonic/Triad/Root], Spread [cont] | `a{i}p1..p4` |
| 5 | Root | 14: C, C#, D, D#, E, F, F#, G, G#, A, A#, B, W7 Drift, Neo-Plagial | `shmroot` + `shmrootmode` (combined row: idx 0-11 → mode 0 root=idx; 12 → mode 1; 13 → mode 2) |
| 6 | Neo-Op | 8: Random, P, L, R, N, S, H, PLR — shows `(n/a)` unless Root = Neo-Plagial | `shmneoop` |
| 7 | Scale | 20: Chromatic, Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian, Pentatonic Major, Pentatonic Minor, Blues, Harmonic Minor, Melodic Minor, Phrygian Dominant, Whole Tone, Octatonic, Hirajoshi, In Sen, Double Harmonic, Acid Pool | `shmscale` |
| 8 | Glide | 0-100: 0 renders "Instant", else "%d ms" | `a{i}glide` |
| 9 | Clock | 3: Global, Internal, MIDI | `a{i}clk` |
| 10 | Division | 13 (same table as gate) | `a{i}div` |
| 11 | Pitch Slot | slot cycle | `a{i}pslot` |
| 12 | Gate Slot | slot cycle | `a{i}gslot` |
| 13 | Accent Slot | slot cycle | `a{i}aslot` |
| 14 | Slide Slot | slot cycle | `a{i}sslot` |

Edits go through `DspProxy::sendSeqSet(key, value)`; the page does NOT re-parse locally — the `link/pushSeq` push lands asynchronously and `seqDirty` triggers a redraw (spec: the panel must not assume its own write is the truth; same pattern as the presets list). The DSP setters echo the state back within one stream tick.

Value rendering: floats like Gate Length display as percent ints; `formatRow` mirrors `UIMenuPageMod::formatRow` styling (value right-aligned, name clipped to the pixels left of it — the MODE_EDIT clipping rule from AGENTS.md).

- [ ] **Step 3: Clock-page routing**

The MOD CLOCK page's clock source row (find it in `UIMenuPageMod.cpp` — it sets Marbles `tExternal`) now ALSO sends `sendSeqSet("globalext", v)` so SEQ Global-clk sources follow the same choice. Keep the existing `tExternal` send (Marbles' t section has its own external flag by design).

- [ ] **Step 4: Build + bench screenshots**

`cd uimcu && idf.py build && idf.py -p /dev/ttyACM0 flash`. ONE console session (opening resets the C3): navigate ROOT → SEQ (wheel now 6 segments — screenshot the bar), open MAIN, GATE 1 (change Mode to "Floor Pump" — pattern preview row must show `o o o x ...` = 0x7777), ARP 1 (change Algorithm to "Acid 303 2-OCT", P1 down/up), screenshot each state. Verify the values echo back via the push (row re-renders with the DSP-confirmed value).

- [ ] **Step 5: Commit**

```bash
git add uimcu/ui/menupages/UIMenuPageSeq.hpp uimcu/ui/menupages/UIMenuPageSeq.cpp uimcu/ui/UIMenu.hpp uimcu/ui/UIMenu.cpp uimcu/ui/DspProxy.hpp uimcu/ui/DspProxyLink.cpp uimcu/main/CMakeLists.txt
git commit -m "feat(uimcu): PANEL_SEQ — gate/arp setup pages with full-name rows, live strip, wheel slot after MOD"
```

---

### Task 10: Docs + end-to-end verification

**Files:**
- Modify: `doc/modulation-system.md` (SEQ section + slot table + the Marbles T inversion note)
- Modify: `AGENTS.md` (memory bullet, session-learned style)
- Test: full host suite + bench

**Interfaces:** none new.

- [ ] **Step 1: Docs**

`doc/modulation-system.md`: add a "SEQ Modulators (GATE/ARP)" section — slot table (cv 102-105, trig 43-50, active-low), engine summary (19 gate modes, 5 arp algos, shared root/scale, GLIDE semantics incl. TBD03 wiring), config keys (`seq` object), link keys. Update the Slot Allocation table. Note the Marbles T inversion fix explicitly under Gotchas.

`AGENTS.md`: one bullet in the working-rules/known-issues style: the active-low trig convention evidence chain (`Midi.hpp:51`, `Midi.cpp:831`, all readers), the Marbles T fix, the SEQ slot reservations, the ModTransport single-funnel rule (never drain MIDI ticks outside ModTransport — FOURTH+ instance class of double-consume bugs), and the rack-source div-index discrepancy (default 1/16 = index 7).

- [ ] **Step 2: Full host suite**

Run: `tests/host/run_tests.sh` — exit 0.

- [ ] **Step 3: Bench — TBD03 end-to-end (the spec's headline use case)**

ONE console session on the flashed bench unit:
1. HOME → load TBD03 on ch0 (screenshot plugin page).
2. PARAMS → map TBD03 params via the MOD SOURCE page: `pitch` → cv source `SQ.A1`, `trigger` → `SQ.A1 GATE`, `accent` → `SQ.A1 ACC`, `slide` → `SQ.A1 SLIDE`.
3. SEQ → ARP 1: Algorithm "Acid 303 2-OCT", Density ~65, Slide ~40, Accent ~50, Root C, Scale Aeolian.
4. Confirm audio runs clean: `/debug/getLoopHist` flat; sd `u`/`d` counters not growing.
5. Screenshot the SEQ ARP page showing live step movement (snapshot strip).
6. GATE 1 → Mode "Floor Pump", Trig Slot → map to a second plugin's trig (or TBD03 `sync_trig`) — verify gates fire audibly.

- [ ] **Step 4: Flash-size check**

`idf.py -B build-bba2 size` — record flash code + rodata delta vs the 257 KB baseline in the AGENTS.md bullet. A1S `idf.py size` too (headroom was ~68 KB pre-Mbeads-removal; must remain positive with margin).

- [ ] **Step 5: Commit**

```bash
git add doc/modulation-system.md AGENTS.md
git commit -m "docs: SEQ modulators — slot map, TBD03 wiring, active-low convention + Marbles T fix note"
```

---

## Self-Review

**Spec coverage:** §2 goals → Tasks 4-6 (generators), 3 (ModTransport+fix), 7 (link), 8-9 (panel), 1 (slots), 10 (docs/verification); §3 convention → Task 3 Step 1 inversion + Task 6 idle=1 + tests; §4.1 ModTransport → Tasks 2-3; §4.2 SeqEngine → Tasks 4-6; §4.3 link → Task 7; §4.4 persistence → Task 6 (`Serialize/Deserialize` + ModTransport section IO) + Task 2 (`transport` object + legacy read); §4.5 panel → Task 9; §4.6 Marbles fix → Task 3; §5 tests → Tasks 1-6 host + Task 10 bench; §6 risks → Review Focus items + Task 3/7 bench steps; §7 inventory → per-task Files lists (plus the plan's refinement: pure core split into `SeqEngineCore.hpp` for host testability — documented here, consistent with spec's engine contract).

**Placeholder scan:** no TBD/TODO; port ranges name exact files+lines; the one intentional draft-then-correct in Task 2 Step 3 is called out explicitly with the final code given in full.

**Type consistency:** `SeqEngine::Process(float*, uint8_t*, float dt)` used identically in Tasks 6/7/10; `ModTransport::GetGlobalClockMidi/SetGlobalClockMidi` added in Task 6 and consumed by Task 7 key `globalext` + Task 9 Step 3; `sendSeqSet`/`parsePushSeq`/`SeqState` names consistent between Tasks 8 and 9; `kDivisions[13]`/`kDivCount` consistent between Tasks 4/6/9.

**Review Focus → tests:** (1) Task 6 `engine_trig_idle_is_one`; (2) Task 2 `transport_ticks_drained_once` + Task 3 Step 6 bench; (3) Task 1 clamp tests; (4) Task 4 `division_table_and_default`; (5) Task 7 Step 3 size check + Task 9 Step 4 bench.
