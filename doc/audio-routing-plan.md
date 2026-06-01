# Audio Routing + OLED Brightness + Screensaver Plan

## Goals
1. Expose ES8388 codec input/output routing in the System settings page
2. Add OLED brightness control
3. Two-stage screensaver: dim at 30s, off at 60s

## Hardware Context
- ES8388 has two stereo input pairs: LIN1/RIN1 (LINE1) and LIN2/RIN2 (LINE2)
- Two stereo output pairs: LOUT1/ROUT1 (headphones) and LOUT2/ROUT2 (amp/speakers)
- SSD1309 OLED supports contrast 0x00–0xFF via command 0x81

## Files to Modify (11 files)

### 1. `components/drivers/codec_bba.hpp`
5 public static methods on the `Codec` class:
```cpp
static void SetInputSource(int sel);    // 0=IN1 (mic), 1=IN2 (line)
static void SetOutputSource(int sel);   // 0=OUT1 (hp), 1=OUT2 (amp), 2=OUTALL
static void SetMixerMode(int mode);     // 0=DAC, 1=BYPASS, 2=MIX
static void SetAnalogBypass(bool on);
static void SetInputGain(int gain);     // 0-8
```

### 2. `components/drivers/codec.hpp`
Declare the same 5 methods. On BBA platform (`CONFIG_TBD_PLATFORM_BBA`) they are declarations only (implemented in `codec_bba.cpp`). On other platforms they remain inline empty stubs.

### 3. `components/drivers/codec_bba.cpp`
Implement delegating to the private `codec` instance. `SetMixerMode` uses `analogBypass()` rather than raw `mixerSourceControl()`:
```cpp
void Codec::SetInputSource(int sel) {
    static const insel_t map[] = {IN1, IN2}; // _diff removed
    if (sel >= 0 && sel <= 1) codec.inputSelect(map[sel]);
}
void Codec::SetOutputSource(int sel) {
    static const outsel_t map[] = {OUT1, OUT2, OUTALL};
    if (sel >= 0 && sel <= 2) codec.outputSelect(map[sel]);
}
void Codec::SetMixerMode(int mode) {
    if (mode == 0) codec.analogBypass(false);        // DAC
    else if (mode == 1) codec.analogBypass(true);    // BYPASS
    else if (mode == 2) {                             // MIX
        codec.analogBypass(true);                    // set DACCONTROL16 + bypass
        codec.mixerSourceControl(true, true, 2, true, true, 2); // enable both
    }
}
void Codec::SetAnalogBypass(bool on) { codec.analogBypass(on); }
void Codec::SetInputGain(int gain) { codec.setInputGain(gain); }
```

`analogBypass()` sets DACCONTROL16 (mixer source select = MIXIN1 or MIXIN2 based on `_inSel`)
BEFORE setting DACCONTROL17/20 (line/DAC enable). This is why the bypass input correctly
follows the Input Source setting.

### 4. `main/Display.hpp`
Add:
```cpp
static void SetContrast(int val);
```

### 5. `main/Display.cpp`
Implement:
```cpp
void Display::SetContrast(int val) {
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    ssd1306_contrast(&I2CDisplay, val);
}
```

### 6. `main/UIMenu.hpp`
Replace screensaver constants:
```cpp
static constexpr int SCREENSAVER_DIM_TIMEOUT = 1500;   // 30s → dim to 30%
static constexpr int SCREENSAVER_SLEEP_TIMEOUT = 3000;  // 60s → off
enum DisplayState { AWAKE, DIMMED, ASLEEP };
static DisplayState displayState;
```
Remove old `static bool displayAsleep;` and `static constexpr int SCREENSAVER_TIMEOUT`.

### 7. `main/UIMenu.cpp`
Rewrite screensaver block in `TaskFunction()`:

```
gotEv? → if DIMMED: SetContrast(0xFF), state=AWAKE, reset timer
         if ASLEEP: Wake(), SetContrast(0xFF), state=AWAKE, reset timer
         else: reset timer, normal dispatch

no ev? → screensaverTimer++
         if state==AWAKE && timer >= SCREENSAVER_DIM_TIMEOUT:
             SetContrast(0x4D), state=DIMMED
         if state==DIMMED && timer >= SCREENSAVER_SLEEP_TIMEOUT:
             Sleep(), state=ASLEEP
```

The periodic refresh guard changes from `!displayAsleep` to `displayState != ASLEEP`.

### 8. `spiffs_image/data/spm-config.jsn`
Add 5 keys to `"configuration"`:
```json
"input_source": "line",
"input_gain": "0",
"output_source": "all",
"mixer_mode": "dac",
"oled_brightness": "255"
```

### 9. `main/menupages/UIMenuPageSystem.hpp`
Increase `MAX_ITEMS` from 16 to 21.

