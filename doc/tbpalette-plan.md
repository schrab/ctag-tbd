# TBPalette — new plugin with all 98 Rubato Plaits models

**Status:** in progress · **Progress log:** `doc/tbpalette-progress.md`

## Context

The Rubato Audio eurorack fork (cloned at `components/rubato`, github.com/lylepmills/eurorack) is a
Plaits 1.2-based rewrite with a 98-model catalog: 63 reworked MI models (all stock models upgraded
with **true stereo** OUT/AUX, a 4th "TWIST" macro, runtime options), **22 Rubato Lab originals**
(gendy, scanned, pulsar, brass, shakers, helix, …), and 13 community engines. Instead of modifying
the proven TBDaits plugin (which would destabilize its presets/param indices), we add a **new,
separate `TBPalette` plugin** that includes all 98 models, keeping TBDaits untouched as fallback.

Exploration established the integration facts:

- Engine objects are all **embedded members** of `plaits::Voice` (`PLAITS_ENGINE_MEMBERS`),
  scratch-allocated once at `Voice::Init` from a shared 16 KB pool (rubato voice.cc:111), and
  engines are never re-Inited on model switch (`LoadUserData`+`Reset` only, voice.cc:382-488).
  Registry order = our `RegisterInstance` order = panel model index.
- `PLAITS_ENGINE_COUNT` is hard-capped at 32 (`build_config.h:36`) because voice.cc indexes
  `uint32_t` masks with `1u << engine_index` (kSpeechEngineMask, kLPCWordsEngineMask,
  kNaturalSpeechEngineMask, kChiptuneEngineMask, kWavetableEngineMask, kWaveTerrainEngineMask).
  Needs a uint64 patch for 98.
- True stereo = `patch.aux_output_option == 1` + `engine->stereo_capable()` → OUT=left/AUX=right
  (voice.cc:500-501). Our plugin wrapper already writes `Frame{out,aux}` to both channels.
- The 4th macro = `patch.locked_frequency_pot_option == 1 ? patch.freqlock_param : 0.5f`
  (voice.cc:510) — trivially exposed as a panel param.
- Factory registration is filename-globbed (`components/ctagSoundProcessor/CMakeLists.txt` →
  generated factory), engine names on the panel are pure `texts` data. TBDaits runs
  44.1 kHz/32-block; Rubato ships 48 kHz/12–24-block (`plaits/dsp/dsp.h`).
- Scratch arena: `ctagSPAllocator` gives a STEREO plugin the whole 110,592 B internal-SRAM arena;
  the plugin object + 16 KB scratch must fit → `sizeof(Voice@98)` ≤ ~94 KB. STM32's full-palette
  build suggests ~60–100 KB: borderline, hence the Phase 0 probe.

## Plan

### Phase 0 — Feasibility probe (host only, no hardware)
1. Host-compile the catalog engines from `components/rubato/plaits/dsp/{engine,engine2}` with
   plain g++ and print `sizeof` of a struct embedding all of them. Catalog list from
   `components/rubato/alt_firmwares/plaits_lab_catalog/catalog.json` (excludes retired
   `virtual-analog-dual`/`-crossfade`; note six-op registers 3× as 6-Op FM 1/2/3 with different
   user-data banks).
2. **Gate:** sizeof(engines) ≤ ~90 KB (arena 110,592 − 16 KB scratch − scaffolding/margin). If it
   fits → proceed as planned. If not → fallback: one-at-a-time engine pool (placement-new the
   selected engine from the scratch pool on model switch; requires adding `allocator->Free()` +
   re-Init to the switch path in voice.cc — a contained change since all access goes through
   `EngineRegistry`). Decide by the measured number.

### Phase 1 — Vendor Rubato plaits into the build
Files: `components/mutable/CMakeLists.txt` (only the `eurorack/plaits/*` glob lines), new
`components/rubato/plaits/dsp/tbpalette_engine_config.h`, small patches to
`components/rubato/plaits/{dsp/dsp.h,build_config.h,dsp/voice.cc}`.

1. Reference `components/rubato/plaits/**` instead of `eurorack/plaits/**` in
   `components/mutable/CMakeLists.txt` (other modules — rings/elements/clouds/braids/marbles/
   beads/tides2 — stay on `eurorack`). Pin `components/rubato` (submodule or committed clone)
   with the tested commit.
