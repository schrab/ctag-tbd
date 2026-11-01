# TBPalette progress log

Append-only log; newest entries at the bottom. One entry per milestone/commit.

---

## 2026-11-02 — Session start

- Plan saved to `doc/tbpalette-plan.md`. Target: new `TBPalette` plugin with all 98 Rubato
  Plaits models (true-stereo reworked stock + 22 Rubato Lab originals + 13 community), TBDaits
  untouched. Rubato clone at `components/rubato` (github.com/lylepmills/eurorack).
- Phase order: 0 host sizeof probe → 1 vendor into build → 2 plugin + JSON specs → 3 panel label
  pool → 4 build/bench.

## 2026-11-02 — Phase 0: sizeof probe — GATE PASSED

- Probe (host g++, layout-identical on Xtensa): all 98 catalog engines sum **25,888 B**;
  largest single engine `SixOpEngine` 2,088 B. `sizeof(rplaits::Voice@98)` = **23,024 B**.
- Verdict: TBPalette object + 32 KB scratch ≈ 56 KB vs the 110,592 B STEREO arena → embed
  all engines in one Voice; the placement-new fallback from the plan is NOT needed.

## 2026-11-02 — Phase 1: vendor + fork patches — committed

- Vendored `components/mutable/rubato/` from the user's clone: `rplaits/` (renamed plaits
  subtree, namespace **rplaits**), catalog.json, upstream AGENTS/README, `VENDOR_README.md`
  (transformation list). Clone itself gitignored (`/components/rubato/`).
- Coexistence with the eurorack plaits tree (TBDaits/WTOsc/Retroactor/... must not change):
  namespace rename + include-prefix rename (`plaits/`→`rplaits/`) + include-guard rename
  (`PLAITS_*_H_`→`RPLAITS_*_H_` — upstream guards were identical and suppressed headers).
- Fork patches: dsp.h → 44.1 kHz + 32-sample blocks; build_config.h ceiling 32→128; voice.cc
  `1u <<`→`1ull <<` (13 sites). `tools/tbpalette/gen_engine_config.py` generates
  `rplaits/dsp/engine_config.h`: **98 engines**, catalog order = smodel index, uint64 masks,
  six-op slots 22/23/24 → DX7 syx banks 0/1/2, `PLAITS_NATURAL_SPEECH_ENGINE_MEMBER` defined.
- All 98 engines included — no exclusions (lpc word banks are baked in
  lpc_speech_synth_words.cc; earlier exclusion was wrong; formant/natural speech self-contained).
- CMakeLists: both plaits trees compiled, `INCLUDE_DIRS eurorack rubato`.
- **Host smoke test green**: one binary links both trees; 98 + 24 engines swept, finite output.
- Gotchas hit: identical include guards (silent header suppression); ODR/stale-object trap
  after regenerating engine_config.h (mixed 97/98-engine TUs → segfault in Voice::Init that
  mimicked a scratch-pool overflow — always full-recompile after regen);
  `git add components/rubato` staged a gitlink (untracked clone, fixed).
- Full details: `doc/tbpalette-handoff.md`.

## 2026-11-02 — Phase 2 & 3: plugin, specs, panel label pool, host & idf builds green

- **Phase 2 (Plugin + JSON specs)**:
  - Created `components/ctagSoundProcessor/ctagSoundProcessorTBPalette.{hpp,cpp}`:
    wrapped `rplaits::Voice` with 32 KB scratch buffer (`stmlib::BufferAllocator`), stereo interleaved
    output, active-low trigger, `fastfabs` CV unipolar handling, clamped `smodel` [0..97].
  - Parameters: `smodel` (0..97), `frequency`, `level`, `harmonics`, `timbre`, `morph`, `macro`
    (mapped to `patch.freqlock_param` with `patch.locked_frequency_pot_option = 1`), `lpg_color`,
    `lpg_decay`, `trigger`, `stereo` bool (`patch.aux_output_option` 1/0, default 1 ON), and
    modulation group (`mod_freq`, `mod_timbre`, `mod_morph`, `mod_macro`).
  - Added `spiffs_image/data/sp/mui-TBPalette.jsn` (98 engine texts) and `mp-TBPalette.jsn` (default preset).
  - Fixed format specifiers in `rplaits/user_data.h` mock flash stubs for Xtensa toolchain format checking.
- **Phase 3 (Panel label pool)**:
  - Updated `uimcu/ui/DspProxyLink.cpp`: raised `labelPool` from 1024 to 2560 bytes per channel,
    increased max labels per param from 32 to 128, max string length from 15 to 20.
  - Widened `valBuf` in `uimcu/ui/menupages/UIMenuPageParams.cpp` from 16 to 24 chars to avoid display truncation.
- **Verification**:
  - Added `tests/host/test_tbpalette_smoke.cpp` and `tests/host/run_tbpalette_tests.sh`: sweeps all 98
    Rubato Plaits models and all 24 stock Plaits models in one binary with finite output checks. PASS.
  - `idf.py build` (DSP / ESP32-A1S): SUCCESS. Binary size 0x306ac0 (493 KB / 14% free in partition).
    LittleFS `storage.bin` packaged with TBPalette descriptors.
  - `idf.py build` (Panel / ESP32-C3): SUCCESS. Binary size 0xbed70 (2.3 MB / 75% free).


