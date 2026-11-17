# VARG Block Chords Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a seventh ARP algorithm that sounds the VARG black-metal harmony as polyphonic block chords on every gated strum, keeping the 16 guitar masks and per-bar mutation.

**Architecture:** A new algo id reuses VARG's progression/quality/mask machinery but writes a packed chord index (root offset × quality, 0..107) directly to the ARP pitch CV instead of a single chord tone. TBDings decodes that index on a new CV-capable `vargChord` param, applying the root offset and selecting a Rings voicing from a table widened with the doc's missing chord shapes. No new CV/gate slots.

**Tech Stack:** C++17/20, ESP-IDF 6.1.1 (A1S + BBA2/S3), ESP32-C3 panel firmware, host g++ unit tests, eurorack git submodule.

**Spec:** `docs/superpowers/specs/2026-11-17-varg-block-chords-design.md`

## Global Constraints

- `kArpAlgoVarg` (5) must stay byte-for-byte; new algorithm is `kArpAlgoVargBlock = 6`.
- Algo encoding `v = algo * 3 + (range - 1)`; `kArpAlgoCount = 7`; engine clamp maximum `20`; panel `clampI(v, 0, 20)`.
- Packed chord index: `index = curOff * 9 + curQual` (0..107); `cv = index / 108.0f`. Write the CV directly — never through `s.cur_note` (8-bit) or the glide accumulator.
- No new rack slots: `N_CVS` stays 106, `N_TRIGS` stays 51; do not touch `main/CVSlotNames.hpp` or `uimcu/ui/CVSlotNames.hpp`.
- `kNumChords = 17` (11 existing + 6 new: 5b5, dim7, m7b5, aug, add9, m+b9). Chord tables stay `const` in flash; TBDings arena is exact (105,708 B) — no Part-member table.
- New TBDings param `vargChord`: `{min:0, max:1, type:"int", def:0}`, cv-capable, added to `mui-TBDings.jsn` with a `"def"` so old presets pre-fill it.
- `BRYAN_CHORDS` is active (`rings/dsp/dsp.h:35`): new voicing rows follow its compact dropped-fifth convention.
- The eurorack submodule is a separate git repo: commit there, then update the parent pointer. Parent changes go in the parent repo.
- Flash policy: app-only by default; `spiffs_image/` changes (the mui JSON) go to the device by bench push (`spiffs_push_file.py`, backup first), NOT a full flash.

## Review Focus

- **An old algo-5 preset switched to algo 6 without remapping its CV** — the root must wander with a static manual chord, never produce noise or a hard-fault. Pinned by Task 6 bench step (unmapped-CV check).
- **Float round-trip of the packed index** — `round(cv*108)` must reproduce the exact `(off,qual)` for all 108 indices; a 1-off decode selects a wrong chord. Pinned by Task 1 host test and Task 2 shell test.
- **Quality identity at polyphony 4** — only 3 chord slots exist there; `dim7`/`m7b5`/`add9` must keep their defining tones, not collapse to a major/minor triad. Pinned by Task 4 host test via the `ChordVoicing()` accessor.
- **A preset saved before `vargChord` existed** — an edit of the new param must not be silently dropped. Pinned by Task 5 `def` entry and Task 6 bench load check.
- **The C3 panel's algo name/param tables and the DSP clamp must agree** — an out-of-range algo must not produce a blank row or a rejected `SetSeqArpAlgo`. Pinned by Task 3 build + Task 6 bench algo sweep.

---

### Task 1: Shared chord vocabulary header and encode/decode tests

**Files:**
- Create: `components/ctagSoundProcessor/helpers/varg_chord.h`
- Modify: `main/SeqEngineCore.hpp` (remove the duplicate `VargQuality` enum, `kVargTones`; include the new header)
- Test: `tests/host/test_varg_core.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: in namespace `CTAG::SEQ` — `enum VargQuality { VQ_P5=0, VQ_MAJ, VQ_MIN, VQ_5B5, VQ_DIM7, VQ_M7B5, VQ_AUG, VQ_ADD9, VQ_MB9, VQ_COUNT }`; `struct VargToneSet { uint8_t n; uint8_t st[5]; }`; `inline constexpr VargToneSet kVargTones[VQ_COUNT]` (copied verbatim from the current `SeqEngineCore.hpp`, no value change); `inline constexpr uint8_t kVargToVoicing[VQ_COUNT] = {1,10,3,11,12,13,14,15,16}`; `inline constexpr int kVargChordSlotCount = 108`; `inline uint16_t EncodeChord(uint8_t off, uint8_t qual)` returning `off*9+qual`; `inline void DecodeChord(uint16_t idx, uint8_t& off, uint8_t& qual)`.

- [ ] **Step 1: Write the failing test**

Append to `tests/host/test_varg_core.cpp`:

```cpp
#include "helpers/varg_chord.h"