### 10. `main/menupages/UIMenuPageSystem.cpp`
Register 5 new config items in `parseConfig()` after the existing `ch1_codecLvlOut` block:

| # | id | name | type | options | min | max | default |
|---|-----|------|------|---------|-----|-----|---------|
| 9 | `input_source` | Input Source | enum | `mic,line` | 0 | 1 | 0 |
| 10 | `input_gain` | Input Gain | int | — | 0 | 8 | 0 |
| 11 | `output_source` | Output Route | enum | `hp,amp,all` | 0 | 2 | 0 |
| 12 | `mixer_mode` | Mixer Mode | enum | `dac,bypass,mix` | 0 | 2 | 0 |
| 13 | `oled_brightness` | OLED Brightness | int | — | 0 | 255 | 255 |

`applyCurrent()` works generically — iterates all registered items and overlays them on the full config JSON. No changes needed.

### 11. `main/SPManager.cpp`
Add handling in `updateConfiguration()` after the output level block (line ~543):

```cpp
// input source
string inSrc = model->GetConfigurationData("input_source");
if (inSrc == "mic" || inSrc == "line1")  DRIVERS::Codec::SetInputSource(0);
else if (inSrc == "line" || inSrc == "line2") DRIVERS::Codec::SetInputSource(1);

// input gain
string inGain = model->GetConfigurationData("input_gain");
if (!inGain.empty()) DRIVERS::Codec::SetInputGain(std::stoi(inGain));

// output source
string outSrc = model->GetConfigurationData("output_source");
if (outSrc == "headphones" || outSrc == "hp") DRIVERS::Codec::SetOutputSource(0);
else if (outSrc == "amp")   DRIVERS::Codec::SetOutputSource(1);
else if (outSrc == "all")   DRIVERS::Codec::SetOutputSource(2);

// mixer mode
string mixMode = model->GetConfigurationData("mixer_mode");
if (mixMode == "dac")    DRIVERS::Codec::SetMixerMode(0);
else if (mixMode == "bypass") DRIVERS::Codec::SetMixerMode(1);
else if (mixMode == "mix")    DRIVERS::Codec::SetMixerMode(2);

// OLED brightness
string oledBr = model->GetConfigurationData("oled_brightness");
if (!oledBr.empty()) DRIVERS::Display::SetContrast(std::stoi(oledBr));
```

Backward compatibility: old `"line1"`/`"line2"` strings in stored config still map correctly.
`"headphones"` also accepted for `output_source` (legacy name for `"hp"`).

## Design Decisions
- `analogBypass` is covered by Mixer Mode = bypass (no separate UI toggle)
- `mixer_mode = dac` is the default (digital playback only, same as current behavior)
- `mixer_mode = bypass` routes analog input directly to output (no DSP)
- `mixer_mode = mix` blends DAC output + analog input
- OLED dim level is 0x4D (~30% of 0xFF) — configurable in the plan if needed

## Implementation Notes

### Bypass routing must select the correct input pair
`SetMixerMode` calls `codec.analogBypass(true)` which reads `_inSel` and writes
`DACCONTROL16` to select MIXIN1 (LIN1 = mic) or MIXIN2 (LIN2 = line in).
Without this, `DACCONTROL16` stays at its init value (0x00 = MIXIN1), so bypass
always routes LIN1 regardless of the Input Source setting.

### OUT1/OUT2 power register values
`ES8388_DACPOWER` register 0x04:
| Value | Bits | Effect |
|-------|------|--------|
| 0x3C | 00xx11xx | Both LOUT1/ROUT1 + LOUT2/ROUT2 powered |
| 0x0C | 00xx00xx | Only LOUT1/ROUT1 (headphones) powered |
| 0x30 | 00xx11xx | Only LOUT2/ROUT2 (amp line out) powered |

Note: bits are active-high enables (not power-down). The initial driver had these
swapped (0x30 for OUT1, 0x0C for OUT2), which caused "hp" to enable the amp and
"amp" to enable headphones.

### `codec.hpp` inline stubs must not shadow real implementations
`SPManager.cpp` includes `codec.hpp`. That header had inline empty bodies for the
routing functions (`static void SetMixerMode(int mode) {}`), which the compiler
inlined at the call site. The real implementations in `codec_bba.cpp` were never
reached. Fixed by guarding the inline stubs with `#ifndef CONFIG_TBD_PLATFORM_BBA`.

### Deferred items and double-write avoidance
- Non-deferred items (levels, softclip, etc.) are written to the codec on every
  encoder scroll (`applyCurrent(false)`).
- On OK/BACK commit, only deferred items are written (`applyCurrent(true, true)`)
  to avoid re-writing non-deferred items that were already applied.
