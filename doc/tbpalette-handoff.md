# TBPalette implementation handoff

**Date:** 2026-11-02 · **Branch:** `feat/ui-split` · **Plan:** `doc/tbpalette-plan.md` ·
**Progress log:** `doc/tbpalette-progress.md`

## Goal

New sound-processor plugin **`TBPalette`** exposing **all 98 models** of the Rubato Audio
Plaits fork (63 reworked MI models with true stereo + 4th macro, 22 Rubato Lab originals,
13 community engines), alongside the untouched TBDaits. Rubato fork: github.com/lylepmills/eurorack,
user's clone at `components/rubato` (gitignored, reference only).

## Where we are

| Phase | Status |
|---|---|
| 0 — host sizeof probe | **DONE** — gate PASSED: 98 engines sum 25,888 B; `sizeof(rplaits::Voice@98)` = **23,024 B**; largest engine SixOpEngine 2,088 B. Object + 32 KB scratch ≈ 56 KB of the 110,592 B STEREO arena → no fallback needed. |
| 1 — vendor Rubato plaits into build | **DONE in working tree, NOT YET COMMITTED** — host smoke test green (both trees compile/link/render; see below). |
| 2 — TBPalette plugin + JSON specs | not started |
| 3 — uimcu label pool for 98 names | not started |
| 4 — idf build + bench | not started |

## Uncommitted change set (commit as Phase 1)

- `components/mutable/rubato/` (NEW, vendored): `rplaits/` (renamed plaits tree, 4.1 MB),
  `alt_firmwares/plaits_lab_catalog/catalog.json` (+README), upstream `AGENTS.md`/`README.md`,
  and **`VENDOR_README.md` — read it first**, it lists every transformation applied.
- `tools/tbpalette/gen_engine_config.py` (NEW): regenerates
  `components/mutable/rubato/rplaits/dsp/engine_config.h` from the catalog. Config is
  **checked in, generated — never hand-edit**. Registry order = catalog order = smodel panel
  index (frozen; only append).
- `components/mutable/CMakeLists.txt`: plaits globs duplicated — `rubato/rplaits/dsp/**` +
  `eurorack/plaits/dsp/**` both compiled; `INCLUDE_DIRS eurorack rubato`.
- `.gitignore`: `/components/rubato/` ignored (clone stays untracked).
- `doc/tbpalette-plan.md`, `doc/tbpalette-progress.md` (committed, ed06912c).

## The coexistence design (critical to preserve)

TBDaits/WTOsc/WTOscDuo/Retroactor/Freakwaves/SpaceFX/VctrSnt/Formantor/Bjorklund/Rimshot
compile against the **eurorack** plaits tree and must not change. The vendored tree therefore
differs in three disjointness mechanisms (all mechanical, documented in VENDOR_README.md):

1. **Namespace** `plaits` → `rplaits` (all sources).
2. **Include prefix**: directory renamed `plaits/` → `rplaits/` and every internal
   `#include "plaits/..."` → `"rplaits/..."` — so no `-I` ordering can mix the trees.
3. **Include guards** `PLAITS_*_H_` → `RPLAITS_*_H_` — upstream guards are IDENTICAL to the
   eurorack tree's; without this the second tree's headers get silently skipped (this bit us).

Fork patches in the vendored tree: `dsp/dsp.h` retuned to 44.1 kHz + 32-sample blocks
(kBlockSize = kMaxBlockSize = 32); `build_config.h` engine-count ceiling 32 → 128;
`voice.cc` masks `1u <<` → `1ull <<` (13 sites). True stereo = `patch.aux_output_option == 1`
(+ `stereo_capable()`); 4th macro = `patch.freqlock_param` with `locked_frequency_pot_option = 1`.

Host proof (`/tmp/tbpalette_probe/host_smoke.cpp`, pattern to check into `tests/host` in
Phase 4): both `plaits::Voice` (24 engines) and `rplaits::Voice` (98 engines) init, render
8 blocks per model (engine switch every model), finite output, in ONE binary.
Run pattern: compile every `.cc` under both `dsp/` trees + both `resources.cc` +
`eurorack/stmlib/{utils/random,dsp/units,dsp/atan}.cc` with
`g++ -std=c++17 -O1 -ffast-math -DTEST -I components/mutable/eurorack -I components/mutable/rubato`.

## Hard-won gotchas from this session