TEST(varg_chord_index_roundtrip) {
    for (uint8_t off = 0; off < 12; ++off)
        for (uint8_t q = 0; q < VQ_COUNT; ++q) {
            const uint16_t idx = EncodeChord(off, q);
            CHECK(idx == off * 9u + q);
            CHECK(idx < kVargChordSlotCount);
            uint8_t dOff = 0xFF, dQual = 0xFF;
            DecodeChord(idx, dOff, dQual);
            CHECK(dOff == off);
            CHECK(dQual == q);
        }
}

TEST(varg_chord_cv_float_roundtrip) {
    // The DSP writes cv = index/108.0f as a float; TBDings decodes with
    // lround(cv*108). Every index must survive that exact path.
    for (uint16_t idx = 0; idx < kVargChordSlotCount; ++idx) {
        const float cv = (float)idx / 108.0f;
        CHECK((uint16_t)lroundf(cv * 108.0f) == idx);
    }
}

TEST(varg_voicing_map_covers_every_quality) {
    CHECK(kVargToVoicing[VQ_P5]   == 1);   // existing "5"
    CHECK(kVargToVoicing[VQ_MAJ]  == 10);  // existing "M"
    CHECK(kVargToVoicing[VQ_MIN]  == 3);   // existing "m"
    for (uint8_t q = 0; q < VQ_COUNT; ++q)
        CHECK(kVargToVoicing[q] < 17);
    // each quality must get a distinct voicing
    for (uint8_t a = 0; a < VQ_COUNT; ++a)
        for (uint8_t b = (uint8_t)(a + 1); b < VQ_COUNT; ++b)
            CHECK(kVargToVoicing[a] != kVargToVoicing[b]);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `tests/host/run_tests.sh`
Expected: compile failure — `helpers/varg_chord.h: No such file or directory`.

- [ ] **Step 3: Create the shared header and de-duplicate the engine**

Create `components/ctagSoundProcessor/helpers/varg_chord.h` with the exact interface above. Guard with `#pragma once`, include `<cstdint>` and `<cmath>`, no ESP-IDF headers. In `main/SeqEngineCore.hpp`, add `#include "helpers/varg_chord.h"` inside the existing `namespace CTAG { namespace SEQ {` include area and delete the now-duplicate enum and `kVargTones` definition. Leave `kVargProgressions`, `kVargMasks`, `VargBiasQuality`, `VargNextChord`, `VargLoadChord`, `ArpBuildVarg` in place — they keep using the same names via the shared header.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `tests/host/run_tests.sh`
Expected: all tests PASS, including the three new ones and the existing `varg_*` tests (no value changed).

- [ ] **Step 5: Commit**

```bash
git add components/ctagSoundProcessor/helpers/varg_chord.h main/SeqEngineCore.hpp tests/host/test_varg_core.cpp
git commit -m "feat(varg): shared chord vocabulary header + index encode/decode tests"
```

---

### Task 2: Algo 6 in the ARP engine

**Files:**
- Modify: `main/SeqEngineCore.hpp:507-516` (enum), `:1214-1245` (`ArpRebuildPattern`), `:1280-1292` (`ArpCalcNote`)
- Modify: `main/SeqEngine.cpp:344-352` (clamp + init marker), `:270-277` (direct CV write)
- Test: `tests/host/test_varg_core.cpp`

**Interfaces:**
- Consumes: `EncodeChord`, `kVargChordSlotCount`, `VQ_COUNT` from Task 1.
- Produces: `SEQ::kArpAlgoVargBlock = 6`; `SEQ::kArpAlgoCount = 7`; `SEQ::ArpCalcNote(s, e, step)` returns the audible root MIDI note for algo 6 (unchanged signature); `SeqEngine::SetArpAlgo`/`GetArpAlgo` accept 0..20.

- [ ] **Step 1: Write the failing tests**

Append to `tests/host/test_varg_core.cpp`:

```cpp
TEST(varg_block_algo_encoding) {
    CTAG::DRIVERS::SeqEngine::Init();
    for (int v = 18; v <= 20; ++v) {
        CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, v);
        CHECK(CTAG::DRIVERS::SeqEngine::GetArpAlgo(0) == v);
    }
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 99);
    CHECK(CTAG::DRIVERS::SeqEngine::GetArpAlgo(0) == 20);
}

TEST(varg_block_cv_carries_packed_chord) {
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 18);   // VARG BLOCK, range 1
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 0, 10);   // i-bVII
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 1, 0);    // TRM16
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 2, 50);   // TRIAD
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 3, 0);    // no mutation
    uint8_t trig[51]; float cv[106];
    memset(trig, 1, sizeof(trig));
    memset(cv, 0, sizeof(cv));
    // first gated step: cv[104] must equal EncodeChord(0, VQ_MIN)/108
    for (int b = 0; b < 64; ++b) {
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
        if (trig[45] == 0) break;
    }
    const float expected = (float)EncodeChord(0, VQ_MIN) / 108.0f;
    CHECK(fabsf(cv[104] - expected) < 1e-5f);
}

TEST(varg_block_gates_follow_mask) {
    CTAG::DRIVERS::SeqEngine::Init();
    CTAG::DRIVERS::SeqEngine::SetArpAlgo(0, 18);
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 1, 15);   // DBL 0xF000
    CTAG::DRIVERS::SeqEngine::SetArpP(0, 3, 0);
    uint8_t trig[51]; float cv[106];
    memset(trig, 1, sizeof(trig)); memset(cv, 0, sizeof(cv));
    int gated = 0;
    for (int b = 0; b < 2100; ++b) {
        CTAG::DRIVERS::SeqEngine::Process(cv, trig, 1.0f / 1000.0f);
        if (trig[45] == 0) ++gated;
    }
    CHECK(gated > 0);
}

// update the existing algo-encoding test's clamp expectation:
//   CTAG::DRIVERS::SeqEngine::SetArpAlgo(1, 99);
//   CHECK(CTAG::DRIVERS::SeqEngine::GetArpAlgo(1) == 20);
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `tests/host/run_tests.sh`
Expected: FAIL — `varg_block_algo_encoding` clamps to 17, `varg_block_cv_carries_packed_chord` sees a 0.2 V/oct pitch CV.

- [ ] **Step 3: Implement the engine changes**

In `main/SeqEngineCore.hpp`: add `kArpAlgoVargBlock = 6` before `kArpAlgoCount`, set `kArpAlgoCount = 7`. In `ArpRebuildPattern`, route `case kArpAlgoVargBlock:` through `ArpBuildVarg(s, ext, advanceChord)` — the progression/mask/mutation state machine is identical. In `ArpCalcNote`, treat `kArpAlgoVargBlock` exactly like `kArpAlgoVarg` (audible root, semitone-exact, no scale folding).

In `main/SeqEngine.cpp`: change `SetArpAlgo`'s clamp to `SEQ::kArpAlgoCount * 3 - 1`; extend the fresh-switch marker condition so switching into **or out of** either VARG algo sets `gArpExt[i].st.varg.chordIdx = 0xFF`. In the bar-boundary drift guards, skip the shared W7/Neo drift for both VARG algos. After the normal `pitchCv` computation, if `s.algo == SEQ::kArpAlgoVargBlock`, overwrite `cv[arpPitchSlot[i]] = (float)SEQ::EncodeChord(e.st.varg.curOff, e.st.varg.curQual) / 108.0f`. Leave gate/accent/slide trig writes untouched.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `tests/host/run_tests.sh`
Expected: all tests PASS, including the three new ones and the existing algo-5 VARG tests.

- [ ] **Step 5: Commit**

```bash
git add main/SeqEngineCore.hpp main/SeqEngine.cpp tests/host/test_varg_core.cpp
git commit -m "feat(varg): block-chord algo 6 with packed chord CV"
```

---

### Task 3: Panel selects algo 6

**Files:**
- Modify: `uimcu/ui/menupages/UIMenuPageSeq.cpp:28-34` (`arpAlgoNames`), `:51-64` (`arpParamNames`), `:66-76` (`arpParamChoices`), `:78-86` (`arpParamMax`), `:88-95` (`arpParamStep`)
- Modify: `uimcu/ui/DspProxyLink.cpp:1325-1330` (`SetSeqArpAlgo` clamp)

**Interfaces:**
- Consumes: `kArpAlgoCount = 7` from Task 2 (panel uses raw numbers, no header dependency).
- Produces: algos 18/19/20 display as `"VARG BLOCK 1-OCT"`, `"VARG BLOCK 2-OCT"`, `"VARG BLOCK 3-OCT"`; their four P rows are identical to VARG's.

- [ ] **Step 1: Verify the change is needed**

Run: `grep -n 'clampI(v, 0, 17)' uimcu/ui/DspProxyLink.cpp`
Expected: one match — the algo setter rejects 18..20.

- [ ] **Step 2: Extend the panel tables and clamp**

Change `arpAlgoNames[18]` → `[21]` and append the three `"VARG BLOCK n-OCT"` names. Change each of `arpParamNames`, `arpParamChoices`, `arpParamMax`, `arpParamStep` from `[6][4]` → `[7][4]`, appending a seventh row copied verbatim from the VARG row (`{"Progression","Rhythm","Quality","Mutate"}`, the same choices string, `{10,15,2,100}`, `{1,1,1,1}`). Change `clampI(v, 0, 17)` → `clampI(v, 0, 20)` and its comment to `// 7 algos x 3 ranges`.

- [ ] **Step 3: Build the panel firmware**

Run: `source /home/schrab/esp/esp-idf-v6.1.1/export.sh && cd uimcu && idf.py build`
Expected: build succeeds; no out-of-bounds warnings.

- [ ] **Step 4: Commit**

```bash
git add uimcu/ui/menupages/UIMenuPageSeq.cpp uimcu/ui/DspProxyLink.cpp
git commit -m "feat(panel): expose VARG BLOCK algorithm rows"
```

---

### Task 4: Rings voicing table for the doc's chord qualities

**Files:**
- Modify (submodule `components/mutable/eurorack`): `rings/dsp/performance_state.h:34` (`kNumChords`), `rings/dsp/string_synth_part.cc:113-220` (the `BRYAN_CHORDS` table), `rings/dsp/string_synth_part.h:48-56` (accessor declaration)
- Test: `tests/host/test_varg_core.cpp`
- Modify: parent pointer `components/mutable/eurorack`

**Interfaces:**
- Consumes: `VQ_*` and `kVargToVoicing` from Task 1; `kNumChords = 17`.
- Produces: `rings::ChordVoicing(int polyphony, int chord)` returning `const float*` to `chords[polyphony-1][chord]`; `rings::ChordSize(int polyphony)` returning `min(12/polyphony, 8)`. New voicing indices 11..16 = 5b5, dim7, m7b5, aug, add9, m+b9.

- [ ] **Step 1: Write the failing test**

Append to `tests/host/test_varg_core.cpp`:

```cpp
#include "rings/dsp/string_synth_part.h"

TEST(varg_block_voicings_keep_quality_identity) {
    using namespace rings;
    CHECK(kNumChords == 17);
    // 3-note qualities survive the polyphony-4 (3-slot) truncation whole
    const uint8_t aug = kVargToVoicing[VQ_AUG];
    const uint8_t mb9 = kVargToVoicing[VQ_MB9];
    const float* a = ChordVoicing(4, aug);
    const float* m = ChordVoicing(4, mb9);
    CHECK(fabsf(a[0]) < 0.01f && fabsf(a[1] - 4.f) < 0.01f && fabsf(a[2] - 8.f) < 0.01f);
    CHECK(fabsf(m[0]) < 0.01f && fabsf(m[1] - 1.f) < 0.01f && fabsf(m[2] - 7.f) < 0.01f);
    // 4-note qualities put their defining tone in slot 2 so it survives at poly 4
    const uint8_t dim7 = kVargToVoicing[VQ_DIM7];
    const uint8_t m7b5 = kVargToVoicing[VQ_M7B5];
    const uint8_t add9 = kVargToVoicing[VQ_ADD9];
    CHECK(fabsf(ChordVoicing(4, dim7)[2] - 6.f) < 0.01f);     // b5
    CHECK(fabsf(ChordVoicing(4, m7b5)[2] - 6.f) < 0.01f);     // b5
    CHECK(fabsf(ChordVoicing(4, add9)[2] - 7.f) < 0.01f);     // 5 of the m triad
    // polyphony 1 has room for all four tones
    CHECK(ChordSize(1) == 8); CHECK(ChordSize(2) == 6); CHECK(ChordSize(4) == 3);
    for (int p = 1; p <= 4; ++p)
        for (uint8_t q = 0; q < VQ_COUNT; ++q)
            CHECK(ChordVoicing(p, kVargToVoicing[q]) != nullptr);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `tests/host/run_tests.sh`
Expected: compile failure — `kNumChords` is 11 and `ChordVoicing` is undeclared.

- [ ] **Step 3: Add the voicings and the test seam**

In the submodule: bump `kNumChords` from 11 to 17 in `rings/dsp/performance_state.h`. Append six rows to the `BRYAN_CHORDS` `chords[kMaxStringSynthPolyphony][kNumChords][kMaxChordSize]` table in `string_synth_part.cc`, in this order (indices 11..16): `5b5` `{0,6,12,18,...}`, `dim7` `{0,3,6,9,12,15,18,21}`, `m7b5` `{0,3,6,10,12,15,18,22}`, `aug` `{0,4,8,12,16,20,24,28}`, `add9` `{0,3,7,14,12,15,19,26}`, `m+b9` `{0,1,7,12,13,19,24,25}`. Keep each row's lower-octave root at `-12` where the existing Bryan rows use it; fill the remaining slots with octave doublings of the identity tones. Define `ChordVoicing(int,int)` and `ChordSize(int)` in `string_synth_part.cc` and declare them in `string_synth_part.h`, both in `namespace rings`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `tests/host/run_tests.sh`
Expected: all tests PASS.

- [ ] **Step 5: Commit in the submodule, then update the parent pointer**

```bash
cd components/mutable/eurorack
git add rings/dsp/performance_state.h rings/dsp/string_synth_part.cc rings/dsp/string_synth_part.h
git commit -m "feat(rings): guitar voicings for VARG block chords"
cd -
git add components/mutable/eurorack
git commit -m "chore: update eurorack submodule for VARG block voicings"
```

---

### Task 5: TBDings decodes `vargChord`

**Files:**
- Modify: `components/ctagSoundProcessor/ctagSoundProcessorTBDings.hpp` (atomic fields in the `sectionHpp` block)
- Modify: `components/ctagSoundProcessor/ctagSoundProcessorTBDings.cpp` (`knowYourself` maps and `updateParams` decode)
- Modify: `spiffs_image/data/sp/mui-TBDings.jsn` (new param entry with `def`)
- Test: `tests/host/test_varg_core.cpp` (decode-helper assertions only; no DSP build in host tests)

**Interfaces:**
- Consumes: `DecodeChord`, `kVargToVoicing` from Task 1; `kNumChords = 17` from Task 4.
- Produces: `vargChord` param (0..1) with CV map; when its CV is mapped, `performance_state.note` gains the decoded root offset and `performance_state.chord` is set from `kVargToVoicing[qual]`.

- [ ] **Step 1: Write the decode guard test**

Append to `tests/host/test_varg_core.cpp`:

```cpp
TEST(varg_block_decode_applies_root_and_voicing) {
    // Mirrors the exact arithmetic updateParams will use.
    for (uint8_t off = 0; off < 12; ++off)
        for (uint8_t q = 0; q < VQ_COUNT; ++q) {
            const uint16_t idx = EncodeChord(off, q);
            const float cv = (float)idx / 108.0f;
            const int decoded = std::min(107, std::max(0, (int)lroundf(cv * 108.0f)));
            uint8_t dOff = 0, dQual = 0;
            DecodeChord((uint16_t)decoded, dOff, dQual);
            CHECK(dOff == off);
            CHECK(dQual == q);
            CHECK(kVargToVoicing[dQual] < 17);
        }
}
```

- [ ] **Step 2: Run the tests to confirm the guard passes**

Run: `tests/host/run_tests.sh`
Expected: PASS — the identical arithmetic was proven in Task 1; this test guards against later divergence. The DSP-side wiring is verified in Task 6.

- [ ] **Step 3: Add the param and the decode**

In `ctagSoundProcessorTBDings.hpp`, inside the `sectionHpp` block add `atomic<int32_t> vargChord{0}, cv_vargChord{-1};`. In `ctagSoundProcessorTBDings.cpp`, add the two `knowYourself` map entries for `"vargChord"`, and in `updateParams` after the existing `cv_chords` block add:

```cpp
if (cv_vargChord != -1) {
    const int idx = (int)lroundf(fabsf(data.cv[cv_vargChord]) * 108.f);
    uint8_t vOff = 0, vQual = 0;
    SEQ::DecodeChord((uint16_t)std::min(107, std::max(0, idx)), vOff, vQual);
    performance_state.note += vOff;
    performance_state.chord = SEQ::kVargToVoicing[vQual];
}
```

placed **before** the `CONSTRAIN(performance_state.note, 0.f, 96.f)` and `CONSTRAIN(performance_state.chord, ...)` calls, and after the manual `chords`/`cv_chords` assignment so the decoded voicing wins when both are mapped. Include `helpers/varg_chord.h`. No logging, no allocation.

In `spiffs_image/data/sp/mui-TBDings.jsn`, append a param object `{"id":"vargChord","name":"VARG Chord","type":"int","min":0,"max":1,"def":0}` to the `params` array. Do not edit `mp-TBDings.jsn`.

- [ ] **Step 4: Run the tests and both firmware builds**

Run: `tests/host/run_tests.sh` (all PASS), then:
`source /home/schrab/esp/esp-idf-v6.1.1/export.sh && idf.py build`
`source /home/schrab/esp/esp-idf-v6.1.1/export.sh && idf.py -B build-bba2 -D IDF_TARGET=esp32s3 -D SDKCONFIG=build-bba2/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.bba2" build`
Expected: both builds succeed; A1S app ~3.4 MB, BBA2 ~1% partition free (no arena regression).

- [ ] **Step 5: Commit**

```bash
git add components/ctagSoundProcessor/ctagSoundProcessorTBDings.hpp components/ctagSoundProcessor/ctagSoundProcessorTBDings.cpp spiffs_image/data/sp/mui-TBDings.jsn tests/host/test_varg_core.cpp
git commit -m "feat(tbdings): decode VARG block chord CV into root + voicing"
```

---

### Task 6: Bench verification on the A1S

**Files:**
- Modify: `AGENTS.md` (record the verified setup and any new findings)
- No code changes expected.

**Interfaces:**
- Consumes: all of Tasks 1–5, flashed to the A1S and pushed to SPIFFS.

- [ ] **Step 1: Flash app-only and push the mui JSON**

Run the app-only esptool command from `AGENTS.md` (never plain `idf.py flash`), then back up the device TBDings model file and push the updated `mui-TBDings.jsn` with `uimcu/tools/spiffs_push_file.py` (Void-first, byte-exact read-back, reload-confirm). Expected: panel offers the `VARG Chord` param as a CV target.

- [ ] **Step 2: Verify algo 6 selection and the packed CV**

Through the C3 relay (`screenshot.py` in one console session): set ARP1 algo to 18/19/20 and read back via `getProxyState`/`GetArpAlgo`; confirm the panel row reads `VARG BLOCK n-OCT`. Map `SQ.A1` to `vargChord` and `SQ.A1 Gate` to `strum_int`. Expected: no rejected setter, no blank row.

- [ ] **Step 3: Verify block chords sound with the guitar mask**

Drive a TRM16 and a BURST rhythm, progression `i-bVII`, TRIAD quality, mutation 0. Confirm via `/debug/getAudioCapture {"blocks":1,"scale":8192,"vuHist":0}` and the MIX VU that each gate produces a multi-voice chord (multiple simultaneous peaks) rather than a single note, and that the chord changes once per bar. Sweep the quality param through DIM7/AUG/ADD9 and confirm the voicing changes audibly.

- [ ] **Step 4: Verify health and the unmapped-CV migration case**

Poll `/debug/getAudioStats`: `cpuOverload`, `i2sErrR/W`, `diagStallRd/Wr`, `muteBlocks` must stay at their pre-test values. Then leave `vargChord` unmapped and switch an old algo-5 patch to algo 6: confirm chords still sound (from the manual `chords` param) with a wandering root and no fault. Reload a preset saved before `vargChord` existed and edit it: the edit must stick.

- [ ] **Step 5: Commit the findings**

```bash
git add AGENTS.md
git commit -m "docs(AGENTS): VARG block chords — verified bench setup and findings"
```
