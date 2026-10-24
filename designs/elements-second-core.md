# Elements Second-Core Offload

**Status: implemented; 52-mode goal not met on hardware — see Outcome.**

## Problem

Elements' modal resonator (originally 52 SVF modes) exceeds the 174,150-cycle audio-block budget on a single core. The port compromised to 12 modes (`voice.cc:73`, `set_resolution(12)`) and applies a 0.125 master gain to tame SoftLimit output. The result is audibly thinner than the original instrument. Goal: restore the original 52-mode resonator by moving the heavy DSP to core 0, mirroring the Mbeads second-core pattern, without audible dropouts or regressions.

## Proposal

Offload the entire `elements::Part::Process` call to a core-0 helper task, one full 32-sample block at a time, with a one-block output latency. The audio task (core 1) keeps codec I/O, ModEngine, glue, and applies the 0.125 gain.

### Shape

- **Helper task**: core 0, priority `configMAX_PRIORITIES - 2`, stack 4096, one task for the plugin's lifetime (same as Mbeads).
- **Double buffer**: 2 slots × (PerformanceState + blow_in[32] + strike_in[32] + main[32] + aux[32]). ~5×32×4 bytes ≈ 2.5 KB per slot, 5 KB total. Allocated in PSRAM at Init.
- **Handoff (sequence-checked, like Mbeads):**
  - Core 1, start of block: copy `perfState`, `blow_in`, `strike_in`, and a snapshot of `patch_` into the write slot; mark slot busy; notify helper.
  - Core 0: copy the patch snapshot into `part`, run `part.Process(perfState, blow_in, strike_in, main, aux, 32)`; store done-seq; mark slot free.
  - Core 1, next block: if the read slot's done-seq matches, mix `main`/`aux` (×0.125) into `out_`; else skip (no helper contribution this block) and count a miss.
- **Input snapshot**: Elements consumes `blow_in`/`strike_in` from the current block's stereo input. Core 1 captures these at block start (before processing) into the helper's input buffers. The helper never reads the live input.
- **Latency**: one block (~0.73 ms) on the resonator output. Acceptable for a resonator; the exciter attack is delayed by one block, inaudible in practice.
- **State ownership**: `part`, `patch`, `perfState` live in the plugin object (PSRAM). The helper mutates `part` (resonator state) — same object, accessed from core 0 only during render. Core 1 must not touch `part` while the helper runs; it writes only the patch snapshot into the helper block at block start (before queuing), so no mutex is needed.
- **Mode count**: restore `set_resolution(52)` (original). Keep 0.125 gain.
- **Reverb**: stays inside `Part::Process` on core 0 (no split needed; the resonator dominates).

### Debug / measurement

Reuse the Mbeads endpoints: `/debug/getAudioStats` extended with `helperCycles`, `helperOverload`, `helperStackHighWater`. Hardcode 52 modes and measure; if helperOverload or audio overload, step down to 32/24 and re-measure.

*As built:* Elements exposes distinct keys instead (`elementsHelperCycles`,
`elementsHelperMaxCycles`, `elementsHelperOverload`,
`elementsHelperStackHighWater`, `elementsHelperResolution`,
`elementsHelperMisses`, `elementsHelperModeOverride`), plus
`/debug/setElementsMode` to pin a mode count for measurement. Distinct keys keep
Mbeads and Elements stats from overwriting each other when both plugins are
loaded.

## Design decisions

- **Decision:** Patch snapshot into the helper block; helper copies it into `part` at render start. No mutex. **Why:** Core 1 never touches `part` during render; only the patch snapshot crosses cores. Avoids blocking and matches the Mbeads "no wait" invariant.
- **Decision:** One-block latency on the resonator output. **Why:** Simpler handoff, proven by Mbeads; 0.73 ms is inaudible for a resonator.
- **Decision:** Restore full 52 modes first, then step down if overloaded. **Why:** The goal is the original sound; measurement, not assumption, sets the final count.
- **Decision:** Keep reverb inside `Part::Process` on core 0. **Why:** Simpler; the resonator dominates cost.
- **Decision:** Fall back to single-core at 12 modes if helper task creation fails or helper misses deadlines. **Why:** Preserves current behavior as the safe baseline.

## Success criterion

- Elements renders at 52 modes (or highest stable count) with `helperOverload == 0` and `cpuOverload` within the existing conservative flag rate, at worst-case settings, over a 10-minute soak.
- No audible dropouts/crackle; user listening sign-off.
- Mbeads and Vintage unaffected (Elements is a separate plugin).

## Outcome (2026-10-23)

Implemented on `feat/mbeads`: helper offload (`e1b7f88c`), review fixes
(`06ad2d92`), hygiene (`43cc1398`), runtime resolution API with 52-mode default
(submodule `e6fbfd5`, `f217a53`), literal V9 handoff + stats (`121d71f3`), debug
mode override (`8b42cd97`). Design folded into SPEC.md as V5–V11 / T1–T2
earlier.

Measured on hardware (ESP32-D0WD-V3, 240 MHz, budget 174,150 cycles per
32-sample block; UART `/debug/getAudioStats`):

| Observation | Value |
|---|---|
| Helper at 52 modes | ~225k cycles/block typical; ~445k–555k worst-case → over budget |
| Step-down ladder | one-way 52 → 32 → 24 → 16 → 12, 8-block hysteresis per mode |
| Settled state | literal V9 handoff to single-core 12 modes ~25–30 ms after boot, ~40 missed blocks while settling |
| Runtime failures | none observed; no crash, watchdog or silent wedge |
| Audio after settle | single-core 12-mode profile that shipped before this work |

Consequence: on this hardware the second-core offload does **not** make 52 modes
viable — the helper itself is over the block budget at every attempted mode
count. The work still pays off as a safe automatic fallback: production audio
ends up on the proven single-core 12-mode path. The 52-mode success criterion is
not met; the 10-minute soak was not run (boot transients only). Listening
sign-off is pending user review.

## Out of scope

- Splitting individual modes across cores (not feasible without resonator rewrite).
- Changing the 0.125 gain or SoftLimit behavior.
- Modulation CV routing changes.

## Next

None — folded into `SPEC.md` as V5–V11 / T1–T2 and implemented; see Outcome.
Remaining open item is the user listening sign-off on the flashed firmware.