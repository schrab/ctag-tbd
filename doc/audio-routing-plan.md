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
Add 5 public static methods to the `Codec` class:
```cpp
static void SetInputSource(int sel);    // 0=IN1, 1=IN2, 2=IN1DIFF, 3=IN2DIFF
static void SetOutputSource(int sel);   // 0=OUT1, 1=OUT2, 2=OUTALL
static void SetMixerMode(int mode);     // 0=DACOUT, 1=SRCSELOUT, 2=MIXALL
static void SetAnalogBypass(bool on);
static void SetInputGain(int gain);     // 0-8
```

### 2. `components/drivers/codec.hpp`
Add the same 5 methods. SPManager.cpp includes this header for type-checking; the actual implementation comes from codec_bba.cpp (both define `CTAG::DRIVERS::Codec` with the same public API).

### 3. `components/drivers/codec_bba.cpp`
Implement the 5 methods delegating to the private `codec` instance:
```cpp
void Codec::SetInputSource(int sel) {
    static const insel_t map[] = {IN1, IN2, IN1DIFF, IN2DIFF};
    if (sel >= 0 && sel <= 3) codec.inputSelect(map[sel]);
}
void Codec::SetOutputSource(int sel) {
    static const outsel_t map[] = {OUT1, OUT2, OUTALL};
    if (sel >= 0 && sel <= 2) codec.outputSelect(map[sel]);
}
void Codec::SetMixerMode(int mode) {
    static const mixercontrol_t map[] = {DACOUT, SRCSELOUT, MIXALL};
    if (mode >= 0 && mode <= 2) codec.mixerSourceControl(map[mode]);
}
void Codec::SetAnalogBypass(bool on) { codec.analogBypass(on); }
void Codec::SetInputGain(int gain) { codec.setInputGain(gain); }
```

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
Add 5 new keys to `"configuration"`:
```json
"input_source": "line2",
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
| 9 | `input_source` | Input Source | enum | `line1,line2,line1_diff,line2_diff` | 0 | 3 | 1 |
| 10 | `input_gain` | Input Gain | int | — | 0 | 8 | 0 |
| 11 | `output_source` | Output Route | enum | `headphones,amp,all` | 0 | 2 | 2 |
| 12 | `mixer_mode` | Mixer Mode | enum | `dac,bypass,mix` | 0 | 2 | 0 |
| 13 | `oled_brightness` | OLED Brightness | int | — | 0 | 255 | 255 |

`applyCurrent()` works generically — iterates all registered items and overlays them on the full config JSON. No changes needed.

### 11. `main/SPManager.cpp`
Add handling in `updateConfiguration()` after the output level block (line ~543):

```cpp
// input source
string inSrc = model->GetConfigurationData("input_source");
if (inSrc == "line1")      DRIVERS::Codec::SetInputSource(0);
else if (inSrc == "line2") DRIVERS::Codec::SetInputSource(1);
else if (inSrc == "line1_diff") DRIVERS::Codec::SetInputSource(2);
else if (inSrc == "line2_diff") DRIVERS::Codec::SetInputSource(3);

// input gain
string inGain = model->GetConfigurationData("input_gain");
if (!inGain.empty()) DRIVERS::Codec::SetInputGain(std::stoi(inGain));

// output source
string outSrc = model->GetConfigurationData("output_source");
if (outSrc == "headphones") DRIVERS::Codec::SetOutputSource(0);
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

Need to add `#include "Display.hpp"` at the top of SPManager.cpp.

## Design Decisions
- `analogBypass` is covered by Mixer Mode = bypass (no separate UI toggle)
- `mixer_mode = dac` is the default (digital playback only, same as current behavior)
- `mixer_mode = bypass` routes analog input directly to output (no DSP)
- `mixer_mode = mix` blends DAC output + analog input
- OLED dim level is 0x4D (~30% of 0xFF) — configurable in the plan if needed
