# Mbeads Second-Core Dense Grain Cloud — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Mbeads render a genuinely dense Digital granular cloud of at least 16 simultaneous grains — 20 if the helper has real headroom — without exceeding the 174,150-cycle audio-block budget, by splitting grain rendering between the core-1 audio task and a core-0 helper task.

**Architecture:** Keep the existing single-core `beads::Processor` path and add a second, independent `DelayGranularPlayer` to `beads::Processor`. The audio task keeps codec I/O, delay-line writes, the trigger generator, reverb, and the core player's grains; the helper task owns the second player. The trigger generator's grains are split deterministically between the two players. The helper renders one complete 32-sample block from a start-of-block delay-line write-pointer snapshot into a double buffer; the audio task mixes that block, one block later, into the wet bus before the DC blocker, dry/wet, reverb and feedback. Both players normalize amplitude and pan spread against the same total active-grain count. Digital is the only media that uses the helper; Vintage stays single-core. A missed helper deadline skips the helper block rather than blocking the audio task, and a failed helper-task creation falls back to the safe single-core cap.

**Tech Stack:** C++17; ESP-IDF v5.5.4; FreeRTOS dual-core on ESP32-D0WD-V3; Mutable Instruments Beads DSP; host `g++` tests; Python debug-UART scripts; ESP32-A1S (BBA) hardware.

## Spec

- `doc/mbeads-port.md` — current port, CPU-budget and validation spec; §11 is the measured one-core baseline this plan extends.
- `doc/mbeads-implementation-plan.md` — the completed v1 task-level plan; this plan is the v1.1 continuation.
- The **Recon Baseline**, **Architecture** and task interfaces below are the design source for the second-core path. The raw recon log is `/tmp/opencode/mbeads-grain-spike.md` when present.
- User acceptance: the Digital cloud must sound dense (at least 16 grains; 8 was rejected), the Vintage mode must not regress, and the user must sign off after listening.

## Global Constraints

- Work in `/home/schrab/Projects/ctag-tbd` on the existing `feat/mbeads` branch. Do **not** create a fresh worktree from `main`: the uncommitted recon baseline in this tree is the starting state. The SDD controller uses the current tree as its isolated feature workspace.
- The audio block budget is **174,150 cycles** per 32-sample block at 44.1 kHz / 240 MHz. Never raise it; never hide a core-1 overrun by moving work into the measured section.
- Core 1 keeps codec I/O, delay-line writes, trigger generation, reverb, dry/wet/feedback, and its share of grains. The helper owns only the second player's grain rendering.
- Helper: core 0, priority `configMAX_PRIORITIES - 2` (raise to `- 1` only if Task 3/4 proves Wi-Fi/UI contention), stack 4096 bytes, one task for the plugin's lifetime.
- The helper must never block the audio task. The audio task mixes only a completed block (sequence-checked); a late helper block is skipped and counted, never waited for.
- The helper reads the Digital delay line only, with a start-of-block write-pointer snapshot. It never reads the 32 samples being written by core 1.
- The helper output is one block (≈0.73 ms) late and is added as extra wet **inside** the wet path, before the DC blocker/dry-wet/reverb, so it shares feedback and FX.
- Target split: Digital total **16** = **7 core / 9 helper** with or without reverb. First hardware gate is **8 / 8** dry. Optional **20** = **7 / 13** dry or **5 / 15** with reverb, only if stable.
- Vintage stays single-core: **16 dry / 8 with reverb**, helper cap 0. Do not move Vintage grains to the helper.
- Delay mode stays disabled in v1; the helper path must be inert when the trigger generator has no triggers.
- Keep `-O2` on `components/mutable/eurorack/beads/dsp/processor.cc`. Do not change optimisation flags per task.
- No heap allocation, VLA, blocking wait, or file I/O in `Process()`. The helper task, its stack and its double buffers are created/allocated in `Init()`.
- Beads DSP changes are committed **inside** `components/mutable/eurorack` first, then the parent repo stages only the updated gitlink. Never stage the submodule's nested dirty repos (`avrlib`, `avrlibx`, `stm_audio_bootloader`, `stmlib`, etc.).
- Never stage or commit unrelated dirty paths: `ctag-bba/`, `temp_a1s.txt`, `doc/*.pdf`, `doc/*.jpg`, `components/moog/MoogLadders`, `simulator/Simple-Web-Server`, `simulator/rtaudio`, or any unrelated submodule dirt.
- Host tests must pass: `tests/host/run_mbeads_tests.sh` and `tests/host/run_tests.sh`.
- Build with `/home/schrab/esp/esp-idf-v5.5.4/export.sh`. Flash **app only** at `0x10000`; never flash storage at `0x710000` or the bootloader/partition table.
- Ask the user for explicit approval before each hardware flash. The user must perform the listening gates.
- No push, merge or release without explicit user approval. Leave the branch and commits for the user's final review.
- Keep the existing `/debug/setGrainCap`, `/debug/setActivePlugin` and `/debug/getAudioStats` HTTP endpoints working for the bring-up scripts; add `/debug/setHelperGrainCap`.
- Firmware-only code (FreeRTOS task, `esp_cpu_get_cycle_count`, `TaskHandle_t`) is guarded with `#ifndef TBD_SIM`; the simulator falls back to the no-helper path.

## Review Focus

- **Core-0 deadline under UI/Wi-Fi load.** With the OLED UI, UART debug or Wi-Fi active, the helper must finish its 32-sample block within 174,150 cycles; a late block must be skipped, not stutter the audio. Owner: Task 3 (`StartHelper`, `helperOverload`), Task 5 (10-minute soak). Evidence: `helperCycles`, `helperOverload`, `cpuOverload`, listening.
- **Plugin unload and media-switch lifetime.** If Mbeads is unloaded, reloaded, or switched to Vintage while the helper is rendering, the helper must stop before `Processor`/PSRAM buffers are freed, and no stale helper block may be mixed. Owner: Task 3 (`StopHelper`, reset flag), Task 5 (load/unload test).
- **Split level and stereo image.** Two players normalizing only by their own active counts would be ≈3 dB loud and have a narrower pan spread. The shared external active count must keep the 7/9 cloud at the single-16-grain level and width. Owner: Task 2 (host RMS comparison), Task 4 (listening).
- **Stale write-pointer reads.** The helper must never read the 32 samples currently being written or wrap past the head; a wrong snapshot causes clicks or silence. Owner: Task 1 (`ReadFrom` exact test), Task 2 (helper-block test).
- **DRAM and task-creation failure.** A 4 KB helper stack plus TCB must fit after audio init; if `xTaskCreatePinnedToCore` fails, Mbeads must fall back to the safe single-core cap, not assert or go silent. Owner: Task 3 (`StartHelper` return, `helperActiveGrains == -1`), Task 5 (boot/load test).

## Recon Baseline (2026-10-22)

All values are Digital 44.1 kHz / Hermite unless marked, worst-case **Size 4095 / Density 4095**, 3 × 20 blocks after warm-up, and use the existing spike build with `-O2` on `processor.cc`.

| Case | Measured core cycles | Notes |
|---|---:|---|
| Fixed cost (no grains) | ~60,000 | delay-line, trigger generator, glue, core fixed path |
| Marginal cost per Digital grain | ~9,600 Hermite / ~7,100 linear | used for split arithmetic |
| Digital dry, cap 8 | 143,800 avg | 23% overload flags (conservative flag) |
| Digital dry, cap 12 | 178,700 | over budget |
| Digital dry, cap 16 | 218,200 | ≈44,000 over budget |
| Digital + reverb 4095, cap 8 | 169,200 | over budget for 8/8 with reverb |
| Digital + reverb 4095, cap 16 | 240,400 | far over budget |
| Digital linear, cap 8 / 12 / 16 | 126,200 / 155,700 / 192,100 | fallback measurement, not the target voicing |
| Vintage dry, cap 16 | 171,500 Hermite / 137,500 linear | at the one-core ceiling |
| Vintage + reverb, cap 8 | 164,100 | safe one-core ceiling |
| `sizeof(beads::Processor)` | 34,656 bytes | host guard currently 40 KB; raise to 48 KB for the helper player |
| App image / free in app partition | 3,254,304 / 415,712 bytes | current spike build |
| Audio-task stack high-water | 1,748 bytes free (Vintage max Size) | existing v1 measurement |

Derived split arithmetic:

| Split | Core-1 estimate | Helper estimate | Use |
|---|---:|---:|---|
| 8 / 8 dry | ~137,000 | ~77,000 | first hardware gate |
| 7 / 9 with reverb | ~161,000 | ~86,000 | production Digital |
| 5 / 15 with reverb | ~150,000 | ~144,000 | optional 20 grains |
| 7 / 13 dry | ~127,000 | ~125,000 | optional 20 dry |

Current device state at plan time: `-O2` Hermite spike, **Vintage cap 16, dry, max Size/Density**, 15 active grains, 151,700 cycles, no overload; temporary debug hooks are compiled in. The first Task 3 flash replaces this image. The current app image has 415,712 bytes free; the helper player and task code must stay within it.

## File Structure

