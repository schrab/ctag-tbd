# Elements Second-Core Offload — Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development.

**Goal:** Restore Elements' original 52-mode resonator by offloading `part.Process` to a core-0 helper task (one 32-sample block, one-block latency), mirroring the Mbeads second-core pattern.

**Design:** `designs/elements-second-core.md` (folded into `SPEC.md` as V5–V11, T2).

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

## Constraints
- Work on feat/mbeads branch.
- Audio budget 174,150 cycles.
- No heap in Process(); buffers allocated in Init().
- App-only flash at 0x10000; user approval per flash.
- User listening gate required.