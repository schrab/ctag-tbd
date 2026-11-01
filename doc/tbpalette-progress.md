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

## 2026-11-02 — Phase 1: vendor + fork patches — code complete, host-verified, NOT YET COMMITTED

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