| File | Responsibility |
|---|---|
| `components/mutable/eurorack/beads/dsp/delay_line.h` | Add snapshot (`ReadFrom`) read path; `Read` delegates to it. |
| `components/mutable/eurorack/beads/dsp/delay_grain.h` | Accept the fixed write pointer; render via `ReadFrom`; remove the linear recon macro. |
| `components/mutable/eurorack/beads/dsp/delay_granular_player.h` | Pass the write pointer through to each grain. |
| `components/mutable/eurorack/beads/dsp/granular_player.h` | External active-grain count for split amplitude/pan; production cap-0 semantics. |
| `components/mutable/eurorack/beads/dsp/granular_processor.h` | Expose delay lines; external count; add extra wet to the wet bus. |
| `components/mutable/eurorack/beads/dsp/processor.h` | `GrainHelperBlock`, helper player, split/caps, helper reset flag, render/active APIs. |
| `components/mutable/eurorack/beads/dsp/processor.cc` | Fill helper block, split triggers, pass extra wet, run helper player. |
| `components/ctagSoundProcessor/ctagSoundProcessorMbeads.hpp` | Helper task handle, double buffers, sequences, stats, task method declarations. |
| `components/ctagSoundProcessor/ctagSoundProcessorMbeads.cpp` | Helper task lifecycle, block handoff, cap policy, stats, fallback. |
| `main/DebugUI.cpp` | Helper/cap stats fields and `/debug/setHelperGrainCap`. |
| `tests/host/mbeads_core_test.cpp` | Snapshot-read exactness, split ratio, 16-grain host cloud, RMS comparison, `sizeof` bound. |
| `components/mutable/CMakeLists.txt` | Keep `-O2` on `processor.cc`; remove the recon linear block. |
| `doc/mbeads-port.md` | §11 second-core architecture and measured results. |
| `doc/mbeads-implementation-plan.md` | v1.1 status row and plan pointer. |
| `docs/superpowers/plans/2026-10-22-mbeads-second-core-dense-cloud.md` | This plan. |

No new source files are introduced: the helper task lives in the existing Mbeads plugin; the helper DSP lives in the existing Beads `Processor`. All new plugin state is member data, not per-block heap.

## Task Status

| Task | Status | Commit |
|---|---|---|
| 0. Freeze the recon baseline | done | 4b2f1caf |
| 1. Snapshot delay-line read + write-pointer render | done | 22a56495 |
| 2. Helper player, split, extra-wet path | done | 01c8e2d8 |
| 3. Core-0 helper task + 8/8 hardware gate | done | 93b5587b |
| 4. Production 7/9 split (16 grains) + reverb + listening | done (adaptive 9/8) | 1d410afe |
| 5. 20-grain tuning + cleanup + docs + final validation | done | (this commit) |

The SDD controller records the authoritative completion state in its ledger; this table is updated with final commits at the end of Task 5.

---

### Task 0: Freeze the recon baseline

This task introduces no behaviour. It turns the uncommitted CPU-spike state into two reviewable baseline commits — one inside the Beads submodule, one in the parent repo — so every later task diff is clean. The SDD controller's `BASE` is this commit.

**Files:**
- Beads submodule: `beads/dsp/delay_grain.h`, `beads/dsp/granular_player.h`
- Parent: `components/ctagSoundProcessor/ctagSoundProcessorMbeads.cpp`, `components/mutable/CMakeLists.txt`, `main/DebugUI.cpp`, `components/mutable/eurorack` (gitlink), `docs/superpowers/plans/2026-10-22-mbeads-second-core-dense-cloud.md`

**Interfaces:**
- Consumes: nothing.
- Produces: baseline commits and a tree whose only remaining dirt is explicitly unrelated.

- [ ] **Step 1: Verify the current tree and the host baseline**

Run from `/home/schrab/Projects/ctag-tbd`:

```bash
git branch --show-current
git log -1 --oneline
git -C components/mutable/eurorack status --short
git -C components/mutable/eurorack log -1 --oneline
bash tests/host/run_mbeads_tests.sh
bash tests/host/run_tests.sh
```

Expected: branch `feat/mbeads`; parent HEAD `3cdff3b8`; submodule on `upstream-beads` at `8460b50`; submodule dirty only in `beads/dsp/delay_grain.h` and `beads/dsp/granular_player.h` plus unrelated nested submodules (`avrlib`, `avrlibx`, `stm_audio_bootloader`, `stmlib`); both host suites PASS. If the two Beads DSP files are not the only non-nested changes, stop and report before committing.

- [ ] **Step 2: Commit the Beads recon hooks inside the submodule**

Stage only the two Beads files:

```bash
git -C components/mutable/eurorack add beads/dsp/delay_grain.h beads/dsp/granular_player.h
git -C components/mutable/eurorack diff --cached --name-only
```

Expected output is exactly:

```
beads/dsp/delay_grain.h
beads/dsp/granular_player.h
```

Then commit:

```bash
git -C components/mutable/eurorack commit -m "beads: keep recon grain-cap and interpolation hooks"
git -C components/mutable/eurorack log -1 --oneline
```

Do not stage `avrlib`, `avrlibx`, `stm_audio_bootloader`, `stmlib`, or any other nested submodule dirt.

- [ ] **Step 3: Commit the parent spike baseline and the plan**

Stage only the Mbeads spike paths, the updated submodule pointer and this plan:

```bash
git add components/ctagSoundProcessor/ctagSoundProcessorMbeads.cpp \
        components/mutable/CMakeLists.txt \
        main/DebugUI.cpp \
        components/mutable/eurorack \
        docs/superpowers/plans/2026-10-22-mbeads-second-core-dense-cloud.md
git diff --cached --name-only
```

Expected exactly:

```
components/ctagSoundProcessor/ctagSoundProcessorMbeads.cpp
components/mutable/CMakeLists.txt
components/mutable/eurorack
docs/superpowers/plans/2026-10-22-mbeads-second-core-dense-cloud.md
main/DebugUI.cpp
```

Commit:

```bash
git commit -m "chore(mbeads): freeze second-core recon baseline"
git show --stat --oneline HEAD
```

- [ ] **Step 4: Confirm the baseline**

Run:

```bash
git status --short
git -C components/mutable/eurorack status --short
```

Expected: the only remaining parent entries are the known unrelated paths (`ctag-bba/`, `temp_a1s.txt`, `doc/*.pdf`, `doc/*.jpg`, `components/moog/MoogLadders`, `simulator/Simple-Web-Server`, `simulator/rtaudio`); the only submodule entries are the pre-existing nested dirt. The Mbeads spike files and the plan are committed. Do not clean or commit the unrelated paths.

**Review note:** The baseline intentionally contains temporary "TEMP CPU-SPIKE" comments and the linear interpolation macro. Later tasks remove or productionise them. Do not "fix" them in Task 0.

---

### Task 1: Snapshot delay-line read + write-pointer grain render

Add a public `DelayLine::ReadFrom` that accepts an explicit write pointer, and thread an optional fixed write pointer through `DelayGrain` and `DelayGranularPlayer`. This is the read-side primitive the helper needs. Remove the linear-interpolation recon macro at the same time: the production voicing is Hermite and the helper must not depend on a measurement switch.

**Files:**
- Modify Beads submodule: `beads/dsp/delay_line.h`, `beads/dsp/delay_grain.h`, `beads/dsp/delay_granular_player.h`
- Modify parent: `tests/host/mbeads_core_test.cpp`, `components/mutable/CMakeLists.txt`, `components/mutable/eurorack` (gitlink after submodule commit)

**Interfaces:**
- Consumes: existing `DelayLine::Read`, `DelayLine::mutable_write_ptr()`, `DelayGrain::Render`, `DelayGranularPlayer::Render`.
- Produces:
  - `template<Interpolation interpolation> FloatFrame DelayLine::ReadFrom(int write_ptr, int delay_integral, float delay_fractional) const;`
  - `template<Interpolation interpolation> FloatFrame DelayLine::Read(int delay_integral, float delay_fractional) const;` delegates to `ReadFrom(*mutable_write_ptr(), ...)`.
  - `template<HeadCrossingPolicy head_crossing_policy, bool fade_out, typename DelayLineType> void DelayGrain::Render(const DelayLineType* delay_line, FloatFrame* in_out, int write_ptr = -1);`
  - `template<HeadCrossingPolicy head_crossing_policy, bool fade_out, typename DelayLineType> void DelayGranularPlayer::Render(const DelayLineType* delay_line, const TriggerGenerator::Output& trigger, const Parameters& parameters, FloatFrame* out, int write_ptr = -1);`
  - The production interpolation constant is `INTERPOLATION_HERMITE`. The `BEADS_SPIKE_GRAIN_INTERPOLATION` and `BEADS_SPIKE_LINEAR_INTERPOLATION` macros, and the commented linear block in `components/mutable/CMakeLists.txt`, are gone.

- [ ] **Step 1: Write the failing snapshot-read test**

Add `#include "beads/dsp/delay_line.h"` to `tests/host/mbeads_core_test.cpp`. Add this function above `main()`:

```cpp
bool TestSnapshotReads(uint8_t* buffers[]) {
  static const int kMaxDelay = 1024;
  beads::DelayLine<beads::RESOLUTION_16_BIT, kMaxDelay, 2> line;
  line.Init(buffers);

  for (int i = 0; i < 900; ++i) {
    beads::FloatFrame frame;
    frame.l = std::sin(kTwoPi * static_cast<double>(i) / 441.0);
    frame.r = std::cos(kTwoPi * static_cast<double>(i) / 337.0);
    line.Write(frame);
  }

  const int snapshot = *line.mutable_write_ptr();
  bool ok = true;
  for (int delay = 0; delay <= 700; delay += 37) {
    const float fraction = 0.25f;
    const beads::FloatFrame live =
        line.Read<beads::INTERPOLATION_HERMITE>(delay, fraction);
    const beads::FloatFrame from_snapshot =
        line.ReadFrom<beads::INTERPOLATION_HERMITE>(
            snapshot, delay, fraction);
    ok &= Expect(live.l == from_snapshot.l && live.r == from_snapshot.r,
                 "snapshot read matches live read");

    const beads::FloatFrame shifted =
        line.ReadFrom<beads::INTERPOLATION_HERMITE>(
            (snapshot + 1) % kMaxDelay, delay, fraction);
    const beads::FloatFrame delayed =
        line.ReadFrom<beads::INTERPOLATION_HERMITE>(
            snapshot, delay + 1, fraction);
    ok &= Expect(shifted.l == delayed.l && shifted.r == delayed.r,
                 "write pointer +1 is identical to one extra sample of delay");
  }
  return ok;
}
```

Call it in `main()` after the delay/reverb buffers are allocated and zeroed:

```cpp
  ok &= TestSnapshotReads(buffers);
```

- [ ] **Step 2: Run it and verify the expected failure**

Run:

```bash
bash tests/host/run_mbeads_tests.sh
```

