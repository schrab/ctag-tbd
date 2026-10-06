# Handoff Document — ctag-tbd modulation fix pass + MIX page rewrite

**Date:** 2026-10-05  
**Branch:** `feat/norns-ui-phase1` at `1db1daed` (pushed to `origin/feat/norns-ui-phase1`)  
**Last firmware flashed:** `build/ctag-tbd.bin` (3,172,448 bytes, committed hash `1db1daed` = fix pass)  
**Device status:** Flashed and booting clean on `/dev/ttyUSB0` (CP2102 on busid 2-2, attached via `usbipd`)

---

## What's been completed (all pushed, tests green)

### Code review fix pass (commit `8133f0d2`)

All 4 critical + 10 important findings from the review are fixed:

| # | Issue | Fix |
|---|-------|-----|
| 1 | **OOB stack write** from unvalidated `cvSlot`/`trigSlot` loaded from `mod-config.jsn` | New `ctagSlotBounds.hpp` with `ClampCVSlot()`/`ClampTrigSlot()`; all 6 setters route through it; `LoadConfig` type-checks every member |
| 2 | **Sequencer steps uneditable** in UI | Added `editing` branch in `onEncoder` for step list, mirrors gate page |
| 3 | **MIDI clock didn't update BPM** — seq/gate ran at stale tempo | `OnMidiClock()` now derives BPM from clock interval (rolling average over 24) |
| 4 | **Unterminated `buf[4]`** in font test page | `buf[utf8_encode(cp, buf)] = 0;` after encode |
| 5 | **Gate swing inverted** (reverse swing) | Now lengthens off-beat: even steps `1−swing×0.2`, odd `1+swing×0.2` → 1.5:1 at full swing |
| 6 | **Stray MIDI 0xFC froze internal clock** | `Start`/`Stop`/`Continue` gated to `MIDI_CLOCK` source; new `Reset()` exposed in `SP_TEMPO` |
| 7 | **Tap tempo didn't average** | Now keeps 4-interval ring buffer and sets BPM from mean |
| 8 | **Hard-coded 44100/32** ignored `SetSampleRate` | `blocks_per_second` stored as member; `SetBPM`/`SetSource` reuse it |
| 9 | **Gate ran 32-iteration loop with division** when disabled | Early-out `if (trigSlot < 0 && accentSlot < 0) return;` + hoisted division |
| 10 | **Tempo not persisted** despite doc schema | `tempo` object written/read in `SaveConfig`/`LoadConfig` |
| 11 | **SP_MAIN overflowed display** (8 items on 64-row screen) | Added scrolling with `VISIBLE_ITEMS_HDR=6` |
| 12 | **SaveConfig 2 KB buffer on 4 KB UIMenu stack** | Reverted to 512 B; `FileWriteStream` flushes in chunks |

**Tests:** `tests/host/run_tests.sh` — 43 assertions on real helper sources (FreeRTOS shim), green under ASan+UBSan.

### Gitignore cleanup (commit `1db1daed`)

Added to `.gitignore`:
```
__pycache__/
*.pyc
*:Zone.Identifier
```
Verified with `git check-ignore` — the six `:Zone.Identifier` files and `__pycache__/` are now ignored. Deleted untracked `UI/fonts/Norns_full.c` (dead LVGL artifact).

---

## In progress: MIX page rewrite (not yet built/flashed)

### Goal

Replace the font-test `UIMenuPageMix` with a real mixer page:

- **Vertical faders with VU meters** for Line In, Mic, Stereo Out (like Norns screenshot)
- **Input source switch** (Mic / Line) — these config keys already exist in System: `input_source` (enum: mic,line), `input_gain` (0–8), `output_source` (hp/amp/all), `mixer_mode` (dac/bypass/mix), `ch0_codecLvlOut`/`ch1_codecLvlOut` (0–33), `ch0_toStereo`/`ch1_toStereo` (dac/bypass/mix), `ng_config` (off/dual/ch0/ch1), `ch01_daisy`
- **Fader layout:** 3–5 vertical bars, each with two VU meters (input L/R or output L/R) on the right, like the Norns screenshot
- **Navigation:** Encoder scrolls fader selection; OK toggles value editing on the active fader; long-press? TBD

### Architecture needed

**1. Audio metering tap** — *partially done*
- SPManager now has `meteringEnabled`, `vuInL/R`, `vuOutL/R` (atomic<uint32_t>, scaled 0..1000)
- Input peak capture added in audio task (line 128 area)
- **TODO:** Output peak capture still missing — need to re-enable the commented-out `max` tracking in the output section (around line 260) and write to `vuOutL/R` when metering enabled

**2. UIMenu periodic refresh hook** — *not started*
- The 1 Hz panel-bar timer exists; need a per-page virtual `tick()` or a flag so the active page can request ~4 Hz redraw while playing/recording (fixes the frozen tape timecode issue too)
- Add `virtual void onTick()` to `UIMenuPage` base; `UIMenu::TaskFunction` calls it when `meteringEnabled` and `navState == PANEL_IN`