- **ODR / stale-object trap**: `engine_config.h` (engine count, `kEngineUserDataBank` size,
  masks) is seen only by TUs that include `voice.h` — in practice ONLY `voice.cc`. A partial
  rebuild after changing the config (97 → 98 engines) mixed array layouts across TUs →
  segfault in `Voice::Init` that looked like a pool overflow. **Always full-recompile after
  regenerating engine_config.h.**
- The 16 KB upstream scratch is NOT the limit for lpc-speech (~14.5 KB allocs fit), but
  **TBPalette passes a 32 KB scratch** (`BufferAllocator` over the arena remainder) for
  headroom — upstream ships `PLAITS_HAS_LPC_WORDS_ENGINE 0` for STM32 RAM reasons that don't
  apply here (lpc word banks are baked in `lpc_speech_synth_words.cc`; natural-speech uses its
  baked `natural_speech_data.h`; formant-speech never dereferences a word bank — all 98 link).
- `user_data.ptr()` returns NULL (build defines `-DTEST`); six-op placements (slots **22/23/24**)
  fall back to `fm_patches_table[0..2]` via `kEngineUserDataBank`.
- `git add components/rubato` stages an embedded-repo **gitlink** (its `.git` exists) — the
  clone must stay untracked (gitignored); vendor copies live in `components/mutable/rubato`.

## Next steps

### Phase 2 — plugin (do next)
1. `components/ctagSoundProcessor/ctagSoundProcessorTBPalette.{hpp,cpp}`: copy the structure of
   `ctagSoundProcessorTBDaits.*`; include `"rplaits/dsp/voice.h"`; use `rplaits::`.
   - `Init(blockSize, blockPtr)`: **assert remainder ≥ 32768** and pass 32 KB to
     `rplaits::Voice::Init` (not 16 KB).
   - Params (superset of TBDaits's 11): `smodel` 0–97 (clamp!), `frequency`, `level`,
     `harmonics`, `timbre`, `morph`, **`macro`** (→ `patch.freqlock_param`, set
     `patch.locked_frequency_pot_option = 1`), `lpg_color`, `lpg_decay`, `trigger`,
     **`stereo`** bool (→ `aux_output_option` 1/0, default ON), mod group `mod_freq`,
     `mod_timbre`, `mod_morph`, `mod_macro`. Wire via `knowYourself()` pMapPar/pMapCv/pMapTrig;
     keep the cv −1 sentinel + active-low trigger conventions from TBDaits.
   - Render: `rplaits::Voice::Frame out[32]` → interleaved `data.buf` (same as TBDaits).
   - Factory picks it up automatically from the filename glob.
2. `spiffs_image/data/sp/mui-TBPalette.jsn` (id/isStereo true/name/hint, params incl. 98
   `texts`) + `mp-TBPalette.jsn` (Default preset). Regenerate the spiffs image.
3. Host smoke re-run + `idf.py build` (flash budget: ~914 KB app headroom before this work).

### Phase 3 — panel
- `uimcu/ui/DspProxy.hpp`/`DspProxyLink.cpp`: enum label pool is **32 labels × 15 chars** —
  must hold 98 names (propose 128 × 20; watch C3 static RAM). Names flow through the existing
  chunked param-spec pushes; no protocol change. Engine names are pure `texts` data.

### Phase 4 — verification
1. Host: check the smoke test into `tests/host/` (follow `run_tests.sh` pattern) incl. the
   sizeof probe.
2. `idf.py build`; boot log: TBPalette object size + arena fit; TBDings still loads after.
3. Bench (ONE `screenshot.py`-style console session per sweep — console open resets the C3):
   step all 98 models via `/debug/injectEvent`, watch `/debug/getLoopHist` (7 = normal) +
   `cpuOverload`; flag any model over the 174,150-cycle/32-sample budget. Verify true stereo,
   panel texts, preset save/recall, macro + mod mapping, no click regression (u/d counters).
4. Commit per phase; document the engine-index table in the commit (registry order never
   changes after release).

## Key file paths

- Vendored tree + provenance: `components/mutable/rubato/` (`VENDOR_README.md`)
- Engine config: `components/mutable/rubato/rplaits/dsp/engine_config.h` (generated)
- Generator: `tools/tbpalette/gen_engine_config.py`
- Build glue: `components/mutable/CMakeLists.txt`
- Reference wrapper: `components/ctagSoundProcessor/ctagSoundProcessorTBDaits.{hpp,cpp}`
- JSON spec examples: `spiffs_image/data/sp/mui-TBDaits.jsn`, `mp-TBDaits.jsn`
- Panel label pool: `uimcu/ui/DspProxy.hpp` / `DspProxyLink.cpp`