Expected: compile failure, `DelayLine<...>` has no member `ReadFrom`. Stop and confirm the failure is the missing API, not an unrelated test error.

- [ ] **Step 3: Implement `DelayLine::ReadFrom`**

In `delay_line.h`, add the public overload **before** the existing `Read(int, float)`:

```cpp
  template<Interpolation interpolation>
  inline FloatFrame ReadFrom(
      int write_ptr, int delay_integral, float delay_fractional) const {
    if (interpolation == INTERPOLATION_HERMITE) {
      delay_integral += max_delay - 1;  // Fix latency.
    }
    int i = (write_ptr + delay_integral) % max_delay;
    // ... existing ZOH / LINEAR / HERMITE body, unchanged, using i and
    // delay_fractional ...
  }
```

Replace the body of the existing `Read(int delay_integral, float delay_fractional)` with:

```cpp
    return ReadFrom<interpolation>(
        *mutable_write_ptr(), delay_integral, delay_fractional);
```

Keep the `Read(int32_t delay)` overload unchanged: it splits the Q10 delay and calls `Read(delay_integral, delay_fractional)`.

Exactness requirement: `ReadFrom(snapshot + 1, d, f)` must produce bit-identical values to `ReadFrom(snapshot, d + 1, f)` for every interpolation. Do not add rounding, clamping or an extra modulo to one path only.

- [ ] **Step 4: Thread the optional write pointer through the grains**

In `delay_grain.h`:

- Change the render signature to add `int write_ptr = -1` as the last parameter.
- In every delay-line read inside the grain body, use the fixed pointer when supplied:
  - `write_ptr < 0`: keep the existing `delay_line->Read<...>(...)` calls.
  - `write_ptr >= 0`: use `delay_line->ReadFrom<...>(write_ptr, delay_integral, delay_fractional)`.
- The grain's `head_crossing_policy` crossfade reads and the main read must both use the same pointer.
- Remove the `BEADS_SPIKE_GRAIN_INTERPOLATION` / `BEADS_SPIKE_LINEAR_INTERPOLATION` macro block and use `INTERPOLATION_HERMITE` directly. Delete the "TEMPORARY CPU-SPIKE: linear" comment.

In `delay_granular_player.h`, add `int write_ptr = -1` to `Render` and pass it to every `grain_[i].Render(...)` / `grain_priority_list_[...]->Render(...)` call.

Keep the default argument so the existing one-core path and `WasteCPUCycles` compile unchanged.

- [ ] **Step 5: Remove the parent CMake recon block**

In `components/mutable/CMakeLists.txt`:

- Keep the `set_source_files_properties(eurorack/beads/dsp/processor.cc PROPERTIES COMPILE_OPTIONS "-O2")` property.
- Replace the "TEMPORARY CPU-SPIKE" comment with: "The grain renderer is inlined into processor.cc; -O2 is required to stay inside the 174,150-cycle audio budget. Keep this after -Os."
- Delete the commented "TEMPORARY CPU-SPIKE: linear-interpolation" block entirely.

- [ ] **Step 6: Run the test and the full host suites**

Run:

```bash
bash tests/host/run_mbeads_tests.sh
bash tests/host/run_tests.sh
```

Expected: both PASS. The new snapshot test reports no failures; existing Digital/Vintage/Clean/Dirty, half-rate and cap tests still pass.

- [ ] **Step 7: Commit the submodule and the parent**

Inside the submodule:

```bash
git -C components/mutable/eurorack add beads/dsp/delay_line.h \
        beads/dsp/delay_grain.h beads/dsp/delay_granular_player.h
git -C components/mutable/eurorack commit -m "beads: add snapshot delay-line reads"
```

In the parent:

```bash
git add tests/host/mbeads_core_test.cpp components/mutable/CMakeLists.txt \
        components/mutable/eurorack
git commit -m "test(mbeads): pin snapshot delay-line reads"
```

**Review note:** The parent diff shows only a gitlink for the Beads changes. The controller must include `git diff --submodule=diff` and `git -C components/mutable/eurorack diff <old>..<new>` in the review package.

---

### Task 2: Helper player, split, extra-wet path

Add the second `DelayGranularPlayer` to the Beads `Processor`, split the trigger stream deterministically, add the helper's previous block as extra wet inside the wet bus, and normalise both players against the shared total active count. This task is host-only; no FreeRTOS task yet. The split cloud must reach 16 active grains in the host test.

**Files:**
- Modify Beads submodule: `beads/dsp/granular_player.h`, `beads/dsp/granular_processor.h`, `beads/dsp/processor.h`, `beads/dsp/processor.cc`
- Modify parent: `tests/host/mbeads_core_test.cpp`, `components/mutable/eurorack` (gitlink after submodule commit)

**Interfaces:**
- Consumes Task 1: `DelayLine::ReadFrom`, `DelayGrain::Render(..., int write_ptr)`, `DelayGranularPlayer::Render(..., int write_ptr)`.
- Produces:

```cpp
// In beads::Processor, after kMaxBlockSize:
struct GrainHelperBlock {
  TriggerGenerator::Output triggers[kMaxBlockSize] = {};
  Parameters parameters = {};
  int write_ptr = 0;
  int core_active_grains = 0;
  size_t size = 0;
  bool mono = false;
  bool reset = false;
  bool valid = false;
};

void SetHelperMaxActiveGrains(int n);
void RenderHelperGrains(const GrainHelperBlock& block, FloatFrame* out);
int HelperActiveGrains() const;
static bool HelperOwnsTrigger(int counter, int total, int helper);

void Process(
    const uint8_t* seed_state,
    const stmlib::GateFlags* seed,
    const bool* freeze,
    const ModulatedParameter& density,
    const Parameters& parameters,
    const MixParameters& mix_parameters,
    const FloatFrame* in,
    FloatFrame* out,
    size_t size,
    GrainHelperBlock* helper_block = nullptr,
    const FloatFrame* extra_wet = nullptr);

// In beads::GranularProcessor:
void SetExternalActiveGrains(int n);
const DelayLine<Resolution, max_delay * 2, 1>* mono_delay_line() const;
const DelayLine<Resolution, max_delay, 2>* stereo_delay_line() const;
void Process(..., HalfRateScratch* scratch = nullptr,
             const FloatFrame* extra_wet = nullptr);

// In beads::GranularPlayer:
void SetExternalActiveGrains(int n);
```

- `Processor::SetMaxActiveGrains(n)` clamps `n` to `0..32`, stores `core_max_active_grains_`, and sets the four core players. `SetHelperMaxActiveGrains(n)` clamps `n` to `0..32` and stores `helper_max_active_grains_`; it must not touch the helper player from the audio-task thread.
- `Processor::HelperOwnsTrigger(counter, total, helper)` is `total > 0 && helper > 0 && (counter % total) < helper`.
- Production cap 0 is legal: `GranularPlayer::SetMaxActiveGrains` accepts `0` (fades existing grains out and starts none). `0` is used for the helper on Vintage and for the debug fixed-cost measurement. The old comment "production callers only pass 2 or 4" is replaced with "0 disables the player; the main player is at least 2 whenever granular is enabled in the plugin."
- `Processor::Process` fills the helper block **before** the core `GranularProcessor::Process` writes the delay line. It captures `*mutable_delay_write_ptr()` as the snapshot and `ActiveGrains()` as `core_active_grains`; then it moves helper-owned `triggers[i].trigger` from the core array into the helper block.
- `Processor::Configure` sets `helper_reset_pending_ = true`. `Processor::Process` copies and clears that flag into `helper_block->reset` only when the block is valid. `RenderHelperGrains` resets the helper player when `block.reset` is true.
- `GranularProcessor::ProcessInternal` adds `extra_wet[i]` to `out[i]` immediately after `granular_player_.Render(...)` and before the `ONE_POLE` DC blocker/feedback update. The half-rate branch passes `nullptr`; only Digital uses extra wet.
- `GranularPlayer::ComputeGrainAmplitudeAndPan` uses:

```cpp
const float num_active_grains =
    float(CountActiveGrains() + 1 + external_active_grains_);
```

for both amplitude and pan spread. `Reset()` sets `external_active_grains_ = 0`.

- `Processor` private members:

```cpp
DelayGranularPlayer helper_player_;
std::atomic<int> helper_active_grains_{0};
std::atomic<int> helper_max_active_grains_{0};
int core_max_active_grains_ = 0;
int helper_split_counter_ = 0;
bool helper_reset_pending_ = false;
```

- [ ] **Step 1: Write the failing split test**

Add to `tests/host/mbeads_core_test.cpp`:

```cpp
bool TestHelperSplitRatio() {
  bool ok = true;
  int to_helper = 0;
  for (int i = 0; i < 16; ++i) {
    if (beads::Processor::HelperOwnsTrigger(i, 16, 9)) ++to_helper;
  }
  ok &= Expect(to_helper == 9, "7/9 split sends 9 of 16 triggers to helper");
  ok &= Expect(!beads::Processor::HelperOwnsTrigger(0, 0, 0),
               "zero total sends no triggers");
  return ok;
}
```

Add a test-only accumulator to `BlockStats`:

```cpp
  void Add(const beads::FloatFrame* frames, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      finite &= IsFinite(frames[i].l) && IsFinite(frames[i].r);
      sum_squares += static_cast<double>(frames[i].l) * frames[i].l +
                     static_cast<double>(frames[i].r) * frames[i].r;
      samples += 2;
    }
  }
```

Add a split runner that exercises the real `Processor::Process` + `RenderHelperGrains` handoff. Use the existing `BlockStats` type and `RunBlocks` input conventions; the parameters are the same dense settings as the one-core tests:

