# Elements Second-Core Offload — Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development.

**Goal:** Restore Elements' original 52-mode resonator by offloading `part.Process` to a core-0 helper task (one 32-sample block, one-block latency), mirroring the Mbeads second-core pattern.

**Design:** `designs/elements-second-core.md` (folded into `SPEC.md` as V5–V11, T2).

## Task Status

| Task | Status | Commit |
|---|---|---|
| 0. Freeze baseline | done | 3879d043 |
| 1. Helper task + offload (proof of concept) | done | e1b7f88c, fixes 06ad2d92, 43cc1398 |
| 2. Restore 52 modes | done | submodule e6fbfd5, f217a53 |
| 3. Measurement + tuning | done (hardware measurement; auto step-down) | 121d71f3, 8b42cd97 |
| 4. Docs + final validation | done | 8478e65e, (this commit) |

The SDD controller records the authoritative completion state in its ledger
(`.superpowers/sdd/2026-10-22-elements-second-core/progress.md`); this table is
updated with final commits after Task 4.

## Tasks

### Task 0: Freeze baseline
- Commit current state. No code change.

### Task 1: Helper task + offload (proof of concept)
- Add `TaskHandle_t helper_task_`, double buffer (perfState, blow_in[32], strike_in[32], main[32], aux[32]) ×2, sequences.
- `StartHelper`/`StopHelper`/`HelperTask` (core 0, priority configMAX_PRIORITIES-2, stack 4096).
- In `Process()`: snapshot blow_in/strike_in/perfState/patch into write slot; notify helper; mix read slot's main/aux (×0.125) into out_ if ready.
- Helper: copy patch snapshot into part, call part.Process, store done-seq.
- Guard with #ifndef TBD_SIM; simulator falls back to single-core 12 modes.

### Task 2: Restore 52 modes
- Change `set_resolution(12)` → `set_resolution(52)` in voice.cc.
- Keep 0.125 gain.

### Task 3: Measurement + tuning
- Flash, run helper gate, measure helperCycles/cpuOverload.
- If overloaded, step down to 32/24 modes.

### Task 4: Docs + final validation
- Update AGENTS.md, commit.

## As-built outcome (2026-10-23)

- Task 2 shipped as a runtime API (`Voice::set_resolution()` /
  `Part::set_resolution()`, `Voice` default 52) instead of editing a hardcoded
  `set_resolution(12)` call, so the single-core fallback can select 12 at runtime.
- Task 3 measured on hardware: helper at 52 modes costs ~225k cycles/block
  typical and ~445k–555k worst-case (budget 174,150). The one-way ladder
  52 → 32 → 24 → 16 → 12 and the literal V9 single-core handoff engage on every
  boot and settle at single-core 12 modes within ~25–30 ms (~40 missed blocks
  while settling). 52 modes is not viable on this hardware; the automatic
  fallback is the stable configuration and matches the pre-existing baseline.
- Debug surface: `/debug/getAudioStats` reports the `elementsHelper*` fields;
  `/debug/setElementsMode{"value":12..56}` pins a mode count for measurement
  (`<= 0` restores the auto ladder).
- Hardware listening sign-off is still pending (user gate); no code change is
  expected from it.

## Constraints
- Work on feat/mbeads branch.
- Audio budget 174,150 cycles.
- No heap in Process(); buffers allocated in Init().
- App-only flash at 0x10000; user approval per flash.
- User listening gate required.