2. **stmlib reconciliation:** order include dirs so `eurorack` wins `stmlib/*` (one tree → no
   duplicate `stmlib::Random`/`units` symbols; rubato's stmlib is the same MI lineage). Gate:
   diff the trees ignoring CRLF; if rubato plaits needs headers eurorack's stmlib lacks, copy
   just those files into a shim dir.
3. Retune `components/rubato/plaits/dsp/dsp.h` to our clock: `kSampleRate = 44100.0f`,
   `PLAITS_CORRECTED_SAMPLE_RATE = 44100.0f` (→ `kCorrectedPitchOffset` computes to 0),
   `kMaxBlockSize = kBlockSize = 32` (matches BUF_SZ; `Modulations::frequency_audio[kMaxBlockSize]`
   scales automatically).
4. Raise the engine ceiling: `PLAITS_ENGINE_COUNT` → 98+; change the `1u <<` mask sites in
   voice.cc to `uint64_t`/`1ull <<`.
5. Write `tbpalette_engine_config.h` (modeled on `stock_engine_config.h` / the generated recipe
   configs): all engine includes, `PLAITS_ENGINE_MEMBERS` + `PLAITS_REGISTER_ENGINES` in the
   final model order (order frozen here = panel index order forever), uint64 masks,
   `kEngineUserDataBank[]` (six-op placements → syx banks 0/1/2), `PLAITS_HAS_USER_DATA_BANK 1`
   (`fm_patches_table` fallback works under the `TEST` define; `user_data.ptr()` returns NULL),
   `PLAITS_HAS_NATURAL_SPEECH_ENGINE`/`LPC_WORDS` set per what actually works without STM32
   user-data banks (exclude + document if non-functional).
6. Patch stereo flag: `PLAITS_STEREO_ALL` stays 1 (default; ~23 KB flash cost accepted). Keep
   `-DTEST`, `-ffast-math`, `-Os` compile flags as for the old plaits sources.

### Phase 2 — The TBPalette plugin
Files: `components/ctagSoundProcessor/ctagSoundProcessorTBPalette.{hpp,cpp}` (new; factory picks
it up from the filename), `spiffs_image/data/sp/mui-TBPalette.jsn` + `mp-TBPalette.jsn`.

1. Copy the structure of `ctagSoundProcessorTBDaits.{hpp,cpp}`: one `plaits::Voice`,
   `Init(blockSize, blockPtr)` asserts ≥16384 scratch, `BufferAllocator` over the arena remainder;
   same `Process` flow (`Frame out[32]` → interleaved `data.buf`). Keep all the mod-mapping
   lessons (active-low trigger, cv −1 sentinel, `fastfabs`).
2. Params (superset of TBDaits): `smodel` (0..97, `texts` = 98 model names), `frequency`,
   `level`, `harmonics`, `timbre`, `morph`, `macro` (NEW → `patch.freqlock_param` with
   `locked_frequency_pot_option=1`), `lpg_color`, `lpg_decay`, `trigger`, `stereo` (NEW bool →
   `aux_output_option` 1/0, default ON), mod group `mod_freq`/`mod_timbre`/`mod_morph`
   (+ `mod_macro`). All wired via `knowYourself()` pMapPar/pMapCv/pMapTrig so panel mod-mapping
   works out of the box.
3. JSON specs modeled on `mui-TBDaits.jsn` (isStereo true, hint) + a default preset in
   `mp-TBPalette.jsn`. Regen spiffs image.

### Phase 3 — Panel (uimcu) support for 98 model names
Files: `uimcu/ui/DspProxy.hpp` / `DspProxyLink.cpp`.

1. Raise the enum label pool (currently 32 labels × 15 chars) to hold 98 names (e.g. 128 × 20 —
   check the static-RAM impact on the no-PSRAM C3; names pushed through the existing chunked
   param-spec path, no protocol change).
2. Verify the Params page renders `texts[val-min]` for `smodel` from the new spec; long names may
   need the existing name-clipping path.

### Phase 4 — Build + bench verification
1. `idf.py build`: flash size vs 3.5 MB app partition (~914 KB headroom today; new engine code +
   LUTs — natural_speech_data 58.7 KB, ars_luts 17 KB, fm algorithms ~20 KB, syx banks — mostly
   --gc-sections-pruned where unregistered).
2. Boot log: `TBPalette` object size + arena fit (STEREO allocation type = whole 110.6 KB arena);
   confirm TBDings still loads after.
3. Host tests: extend `tests/host/run_tests.sh` pattern with a plaits smoke test — render a few
   blocks per model at 44.1 kHz/32-frame, no crash/non-finite output; keeps the sizeof probe as a
   checked-in test.
4. Bench (single `screenshot.py`-style console session per sweep; console open resets the C3):
   load TBPalette from the panel, step through all 98 models via `/debug/injectEvent`, watch
   `/debug/getLoopHist` pacing (7 = normal) and the `cpuOverload` counter per model — flag any
   model over the 174,150-cycle budget (document; true-stereo models render 2 passes so expect
   the worst there). Verify: true stereo on stock models, engine-name texts on the panel, preset
   save/recall, macro + mod mapping, and no click/overrun regression (u/d counters + loop hist
   per the AGENTS.md forensics order).
5. Commit with the engine-index table documented (registry order = `smodel` index = `texts`
   order; never reorder after release).

### Explicitly out of scope
- TBDaits modifications (stays as-is), exposing per-model extra params beyond the 4 macros,
  user-data bank transfer over the link (DX7 banks use built-in syx fallbacks).

## Verification summary
Host sizeof/smoke tests → `idf.py build` (flash %) → boot log arena check → bench model sweep
(pacing + CPU + audio) → panel texts/presets/mod-mapping drive via one console session.