```cpp
struct SplitRunResult {
  BlockStats stats;
  BlockStats helper_stats;
  int core_active = 0;
  int helper_active = 0;
};

SplitRunResult RunSplitCloud(
    beads::Processor* processor,
    uint8_t* buffers[],
    uint16_t* reverb_buffer,
    int core_cap,
    int helper_cap) {
  processor->Init(buffers, reverb_buffer, kHostSampleRate);
  processor->Configure(beads::Processor::MEDIA_DIGITAL, false);
  processor->SetMaxActiveGrains(core_cap);
  processor->SetHelperMaxActiveGrains(helper_cap);

  beads::Parameters parameters;
  parameters.time.base = 0.02f;
  parameters.time.attenuverter = 0.0f;
  parameters.time.has_cv = false;
  parameters.time.cv = 0.0f;
  parameters.size.base = 1.0f;
  parameters.size.attenuverter = 0.0f;
  parameters.size.has_cv = false;
  parameters.size.cv = 0.0f;
  parameters.shape.base = 0.5f;
  parameters.shape.attenuverter = 0.0f;
  parameters.shape.has_cv = false;
  parameters.shape.cv = 0.0f;
  parameters.pitch.base = 0.5f;
  parameters.pitch.attenuverter = 0.0f;
  parameters.pitch.has_cv = false;
  parameters.pitch.cv = 0.0f;
  parameters.monophonic_mode = false;

  beads::ModulatedParameter density;
  density.base = 0.0f;  // deterministic 130.81278 Hz trigger train
  density.cv = 0.0f;

  beads::Processor::MixParameters mix;
  mix.feedback = 0.0f;
  mix.dry_wet = 1.0f;
  mix.reverb_amount = 0.0f;
  mix.output_gate = false;

  stmlib::GateFlags held_gate[kBlockSize];
  bool freeze[kBlockSize];
  for (size_t i = 0; i < kBlockSize; ++i) {
    held_gate[i] = stmlib::GATE_FLAG_HIGH;
    freeze[i] = false;
  }

  beads::Processor::GrainHelperBlock blocks[2] = {};
  beads::FloatFrame helper_out[2][kBlockSize] = {};
  int write_slot = 0;
  bool have_helper = false;
  BlockStats split_stats;
  BlockStats helper_stats;
  float phase = 0.0f;
  double sine_step = kTwoPi * 220.0 / kHostSampleRate;

  const int blocks = 2000;
  for (int b = 0; b < blocks; ++b) {
    beads::FloatFrame in[kBlockSize];
    beads::FloatFrame out[kBlockSize];
    for (size_t i = 0; i < kBlockSize; ++i) {
      in[i].l = 0.25f * std::sin(phase);
      in[i].r = in[i].l;
      phase += sine_step;
    }

    const beads::FloatFrame* extra = have_helper ? helper_out[write_slot ^ 1] : nullptr;
    processor->Process(
        beads::SEED_BUTTON_STATE_OFF,
        held_gate,
        freeze,
        density,
        parameters,
        mix,
        in,
        out,
        kBlockSize,
        &blocks[write_slot],
        extra);

    beads::FloatFrame helper_block_out[kBlockSize];
    processor->RenderHelperGrains(blocks[write_slot], helper_block_out);
    std::memcpy(helper_out[write_slot], helper_block_out, sizeof(helper_block_out));
    have_helper = true;

    if (b >= 200) {
      split_stats.Add(out, kBlockSize);
      helper_stats.Add(helper_block_out, kBlockSize);
    }
    write_slot ^= 1;
  }

  SplitRunResult result;
  result.stats = split_stats;
  result.helper_stats = helper_stats;
  result.core_active = processor->ActiveGrains();
  result.helper_active = processor->HelperActiveGrains();
  return result;
}
```

For the single-core level reference, run the same dense parameters through the existing `RunBlocks` path with `SetMaxActiveGrains(16)` and no helper block; keep the last 1800 blocks of `BlockStats`. Use a fresh `Processor` (or call `Configure` to clear the delay line) so the comparison is not polluted by the previous run's audio.

Call these from `main()`:

```cpp
  ok &= TestHelperSplitRatio();

  beads::Processor split_processor;
  const SplitRunResult split =
      RunSplitCloud(&split_processor, buffers, reverb_buffer, 7, 9);
  ok &= Expect(split.core_active <= 7, "core player respects 7-grain cap");
  ok &= Expect(split.helper_active <= 9, "helper player respects 9-grain cap");
  ok &= Expect(split.core_active + split.helper_active >= 16,
               "split cloud reaches at least 16 active grains");
  ok &= Expect(split.helper_stats.rms() > 0.01,
               "helper player produces audible output");
  ok &= Expect(split.stats.rms() > 0.01,
               "combined split cloud is audible");

  const BlockStats single16 = RunSingleDense16(buffers, reverb_buffer);
  ok &= Expect(split.stats.rms() > 0.5 * single16.rms() &&
               split.stats.rms() < 2.0 * single16.rms(),
               "split level is within 6 dB of a single 16-grain cloud");
```

Add a `RunSingleDense16` helper that is a copy of `RunSplitCloud` with these changes: it owns a local `beads::Processor`, calls `SetMaxActiveGrains(16)` / `SetHelperMaxActiveGrains(0)`, passes `nullptr, nullptr` as the final two `Process` arguments, accumulates only the audio `out` frames into `BlockStats`, and returns the `BlockStats`. Keep the same parameters, 2000 blocks and 200-block settling period. Do not use the old default `RunBlocks` patch (its density/size differ).

Place the split/single tests at the **end of `main()`**, after the existing tests, so the shared delay banks are not reused by the main `processor` afterwards. The split runner reinitialises the same banks; that is intentional for this test.

Update the `sizeof` guard from `40 * 1024` to `48 * 1024`; keep the printed value:

```cpp
  ok &= Expect(sizeof(beads::Processor) <= 48 * 1024,
               "sizeof(beads::Processor) stays within budget");
```

- [ ] **Step 2: Run it and verify the expected failure**

Run:

```bash
bash tests/host/run_mbeads_tests.sh
```

Expected: compile failures for `GrainHelperBlock`, `SetHelperMaxActiveGrains`, `RenderHelperGrains`, `HelperActiveGrains`, `HelperOwnsTrigger`, or the new `Process` arguments. If the old code happens to compile because of an unrelated name, stop and report.

- [ ] **Step 3: Add the external active count to `GranularPlayer`**

In `granular_player.h`:

- Add `SetExternalActiveGrains(int n)`.
- Add `int external_active_grains_ = 0;`.
- In `Reset()`, set `external_active_grains_ = 0`.
- In `SetMaxActiveGrains`, replace the old `if (n < 1) n = 1;` with `if (n < 0) n = 0;` and update the comment as described in the interfaces. Do not clamp in the other direction here; the existing upper clamp stays.
- In `ComputeGrainAmplitudeAndPan`, add `external_active_grains_` to the `num_active_grains` expression exactly once, before the amplitude and pan spread calculations.

The helper player is initialised at the host sample rate in `Processor::Init`, just like the main players. Its `BeginBlock()` is called in `RenderHelperGrains`, not on the audio task.

- [ ] **Step 4: Add delay-line accessors and extra wet to `GranularProcessor`**

In `granular_processor.h`:

- Add the two public accessors returning the existing `mono_delay_line_` and `stereo_delay_line_` pointers.
- Add `SetExternalActiveGrains(int n)` forwarding to `granular_player_`.
- Add `const FloatFrame* extra_wet = nullptr` to `Process` and `ProcessInternal`.
- In the ratio-1 branch, pass `extra_wet` through. In the half-rate branch, pass `nullptr`; the plugin must never send extra wet to Vintage.
- Add the extra-wet sum after the granular player render and before the DC blocker/feedback update:

```cpp
      if (extra_wet) {
        out->l += extra_wet->l;
        out->r += extra_wet->r;
        ++extra_wet;
      }
```

Do not add extra wet to the dry signal capture, and do not add it twice in the output stage.

- [ ] **Step 5: Add the helper player, block and APIs to `Processor`**

In `processor.h`:

- Add the `GrainHelperBlock` nested struct exactly as specified above.
- Add the public methods and private members exactly as specified.
- Add `#include <atomic>` if not already present.
- `Process` gains the two optional parameters. Existing callers must compile unchanged through the defaults.

In `processor.cc`:

- `Init`: `helper_player_.Init(host_sample_rate); helper_active_grains_.store(0); helper_max_active_grains_.store(0); core_max_active_grains_ = 0; helper_split_counter_ = 0; helper_reset_pending_ = false;`
- `Configure`: after the existing reset, set `helper_reset_pending_ = true;` and leave the helper player untouched (it is reset by the next valid helper block).
- `SetMaxActiveGrains`: clamp `n` to `0..32`; store `core_max_active_grains_`; set the four existing players.
- `SetHelperMaxActiveGrains`: clamp `n` to `0..32`; store `helper_max_active_grains_`; do not touch `helper_player_`.
- `HelperOwnsTrigger`: exact modulo predicate.
- `HelperActiveGrains`: atomic load.
- `Process`: before the media switch, if `helper_block != nullptr`, fill it:

```cpp
  helper_block->valid = (triggers != nullptr) && media_ == MEDIA_DIGITAL;
  helper_block->size = size;
  helper_block->mono = mono_;
  helper_block->parameters = parameters;
  helper_block->core_active_grains = ActiveGrains();
  helper_block->write_ptr =
      helper_block->valid ? *mutable_delay_write_ptr() : 0;
  helper_block->reset = false;
  for (size_t i = 0; i < size; ++i) {
    helper_block->triggers[i] = triggers ? triggers[i]
                                         : TriggerGenerator::Output{};
    if (helper_block->triggers[i].trigger) {
      helper_block->triggers[i].trigger = false;
    }
  }
  if (helper_block->valid) {
    const int total = core_max_active_grains_ + helper_max_active_grains_;
    for (size_t i = 0; i < size; ++i) {
      if (triggers[i].trigger &&
          HelperOwnsTrigger(helper_split_counter_++, total,
                            helper_max_active_grains_)) {
        helper_block->triggers[i].trigger = true;
        triggers[i].trigger = false;
      }
    }
    helper_block->reset = helper_reset_pending_;
    helper_reset_pending_ = false;
  }
```