**3. New `UIMenuPageMix` class** — *not started*
- Replace `UIMenuPageMix.cpp`/`.hpp` entirely
- Items: each fader = name + level (0–33 or enum) + two VU meters drawn by `Display::DrawVUMeter` (already implemented, draws 0..1 float)
- Encoder: scroll fader list; OK on fader → `editing=true`; encoder in edit mode adjusts value via `SoundProcessorManager::SetConfigurationFromJSON()` per-tick (non-deferred, like System page)
- Deinit persists full config via existing `applyCurrent()` pattern
- VU meters read `SoundProcessorManager::GetVUPeak(channel)` → `DrawVUMeter(x, y, w, h, level/1000.0f)` (already exists, takes 0..1)

**4. Input source switch integration**
- The System page already has `input_source` enum (mic/line) and `input_gain` (0–8). Either expose those directly on the MIX page, or add a "Input" sub-row per fader.
- Codec APIs: `SetInputSource(0=mic/1=line)`, `SetInputGain(0..8)`, `SetOutputSource(0=hp/1=amp/2=all)`, `SetMixerMode(0=dac/1=bypass/2=mix)`, `SetOutputLevels(ch0, ch1)`

**5. Config persistence**
- Follow System page pattern: on `deinit()`, `applyCurrent()` overlays managed keys into full config JSON and calls `SetConfigurationFromJSON()` (persists to SPIFFS immediately). Codec changes applied live per-tick in `updateConfiguration()`.

### Files to touch

| File | Change |
|------|--------|
| `main/SPManager.cpp` | Output peak capture (re-enable max tracking in output section); definitions for new statics |
| `main/UIMenuPage.hpp` | Add `virtual void onTick() {}` |
| `main/UIMenu.cpp` | Call `onTick()` when `meteringEnabled && navState==PANEL_IN` (also fixes frozen tape timecode) |
| `main/menupages/UIMenuPageMix.cpp`/`.hpp` | Full rewrite as mixer page |
| `main/UIMenu.cpp` | Include new `UIMenuPageMix.hpp`, instantiate in `Init()` as `pages[PANEL_MIX]` |
| `doc/ui-menu-architecture.md` | Update MIX page documentation |
| `doc/modulation-system.md` | No change (modulation is separate) |

---

## Other open items (decided but not coded)

| Item | Decision |
|------|----------|
| HOME → SLEEP | Remove the row (not applicable on ESP32) |
| Gate `AAmt` row | Wire edit in `onEncoder`; add Accent Slot row (decide if `accentSlot` needs UI — currently only via JSON) |
| Sequencer long-press no-op | Leave as-is for now; decide later |
| MIDI scan dead branch | Delete unreachable `btnId==1 && longPress` in `SP_SCAN` |
| `BTN1_DOUBLE` | Keep enum (may need later) |
| LFO "Out: None" | Decide contract: allow −1 in `SetLFOCVSlot` or drop UI branch. Sequencers/gates allow −1; LFO rejects it. |
| Tape timecode frozen | Fix via `onTick()` hook (above) |
| Favorite API / WebUI active favorite | Separate REST work |

---

## Test harness

`tests/host/run_tests.sh` — compiles `ctagTempo`/`ctagSeq16`/`ctagGate16` against a minimal FreeRTOS shim (`tests/host/shim/freertos/`) and runs 43 assertions.

```bash
tests/host/run_tests.sh   # green, exit 0
```

Also compiles under `-fsanitize=address,undefined -fno-omit-frame-pointer` with zero sanitizer reports.

---

## How to continue

1. **Finish output peak capture** in SPManager audio task (line ~260, uncomment the `max` tracking and write to `vuOutL/R` when `meteringEnabled`)
2. **Add `onTick()` hook** to `UIMenuPage` and call it from `UIMenu::TaskFunction` when metering is enabled
3. **Write `UIMenuPageMix`** from scratch — use System page as the config pattern, Display VU API for meters, vertical fader layout
4. **Build, flash, test** on hardware — verify VU meters track input/output live, faders adjust levels and persist

The device is currently running the fix-pass firmware (`1db1daed`). Flash the new build with:

```bash
source /home/ubuntu/esp-idf/export.sh
python -m esptool --chip esp32 -b 460800 --port /dev/ttyUSB0 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x10000 build/ctag-tbd.bin
```

---

## Reference docs updated

- `AGENTS.md` — added host test command, documented MIDI-only architecture, slot-index safety
- `doc/modulation-system.md` — corrected swing direction, MIDI clock BPM derivation, tap averaging, transport gating, tempo persistence, VU metering design note, testing section
- `doc/ui-menu-architecture.md` — needs MIX page update (currently documents font test page)

---

## Repo state

```
feat/norns-ui-phase1 (HEAD → 1db1daed)  
  origin/feat/norns-ui-phase1 → 1db1daed (synced)  
  22 files changed in fix pass, 18 in gitignore cleanup
```

Untracked (deliberate): `.zcodeignore` (ZCode tool file).