The snapshot must be captured before the `GranularProcessor::Process` call. `triggers` is the mutable trigger array; moving helper-owned triggers out of it is what makes the core player see only its share. The gate/regularity/fractional fields are copied for both players.

- `SetExternalActiveGrains` for the active media: before the switch, for Digital only, call `processor_digital_.SetExternalActiveGrains(helper_active_grains_.load())`; for the other media leave/set 0. Do not read a stale helper count for Vintage.
- In the media switch, pass `extra_wet` only to `MEDIA_DIGITAL`; pass `nullptr` to Vintage/Clean/Dirty.
- `RenderHelperGrains`: if `!block.valid || block.size == 0`, zero `block.size` frames, store `helper_active_grains_.store(0)`, and return. Otherwise reset first if `block.reset` (the reset clears the external count), then call `helper_player_.SetMaxActiveGrains(helper_max_active_grains_.load())`, `SetExternalActiveGrains(block.core_active_grains)`, `BeginBlock()`. Zero each output frame and call `helper_player_.Render(...)` with the matching Digital delay line. **Branch on `block.mono`; do not form a common-typed ternary of the mono and stereo `DelayLine` pointers** — their types differ, and the render is a template. After the loop, store `HelperActiveGrains`.

The helper player's `Render` must use the same `block.write_ptr` for the entire block. Do not call `mutable_write_ptr()` inside the helper render.

- [ ] **Step 6: Run the host test and fix only the split path**

Run:

```bash
bash tests/host/run_mbeads_tests.sh
```

Expected: PASS, including:
- `7/9 split sends 9 of 16 triggers to helper`
- `core player respects 7-grain cap`
- `helper player respects 9-grain cap`
- `split cloud reaches at least 16 active grains`
- `helper player produces audible output`
- `split level is within 6 dB of a single 16-grain cloud`
- `sizeof(beads::Processor) stays within budget`

If the split test reaches only 14–15 grains, do not weaken the assertion: check that both players see the dense trigger train and that the helper block's `valid` is true, then fix the split/cap wiring. If the RMS comparison is outside 6 dB, inspect the external-count double-add and the extra-wet gain before changing the tolerance.

Then run the modulation suite:

```bash
bash tests/host/run_tests.sh
```

Expected: PASS.

- [ ] **Step 7: Commit the submodule and parent**

Inside the submodule:

```bash
git -C components/mutable/eurorack add beads/dsp/granular_player.h \
        beads/dsp/granular_processor.h beads/dsp/processor.h \
        beads/dsp/processor.cc
git -C components/mutable/eurorack commit -m "beads: add split helper grain player"
```

In the parent:

```bash
git add tests/host/mbeads_core_test.cpp components/mutable/eurorack
git commit -m "test(mbeads): reach 16 grains through split helper path"
```

**Review note:** `Processor::Process` must not call `SetMaxActiveGrains` on the helper or touch helper-player state; only the helper task may do that. The core player's trigger array is mutated deliberately, so any code after the split that needs the original trigger edges must use `gate_out_` or the helper block, not `triggers`.

---

### Task 3: Core-0 helper task + 8/8 hardware gate

Run the Task 2 helper player on a real core-0 task, hand blocks over with a two-slot sequence-checked double buffer, and prove on hardware that **8 core + 8 helper = 16 Digital grains, dry, at max Size/Density** fits the audio budget and is audible. This is the go/no-go gate: if it fails, stop and report; do not proceed to Task 4.

**Files:**
- Modify parent: `components/ctagSoundProcessor/ctagSoundProcessorMbeads.hpp`, `components/ctagSoundProcessor/ctagSoundProcessorMbeads.cpp`, `main/DebugUI.cpp`
- Local measurement script (not committed): `/tmp/opencode/mbeads_helper_gate.py` (adapt the existing `mbeads_listen.py` / `mbeads_sweep.py`)

**Interfaces:**
- Consumes Task 2: `GrainHelperBlock`, `Processor::Process(..., helper_block, extra_wet)`, `RenderHelperGrains`, `SetHelperMaxActiveGrains`, `HelperActiveGrains`, `HelperOwnsTrigger`.
- Produces the plugin helper lifecycle and handoff:

```cpp
static constexpr std::size_t kHelperTaskStack = 4096;
static constexpr UBaseType_t kHelperTaskPriority = configMAX_PRIORITIES - 2;
static constexpr BaseType_t kHelperTaskCore = 0;
static constexpr std::size_t kHelperSlots = 2;
static constexpr std::uint32_t kAudioBudgetCycles = 174150;

static void HelperTaskEntry(void* arg);
void HelperTask();
bool StartHelper();
void StopHelper();

TaskHandle_t helper_task_ = nullptr;
std::atomic<bool> helper_stop_{false};
std::atomic<bool> helper_running_{false};
std::atomic<int> helper_pending_slot_{0};
std::atomic<std::uint32_t> helper_pending_seq_{0};
std::atomic<std::uint32_t> helper_done_seq_[kHelperSlots] = {};
std::atomic<bool> helper_slot_free_[kHelperSlots] = {};
beads::Processor::GrainHelperBlock helper_blocks_[kHelperSlots] = {};
beads::FloatFrame helper_out_[kHelperSlots][kBlockSize] = {};
std::uint32_t helper_block_counter_ = 0;
std::atomic<std::uint32_t> helper_cycles_{0};
std::atomic<bool> helper_overload_{false};
std::atomic<int> helper_stack_high_water_{-1};
```

All FreeRTOS-typed members and methods are inside `#ifndef TBD_SIM` in the header and `.cpp`. The simulator build's `helper_task_` path is absent and the plugin uses the no-helper cap.

- [ ] **Step 1: Extend the failing hardware gate script**

Copy the existing bring-up script rather than changing the recon history:

```bash
cp /tmp/opencode/mbeads_listen.py /tmp/opencode/mbeads_helper_gate.py
```

Adapt it to:

1. Load Mbeads with `POST /debug/setActivePlugin {"id":"Mbeads","ch":0}`.
2. Set Digital and the max worst case: `media=0`, `size=4095`, `density=4095`, `dry_wet=2048`, `reverb=0`.
3. Set the total cap and helper cap: `POST /debug/setGrainCap {"value":16}` and `POST /debug/setHelperGrainCap {"value":8}`.
4. Wait 5 seconds for the cloud to fill, then read `POST /debug/getAudioStats` 60 times at 100 ms intervals (3 × 20 samples), and print `audioCycles`, `cpuOverload`, `activeGrains`, `helperActiveGrains`, `helperCycles`, `helperOverload`, `helperStackHighWater`, `grainCap`, `helperGrainCap`.
5. Fail with a non-zero exit code unless the final 20-sample window satisfies:

```text
activeGrains <= 8
helperActiveGrains <= 8
activeGrains + helperActiveGrains >= 16
average(audioCycles) <= 155000
average(helperCycles) <= 120000
helperOverload == 0 for all samples
helperStackHighWater >= 512
helperActiveGrains != -1
```

`cpuOverload` is advisory at this gate (the recon measured 23% flags at cap 8 dry); record its percentage. Any audible dropout, reboot, `helperActiveGrains == -1`, or helper-overload sample fails the gate regardless of averages.

- [ ] **Step 2: Run it and verify the expected failure**

Build/flash the current Task 2 image only after user approval, then run:

```bash
python3 /tmp/opencode/mbeads_helper_gate.py
```

Expected on the current firmware: `/debug/setHelperGrainCap` is unknown and/or `helperActiveGrains` is missing; the total is still one-core and over budget at cap 16. The script exits non-zero. Do not "fix" the script to pass on the old firmware.

- [ ] **Step 3: Implement the helper task and block handoff**

In `ctagSoundProcessorMbeads.hpp` add the guarded members and methods listed above. Include `<freertos/FreeRTOS.h>`, `<freertos/task.h>`, `<atomic>`, `<cstdint>` under `#ifndef TBD_SIM`.

In `ctagSoundProcessorMbeads.cpp`:

- `StartHelper()`:
  - Return true if `helper_task_ != nullptr`.
  - `helper_stop_.store(false); helper_running_.store(false);`
  - Initialise `helper_done_seq_[0/1]` to `UINT32_MAX`, `helper_slot_free_[0/1]` to true, `helper_pending_slot_`/seq to 0.
  - Call `xTaskCreatePinnedToCore(HelperTaskEntry, "MbeadsGrain", kHelperTaskStack, this, kHelperTaskPriority, &helper_task_, kHelperTaskCore)`.
  - On failure, set `helper_task_ = nullptr`, `ESP_LOGE("Mbeads", "helper task creation failed")`, and return false.
  - On `pdPASS`, **wait up to 20 ms for `helper_running_` to become true**. This closes the unload race where `StopHelper` could see a created-but-not-yet-started task, delete its handle and return while the task later runs on a freed plugin object. If the wait times out, `vTaskDelete(helper_task_)`, set `helper_task_ = nullptr`, log, and return false. Do not assert and do not silently use 16 grains without a helper.
- `HelperTaskEntry` calls `static_cast<ctagSoundProcessorMbeads*>(arg)->HelperTask()`.
- `HelperTask()`:

```cpp
  helper_running_.store(true, std::memory_order_release);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (helper_stop_.load(std::memory_order_acquire)) break;

    const int slot = helper_pending_slot_.load(std::memory_order_acquire);
    const std::uint32_t seq =
        helper_pending_seq_.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_acquire);

    const std::uint32_t start = esp_cpu_get_cycle_count();
    processor.RenderHelperGrains(helper_blocks_[slot], helper_out_[slot]);
    const std::uint32_t cycles = esp_cpu_get_cycle_count() - start;

    helper_cycles_.store(cycles, std::memory_order_relaxed);
    helper_overload_.store(cycles > kAudioBudgetCycles,
                           std::memory_order_relaxed);
    helper_stack_high_water_.store(
        static_cast<int>(uxTaskGetStackHighWaterMark(nullptr) *
                         sizeof(StackType_t)),
        std::memory_order_relaxed);

    helper_done_seq_[slot].store(seq, std::memory_order_release);
    helper_slot_free_[slot].store(true, std::memory_order_release);
  }
  helper_running_.store(false, std::memory_order_release);
  vTaskDelete(nullptr);
```

`esp_cpu_get_cycle_count()` must wrap only the render, not the stats or notification wait. Include `esp_cpu.h` inside the firmware guard.

- `StopHelper()`:
  - If `helper_task_ == nullptr`, return.
  - `helper_stop_.store(true); xTaskNotifyGive(helper_task_);`
  - Wait up to 100 ms in 1 ms steps for `helper_running_ == false`.
  - If it is still running, call `vTaskDelete(helper_task_)`.
  - Set `helper_task_ = nullptr`; do not touch the task handle again. The normal path is the helper self-deleting after setting `helper_running_ = false`.
- The destructor calls `StopHelper()` **before** freeing `delay_buffer_`/`reverb_buffer_` and before the `Processor` is destroyed. The `Processor` is a member destroyed after the destructor body, so stopping first protects its delay lines.

- [ ] **Step 4: Add the audio-task block handoff and production cap policy**

In `Process()`:

1. Compute `reverb_active` exactly as today.
2. Compute `helper_available = (helper_task_ != nullptr) && helper_running_.load(std::memory_order_acquire)`. `StartHelper()` waits for `helper_running_` before returning, so this is true for a live helper and false for a failed task or a task that has exited. If false, use the safe one-core fallback.
3. Compute caps:

```cpp
  const int total_override = gMbeadsDebugTotalOverride.load();
  const int helper_override = gMbeadsDebugHelperOverride.load();
  const int default_total =
      media == 0
          ? (helper_available ? 16 : (reverb_active ? 2 : 4))
          : (reverb_active ? 8 : 16);
  const int default_helper =
      (media == 0 && helper_available) ? 8 : 0;  // Task 4 changes 8 -> 9
  int total = total_override >= 0 ? total_override : default_total;
  int helper = helper_override >= 0 ? helper_override : default_helper;
  total = std::max(0, std::min(32, total));
  helper = std::max(0, std::min(total, helper));
  const int core = total - helper;
  processor.SetMaxActiveGrains(core);
  processor.SetHelperMaxActiveGrains(helper);
```

This is the Task 3 gate policy: 16/8 Digital when the helper exists. The fallback no-helper policy is the existing 4 dry / 2 reverb; Vintage is 16 dry / 8 reverb.

4. Handoff. Use exactly this order:

```cpp
  const int write_slot = static_cast<int>(helper_block_counter_ & 1u);
  const int read_slot = write_slot ^ 1;
  const bool use_helper = helper_available && media == 0 && helper > 0;
  const bool helper_ready =
      use_helper && helper_block_counter_ > 0 &&
      helper_done_seq_[read_slot].load(std::memory_order_acquire) ==
          helper_block_counter_ - 1;
  const beads::FloatFrame* extra_wet =
      helper_ready ? helper_out_[read_slot] : nullptr;

  const bool queue_helper =
      use_helper &&
      helper_slot_free_[write_slot].exchange(false, std::memory_order_acquire);
  beads::Processor::GrainHelperBlock* helper_block =
      queue_helper ? &helper_blocks_[write_slot] : nullptr;

  // PERF_MEASURE("Mbeads") / PERF_MEASURE("MbeadsGranular") wrap this call as
  // today. The helper block is filled inside Processor::Process before the
  // delay-line writes; extra_wet is the previous completed helper block.
  processor.Process(seed_state, seed_input, freeze_frames_, density, p, mix,
                    in_, out_, kBlockSize, helper_block, extra_wet);

  if (queue_helper) {
    helper_pending_slot_.store(write_slot, std::memory_order_relaxed);
    helper_pending_seq_.store(helper_block_counter_, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    xTaskNotifyGive(helper_task_);
  }
  ++helper_block_counter_;
```

If `helper_ready` is false, pass `nullptr`; do not wait. If the helper misses a slot, the `exchange(false)` fails and that block is skipped; the core continues. The helper output is mixed one block later by `Processor::Process`.

5. After the call, publish stats:

```cpp
  gMbeadsDebugActiveGrains.store(processor.ActiveGrains());
  gMbeadsDebugHelperActive.store(
      helper_available ? processor.HelperActiveGrains() : -1);
  gMbeadsDebugCoreCap.store(core);
  gMbeadsDebugHelperCap.store(helper);
  gMbeadsDebugHelperCycles.store(
      static_cast<int>(helper_cycles_.load(std::memory_order_relaxed)));
  gMbeadsDebugHelperOverload.store(
      helper_overload_.load(std::memory_order_relaxed) ? 1 : 0);
  gMbeadsDebugHelperStack.store(
      helper_stack_high_water_.load(std::memory_order_relaxed));
```

The `gMbeads*` atomics are file-scope in the plugin `.cpp`; the C hooks below are `extern "C"`. The cap overrides are `std::atomic<int>` initialised to `-1`; the stats are `-1`/`0` initially.

- [ ] **Step 5: Rename the temporary debug hooks and add helper endpoints**

Replace the `mbeads_spike_*` C symbols with stable debug names (the HTTP paths stay compatible):

```cpp
extern "C" int mbeads_debug_set_total_cap(int value);
extern "C" int mbeads_debug_get_total_cap();
extern "C" int mbeads_debug_set_helper_cap(int value);
extern "C" int mbeads_debug_get_helper_cap();
extern "C" int mbeads_debug_get_active_grains();
extern "C" int mbeads_debug_get_helper_active_grains();
extern "C" int mbeads_debug_get_helper_cycles();
extern "C" int mbeads_debug_get_helper_overload();
extern "C" int mbeads_debug_get_helper_stack_high_water();
extern "C" int mbeads_debug_get_core_cap();
```

Semantics:
- `set_total_cap` sets `gMbeadsDebugTotalOverride`; `-1` restores the production total. It returns the old value. The HTTP `/debug/setGrainCap` handler calls it.
- `set_helper_cap` sets `gMbeadsDebugHelperOverride`; `-1` restores the production helper cap. Add HTTP `/debug/setHelperGrainCap` with the same `{"value":N}` shape and `{"ok":1,"old":...,"cap":...}` response.
- `get_total_cap` returns the current total override (`-1` when automatic); `get_helper_cap` returns the helper override.
- The `getAudioStats` JSON adds:

```json
"helperActiveGrains": 8,
"helperCycles": 77000,
"helperOverload": 0,
"helperStackHighWater": 1800,
"coreGrainCap": 8,
"helperGrainCap": 8,
"helperGrainCapOverride": -1
```

Keep `activeGrains` as the core player's count for script compatibility. Add `totalActiveGrains = activeGrains + helperActiveGrains` in the JSON (if `helperActiveGrains < 0`, total is `activeGrains`). Keep `grainCapOverride` as the total override.

- [ ] **Step 6: Initialise and tear down the task**

In `Init()`, after the delay/reverb buffers and `processor.Init(...)` succeed, call `StartHelper()`. If it returns false, log and continue with `helper_task_ == nullptr` (the fallback caps). Do not fail plugin init: a missing helper should degrade to the safe single-core cap, not silence the plugin.

In the destructor, call `StopHelper()` first; then set all `gMbeadsDebug*` stats to `-1`; then free the existing buffers.

Guard all task creation/notification/`esp_cpu_get_cycle_count`/`TaskHandle_t` code with `#ifndef TBD_SIM`. The simulator's `Process` must take the no-helper branch with `helper_available == false`; it must still compile the DSP core and debug getters.

- [ ] **Step 7: Build, run host tests, flash app only, run the gate**

Build:

```bash
source /home/schrab/esp/esp-idf-v5.5.4/export.sh
idf.py build
```

Expected: build succeeds; `build/ctag-tbd.bin` exists; app size + free are reported. Confirm free space is positive (current baseline: 415,712 bytes free).

Host tests:

```bash
bash tests/host/run_mbeads_tests.sh
bash tests/host/run_tests.sh
```

Both PASS.

Ask the user for explicit approval, then flash **app only**:

```bash
source /home/schrab/esp/esp-idf-v5.5.4/export.sh
python -m esptool --chip esp32 -b 460800 --port /dev/ttyUSB0 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x10000 build/ctag-tbd.bin
```

Never include `0x710000 build/storage.bin` in this command.

Run the gate:

```bash
python3 /tmp/opencode/mbeads_helper_gate.py
```

Expected: PASS — `activeGrains` ≈ 8, `helperActiveGrains` ≈ 8, total ≥ 16, `audioCycles` ≈ 144k (≤155k), `helperCycles` ≈ 77k (≤120k), `helperOverload == 0`, `helperStackHighWater >= 512`, no reboot. Record the exact 20-sample averages, overload percentages and stack high-water in the task report.

- [ ] **Step 8: Stop on gate failure; otherwise commit**

If the gate fails on core/helper budget, helper creation, stack, or audible dropouts, **stop the plan and report the measurements**. Do not proceed to Task 4 and do not reduce the grain count silently. The fallback options to propose are: raise the helper priority one step and retest; use half-rate 16-bit or linear interpolation for the Digital engine; or return to a new plan with a lower target. The user must choose.

On pass:

```bash
git add components/ctagSoundProcessor/ctagSoundProcessorMbeads.hpp \
        components/ctagSoundProcessor/ctagSoundProcessorMbeads.cpp \
        main/DebugUI.cpp
git commit -m "feat(mbeads): render Digital grains on a core-0 helper"
```

**Review note:** The helper must never be created inside `Process()`, and the audio task must never wait on `helper_done_seq_`. The `helper_slot_free_` exchange is the only guard against overwriting a block the helper is still reading; keep it even though the gate should never miss.

---

### Task 4: Production 7/9 split (16 grains) + reverb + listening

Move the production Digital policy from the dry 8/8 feasibility gate to the reverb-safe **7 core / 9 helper = 16 grains** split. Prove both dry and reverb 4095 on hardware, then have the user listen. This is the primary acceptance task.

**Files:**
- Modify parent: `components/ctagSoundProcessor/ctagSoundProcessorMbeads.cpp`
- Modify local measurement script: `/tmp/opencode/mbeads_helper_gate.py` (add cases; not committed)

**Interfaces:**
- Consumes Task 3: helper task, cap policy, `/debug/setHelperGrainCap`, `getAudioStats`.
- Produces the production Digital policy:
  - helper available, Digital: total 16, helper 9, core 7;
  - helper unavailable, Digital: total 4 dry / 2 reverb, core = total, helper 0;
  - Vintage: total 16 dry / 8 reverb, helper 0;
  - debug overrides still apply and are clamped as in Task 3.

- [ ] **Step 1: Extend the gate script with production cases**

Add these cases to `/tmp/opencode/mbeads_helper_gate.py`:

| Case | media | size | density | reverb | total cap | helper cap | Expected core | Expected helper |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `digital_dry_16` | 0 | 4095 | 4095 | 0 | 16 | 9 | ≤155k | ≤120k |
| `digital_reverb_16` | 0 | 4095 | 4095 | 4095 | 16 | 9 | ≤170k | ≤120k |
| `vintage_dry_16` | 1 | 4095 | 4095 | 0 | 16 | 0 | ≤174k | — |
| `vintage_reverb_8` | 1 | 4095 | 4095 | 4095 | 8 | 0 | ≤170k | — |

Each case must assert:
- `activeGrains + max(0, helperActiveGrains) >= total cap` for the dense setting;
- `activeGrains <= core cap`, `helperActiveGrains <= helper cap`;
- the 20-sample average of `audioCycles` is at or below the case's core threshold;
- for Digital, the 20-sample average of `helperCycles` is at or below 120,000 and `helperOverload == 0`;
- `helperStackHighWater >= 512` for Digital;
- no reboot/assert; record `cpuOverload` percentage and the exact averages.

`cpuOverload` is advisory for the final listening decision, not a hard fail by itself: the recon shows the flag is conservative at the one-core ceiling. An audible dropout, a helper-overload sample, or a missing helper is a hard fail.

- [ ] **Step 2: Run the new cases and verify the expected failure**

With the Task 3 image still flashed, run:

```bash
python3 /tmp/opencode/mbeads_helper_gate.py --case digital_reverb_16
```

Expected: the 8/8 Task 3 policy with reverb 4095 is over the core threshold (recon 8/8 + reverb ≈169.2k core, plus the helper block/extra-wet overhead); the case exits non-zero. The dry case may already pass; that is expected. Do not change the threshold to make 8/8 reverb pass.

- [ ] **Step 3: Change the production default to 7/9**

In `ctagSoundProcessorMbeads.cpp`, change only the Task 3 default helper cap from 8 to 9:

```cpp
  const int default_helper =
      (media == 0 && helper_available) ? 9 : 0;
```

Keep `default_total` at 16 for Digital with a helper. The resulting core is 7. Do not hard-code 7 separately; compute `core = total - helper`.

- [ ] **Step 4: Build, host-test, flash app only, run the Digital and Vintage gates**

Build and host-test exactly as Task 3:

```bash
source /home/schrab/esp/esp-idf-v5.5.4/export.sh
idf.py build
bash tests/host/run_mbeads_tests.sh
bash tests/host/run_tests.sh
```

Expected: build succeeds, both host suites PASS.

Ask for approval, then flash app only:

```bash
python -m esptool --chip esp32 -b 460800 --port /dev/ttyUSB0 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x10000 build/ctag-tbd.bin
```

Run all four cases:

```bash
python3 /tmp/opencode/mbeads_helper_gate.py --case digital_dry_16
python3 /tmp/opencode/mbeads_helper_gate.py --case digital_reverb_16
python3 /tmp/opencode/mbeads_helper_gate.py --case vintage_dry_16
python3 /tmp/opencode/mbeads_helper_gate.py --case vintage_reverb_8
```

Expected: all pass the numeric thresholds; Digital reports ≈7 active core + ≈9 helper = 16 total; Vintage reports 16 dry / 8 reverb with helper 0. If `digital_reverb_16` exceeds 170k or audibly drops out, try the fallback in Step 6.

- [ ] **Step 5: User listening gate (primary acceptance)**

Ask the user to listen at the worst-case settings. Adapt `/tmp/opencode/mbeads_listen.py` with the same `--helper-cap`, `--size` and `--density` flags used by the gate script if it does not already have them, then leave the device in the `digital_reverb_16` state:

```bash
python3 /tmp/opencode/mbeads_listen.py --cap 16 --helper-cap 9 \
  --media 0 --size 4095 --density 4095 --reverb 4095
```

Ask: "Does this sound like a dense 16-grain cloud, with no crackle, dropouts, or obvious level jump compared with the single-core 8-grain version?" Then repeat dry (`--reverb 0`). Record the verdict in the ledger and task report.

This is a required human gate. If the user rejects the sound, stop and report the measured values and their description; do not claim completion and do not proceed to Task 5 tuning without their direction.

- [ ] **Step 6: Fallback tuning if 7/9 does not fit**

If the core is over 170k or the user hears dropouts with reverb 4095:

1. First try **6 core / 10 helper** (total 16): core reverb estimate ≈153k, helper ≈96k. Re-run `digital_reverb_16` and the listening gate.
2. If the helper misses its deadline instead, raise `kHelperTaskPriority` to `configMAX_PRIORITIES - 1` and re-run both Digital cases. Do not raise it above `configMAX_PRIORITIES - 1`.
3. If still over budget, stop and report; do not reduce below 16 grains without explicit user approval. The documented fallback is a new plan using half-rate 16-bit or linear Digital, not a silent 12-grain build.

Keep the 7/9 split if it passes; keep 6/10 only if the user accepts it and the total remains 16.

- [ ] **Step 7: Commit the production split**

```bash
git add components/ctagSoundProcessor/ctagSoundProcessorMbeads.cpp
git commit -m "feat(mbeads): use reverb-safe 7/9 grain split for 16-grain Digital cloud"
```

**Review note:** Task 4 changes only the default split and any priority/tuning fallback. If the fallback changes `kHelperTaskPriority`, include that in the same commit and state it in the commit body. Do not mix docs or cleanup into this commit.

---

### Task 5: 20-grain tuning + cleanup + docs + final validation

Decide whether 20 grains is genuinely stable; if it is, promote it, otherwise keep the accepted 16-grain policy. Remove the recon scaffolding, document the second-core design and measured results, and run the final regression, soak and human acceptance. No new feature work.

**Files:**
- Modify parent: `components/ctagSoundProcessor/ctagSoundProcessorMbeads.cpp`, `main/DebugUI.cpp` (cleanup only)
- Modify parent docs: `doc/mbeads-port.md`, `doc/mbeads-implementation-plan.md`, this plan's Task Status table
- Modify Beads submodule only if the public API audit removes an unused method (then update the parent gitlink)
- Local measurement script: `/tmp/opencode/mbeads_helper_gate.py` (20-grain cases; not committed)

**Interfaces:**
- Consumes all previous tasks.
- Produces the final production policy, documentation, and a commit ready for the user's review. No new public DSP interfaces are planned.

- [ ] **Step 1: Run the optional 20-grain cases**

Add these cases to `/tmp/opencode/mbeads_helper_gate.py`:

| Case | media | reverb | total cap | helper cap | core cap | Core threshold | Helper threshold |
|---|---|---:|---:|---:|---:|---:|---:|
| `digital_dry_20` | 0 | 0 | 20 | 13 | 7 | ≤150k | ≤150k |
| `digital_reverb_20` | 0 | 4095 | 20 | 15 | 5 | ≤165k | ≤160k |

For each case, wait 5 seconds, read 60 samples, and require:
- `activeGrains <= core cap`, `helperActiveGrains <= helper cap`, total ≥20;
- average `audioCycles` below the core threshold;
- average `helperCycles` below the helper threshold and `helperOverload == 0`;
- `helperStackHighWater >= 512`;
- no reboot, and the OLED/encoder still responds while the cloud runs (manually check the UI; Wi-Fi only if the build has it).

Run both cases on hardware after the Task 4 image is accepted. If either fails, **keep the 16-grain 7/9 production default** and record the failed 20-grain measurements. Do not ship 20 grains on a "looks close" result.

If both pass, ask the user to listen to both 20-grain cases. Only if the user accepts the density and hears no dropouts, change the production Digital policy to:

```cpp
  const int default_total =
      (media == 0 && helper_available)
          ? 20
          : (media == 0 ? (reverb_active ? 2 : 4)
                        : (reverb_active ? 8 : 16));
  const int default_helper =
      (media == 0 && helper_available) ? (reverb_active ? 15 : 13) : 0;
```

The helper player has 32 slots, so 15 is legal. If the user rejects 20, leave the Task 4 16/9 policy unchanged.

- [ ] **Step 2: Audit the new public Beads API**

Inspect the Task 2 additions. `HelperOwnsTrigger`, `SetHelperMaxActiveGrains`, `RenderHelperGrains`, and `HelperActiveGrains` all have production/test callers. There is no separate `ResetHelperGrains` API: reset is owned by `Configure` → `helper_block.reset` → `RenderHelperGrains`. Confirm no stale declaration or implementation survived from the recon branch, and remove any unused helper method you find. Do not leave an untested public method.

Do not change `GrainHelperBlock`, `Process`, `ReadFrom`, or the split semantics in this step.

- [ ] **Step 3: Remove recon scaffolding**

- Remove every `TEMP CPU-SPIKE` comment from the plugin, `main/DebugUI.cpp`, and `components/mutable/CMakeLists.txt`.
- Keep the `-O2` property and its permanent justification comment.
- Keep the `/debug/setGrainCap`, `/debug/setHelperGrainCap`, `/debug/setActivePlugin` and `/debug/getAudioStats` endpoints; they are the Mbeads bring-up tools. Change the `setActivePlugin` comment to "Debug-only deterministic plugin load for bring-up," not a spike marker.
- Keep the `mbeads_debug_*` C names and `gMbeadsDebug*` globals; remove stale "spike" names from comments and scripts.
- Update the `GranularPlayer::SetMaxActiveGrains` comment to the production cap-0 semantics; remove the recon wording.
- Do not delete the debug stats fields; they are required for the final soak and future regression.

- [ ] **Step 4: Update the docs with measured facts**

In `doc/mbeads-port.md`:

- Add a new §11 subsection **"Second-core dense mode (v1.1)"** with:
  - the Digital 7/9 split (or final split if 20 was accepted), total grains, helper task core/priority/stack, double-buffer handoff, one-block latency, snapshot read, shared amplitude/pan count, and extra-wet placement;
  - the exact Task 3/4 gate averages, overload percentages, helper stack high-water, audio stack high-water, `sizeof(beads::Processor)`, app image size and free bytes;
  - the fallback rules: helper creation failure → 4 dry / 2 reverb single-core; Vintage 16 dry / 8 reverb single-core; 20 only if accepted;
  - the Wi-Fi/UI contention caveat and the priority used;
  - the debug endpoints and their units.
- Add a pointer in §4 (feasibility) and §9 (risks) that the 4/2 cap is the single-core fallback and the v1.1 Digital mode uses the second-core split. Do not rewrite the historical v1.0 measurements; mark them as v1.0.
- Update the §11 status table values that changed: `sizeof`, app image/free, audio stack, helper stack, and the final CPU/grain result.
- Keep `Delay mode` documented as disabled.

In `doc/mbeads-implementation-plan.md`:

- Add a Task 4 row: "Second-core dense grain cloud" with status done, the parent commits, and a link to this plan.
- Note that the v1.1 plan is `docs/superpowers/plans/2026-10-22-mbeads-second-core-dense-cloud.md`.

Do **not** modify `AGENTS.md` in this task. The Mbeads design doc is authoritative; the user can promote a summary later.

Update this plan's Task Status table with the actual task commits. Do not claim a task complete without its gate evidence.

- [ ] **Step 5: Final host and build verification**

Run:

```bash
bash tests/host/run_mbeads_tests.sh
bash tests/host/run_tests.sh
source /home/schrab/esp/esp-idf-v5.5.4/export.sh
idf.py build
```

Expected: both host suites PASS; build succeeds; the new split tests still pass; app partition free is positive. Record the final `build/ctag-tbd.bin` size and free bytes in the task report and docs.

- [ ] **Step 6: Flash the final image and run the full regression**

Ask for approval, then flash app only at `0x10000` (same command as Task 4; never storage).

Run all accepted gate cases:

```bash
python3 /tmp/opencode/mbeads_helper_gate.py --case digital_dry_16
python3 /tmp/opencode/mbeads_helper_gate.py --case digital_reverb_16
python3 /tmp/opencode/mbeads_helper_gate.py --case vintage_dry_16
python3 /tmp/opencode/mbeads_helper_gate.py --case vintage_reverb_8
```

If 20 was accepted, also run `digital_dry_20` and `digital_reverb_20`. Expected: all accepted cases pass; total Digital grains are the production target; helper overload is 0; Vintage helper count is 0; no reboot.

- [ ] **Step 7: Load/unload and media-switch lifetime test**

Using the debug endpoint, perform at least 10 cycles:

```text
load Mbeads -> wait 2 s -> load another stereo plugin -> wait 1 s -> load Mbeads again
```

After each Mbeads load, verify `helperActiveGrains` is not `-1`, the total reaches 16 (or 20) at the dense setting, and the device does not reboot or assert. Switch Mbeads to Vintage and back to Digital at least 5 times; after each switch, confirm the helper block is not mixed and the Digital total recovers. This exercises `StopHelper`, `StartHelper`, the reset flag and the PSRAM buffer lifetime.

- [ ] **Step 8: 10-minute worst-case soak**

Leave the device in `digital_reverb_16` (or the accepted 20-grain reverb case) at max Size/Density for 10 minutes. Every 10 seconds record `audioCycles`, `cpuOverload`, `activeGrains`, `helperActiveGrains`, `helperCycles`, `helperOverload`, `helperStackHighWater`. Then repeat 3 minutes dry. Expected: no reboot; total grains do not collapse; helper overload stays 0 after settling; audio and helper stack high-water >512 bytes; the user does not hear recurring dropouts. If the UI/Wi-Fi build shows helper misses only while Wi-Fi is active, record that and either raise the priority one step or document the limitation; do not hide it.

- [ ] **Step 9: Final user listening and sign-off**

Ask the user to listen to the final Digital dry and reverb cases and the final Vintage cases. State the production grain counts and the fallback counts. Ask for explicit acceptance of the dense cloud. Record the verdict. If the user rejects, stop; do not merge or mark complete.

- [ ] **Step 10: Commit, final review, and handoff**

Commit the cleanup, docs and final policy (include the submodule gitlink only if a Beads API was removed):

```bash
git add components/ctagSoundProcessor/ctagSoundProcessorMbeads.cpp \
        main/DebugUI.cpp doc/mbeads-port.md \
        doc/mbeads-implementation-plan.md \
        docs/superpowers/plans/2026-10-22-mbeads-second-core-dense-cloud.md
# If and only if Task 5 changed the submodule:
# git add components/mutable/eurorack
git commit -m "docs(mbeads): document second-core dense mode and finalise Digital grain policy"
```

Then:
- Run the final whole-branch review through the SDD controller.
- Ask the user to review the branch; do **not** push, merge, tag or release without explicit approval.
- Report the commit list, final measured table, app size/free, and the user's listening verdict.

---

## Hardware Measurement Protocol (all hardware tasks)

Common commands:

```bash
source /home/schrab/esp/esp-idf-v5.5.4/export.sh
idf.py build

# app only; user approval first:
python -m esptool --chip esp32 -b 460800 --port /dev/ttyUSB0 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x10000 build/ctag-tbd.bin
```

The debug UI is on UART0. Use the existing `/tmp/opencode/mbeads_serial.py` and `mbeads_sweep.py` helpers, or send the same JSON requests directly. The relevant commands are:

```json
{"cmd":"/debug/setActivePlugin","id":"Mbeads","ch":0}
{"cmd":"/debug/setGrainCap","value":16}
{"cmd":"/debug/setHelperGrainCap","value":9}
{"cmd":"/debug/setPluginParam","ch":0,"id":"media","value":0}
{"cmd":"/debug/setPluginParam","ch":0,"id":"size","value":4095}
{"cmd":"/debug/setPluginParam","ch":0,"id":"density","value":4095}
{"cmd":"/debug/setPluginParam","ch":0,"id":"reverb","value":4095}
{"cmd":"/debug/getAudioStats"}
```

`/debug/setGrainCap` and `/debug/setHelperGrainCap` accept `-1` to restore the production policy. `activeGrains` is the core player only; `helperActiveGrains` is the helper player; `totalActiveGrains` is their sum. `audioCycles` is the core-1 block cost; `helperCycles` is the helper render cost; both are in 240 MHz cycles. `helperStackHighWater` and `stackHighWater` are bytes free (minimum ever). `cpuOverload`/`helperOverload` are the last-block flags.

Measurement rules:
- 5-second settle, then 3 × 20 samples at 100 ms.
- Report the average and the overload percentage; do not report a single best sample.
- The audio budget is 174,150 cycles. Thresholds in the tasks are deliberately tighter.
- The user must approve each flash and perform each listening gate.

## Stop Conditions

Stop the plan and report instead of improvising when:

- Task 3's 8/8 dry gate misses the helper deadline, fails task creation, overruns the core threshold, reboots, or has audible dropouts.
- Task 4's 7/9 reverb case remains over 170k or audibly drops out after the 6/10 fallback and priority check.
- The user rejects the density or hears crackle/dropouts.
- A load/unload or media-switch test reboots, asserts, or leaves `helperActiveGrains == -1`.
- The 20-grain cases are not stable; keep 16 and document the failed numbers.
- Any task requires staging unrelated dirty paths, rewriting the submodule's nested dirty repos, or raising the audio budget.
- A host test fails for a reason outside the task's intended change; do not weaken the assertion.

## Definition of Done

- The user has explicitly accepted the dense Digital cloud at the final production split (16 minimum; 20 only if accepted and stable).
- Digital dry and reverb 4095 run at the production total with no audible dropouts; the helper's `helperCycles` and `helperOverload` show deadline compliance.
- Vintage 16 dry / 8 reverb still works single-core; the helper is idle in Vintage.
- The plugin loads, unloads, reloads and switches media without a crash or use-after-free.
- Host suites pass; the firmware builds; the app partition has free space; the app-only flash is the tested image.
- `doc/mbeads-port.md` and `doc/mbeads-implementation-plan.md` describe the final architecture and measured limits.
- The working tree contains only the task commits plus the user's known unrelated dirt; nothing is pushed or merged without approval.

## Execution Handoff

Execute with `superpowers:subagent-driven-development` in the current `feat/mbeads` workspace. Tasks 0–5 are sequential: Task 1 consumes Task 0's baseline; Task 2 consumes Task 1's `ReadFrom`/write-pointer API; Task 3 consumes Task 2's helper player; Task 4 changes only the production split; Task 5 finalises. Dispatch one fresh implementer per task, then a task reviewer. Include in each brief: the task section, the Global Constraints, the Interfaces block, and the exact files. Do not let an implementer edit unrelated paths or skip a hardware gate.

For every review package, include the submodule diff explicitly (`git diff --submodule=diff` and the corresponding `git -C components/mutable/eurorack diff`). The parent gitlink alone is not a reviewable code change.

The only planned pauses are the user approval before each flash and the user listening gates in Tasks 4 and 5. All other tasks run continuously. Stop on any condition in **Stop Conditions**.

Plan complete and saved to `docs/superpowers/plans/2026-10-22-mbeads-second-core-dense-cloud.md`. This plan targets SDD; please review it before starting a new session.
