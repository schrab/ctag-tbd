# UI Input: ADC Buttons + Encoder

## Overview

Two tactile buttons (SW1, SW2) on a single ADC pin with a resistive voltage divider. A rotary encoder on two dedicated GPIOs via PCNT. No GPIO-button ISRs — the ADC is polled at 10ms intervals from a FreeRTOS task.

## Hardware Circuit

```
3.3V ── [10k] ──┬── GPIO36 (ADC1_CH0)
                │
               SW1 ── [10k] ── GND
                │
               SW2 ── [4.7k] ── GND
```

| State | Equivalent resistance to GND | Calculated ADC | Measured ADC |
|-------|------------------------------|----------------|--------------|
| None  | ∞ (open)                     | 4095           | 4095         |
| SW1   | 10k                          | 2048           | ~1850-1950   |
| SW2   | 4.7k                         | 1318           | ~1100-1200   |
| Both  | 10k ∥ 4.7k = 3.2k           | 992            | ~800-900     |

ADC: 12-bit, 11dB attenuation (`ADC_WIDTH_BIT_12` + `ADC_ATTEN_DB_11`). Measured values are lower than calculated due to diode protection and pad capacitance on the ESP32.

**GPIO0 is I2S MCLK — do not use for buttons.** The original upstream design used GPIO0 as BTN2, but that kills the MCLK signal and stops all audio.

## ADC Detection (`UserInput.cpp`)

4-sample averaging per poll, 20ms debounce, state machine with hysteresis:

```cpp
case BTN_NONE:
    if (adc < 600)  return BTN_SW1;    // direct GND (fallback, unmodded)  
    if (adc < 950)  return BTN_BOTH;
    if (adc < 1540) return BTN_SW2;
    if (adc < 2600) return BTN_SW1;
    return BTN_NONE;
```

Hysteresis thresholds for leaving each state are wider to prevent chatter. See `UserInput.cpp:41-66` for the full `readADCState()`.

## Button Event Mapping

| Physical | ADC state | Event emitted | UIMenu dispatch | Role |
|----------|-----------|---------------|-----------------|------|
| SW1 (short) | BTN_SW1→NONE | `BTN2_SHORT` | `onButton(2, false)` | OK |
| SW1 (long >500ms) | BTN_SW1→NONE | `BTN2_LONG` | `onButton(2, true)` | MOD |
| SW2 (short) | BTN_SW2→NONE | `BTN1_SHORT` | `onBack()` | BACK |
| SW2 (long >500ms) | BTN_SW2→NONE | `BTN1_LONG` | `onBack()` | BACK |
| Both → None | BTN_BOTH→NONE | (none) | — | Ignored |

SW1 has OK/MOD semantics (matches upstream BTN2). SW2 has BACK semantics (matches upstream BTN1_DOUBLE without the 300ms delay).

Both buttons held simultaneously produces no events — this prevents accidental triggering when pressing SW1 then adding SW2, or vice versa.

## Key Design Differences from Upstream

| Feature | Upstream (single GPIO button) | This build (ADC buttons) |
|---------|------------------------------|--------------------------|
| Button count | 1 (GPIO36, with double-click) | 2 (ADC1_CH0, voltage divider) |
| OK action | Short tap, 300ms delay (pendingShort) | Instant (SW1 release → BTN2_SHORT) |
| BACK action | Double-tap within 300ms | SW2 press → BTN1_SHORT (instant) |
| MOD action | Long press >500ms | SW1 long press >500ms |
| Stale events on mode transition | Required skipEncoders / pendingShort suppression | Not possible (no timing race) |
| `BTN1_DOUBLE` event | Emitted by input task | Never emitted (kept in enum for ABI) |
| `PeekEvent` | Not used in dispatch | Available but unused |

## Encoder

| Parameter | Value |
|-----------|-------|
| GPIOs | A = GPIO5, B = GPIO23 (swapped relative to upstream) |
| PCNT unit | 0 |
| Count mode | INC on rising A while B=0, DEC on rising A while B=1 |
| Glitch filter | Enabled, val=200 (~2.5µs) for mechanical bounce |
| Poll rate | 1kHz (every inputTask iteration) |
| ReadDelta() | Polls counter, clears, returns `count/2` (halves PCNT edge count to detent steps) |

**Pin assignment note:** Upstream has ENC_A=GPIO23, ENC_B=GPIO5. This build swaps them (ENC_A=GPIO5, ENC_B=GPIO23) to match physical wiring — the `pulse_gpio_num` in PCNT config therefore receives the swapped signal, reversing CW/CCW count direction relative to upstream.

## Event Queue

- Fixed-size FreeRTOS queue with 64 slots
- Encoder events: `InputEvent::ENC_DELTA` with `int16_t delta`
- Button events: `InputEvent::BTN1_SHORT`, `BTN1_LONG`, `BTN2_SHORT`, `BTN2_LONG`
- `InputEvent::BTN1_DOUBLE = 5` is defined in the enum but never generated (kept for ABI compatibility)
- `UserInput::GetEvent(ev, timeoutMs)` blocks up to timeout
- `UserInput::PeekEvent(ev)` non-blocking peek (available but unused in current dispatch)

## Task

- `inputTask` runs on Core 0 at `idle+3` priority, 2048 byte stack
- Created by `UserInput::Init()`, called from `UIMenu::TaskFunction` (post task creation) to avoid DRAM fragmentation
- 10ms poll period (`vTaskDelay(pdMS_TO_TICKS(10))`)
- Buttons use 4-sample averaging + 20ms debounce — 40ms total latency before a button event fires

## File Locations

| File | Role |
|------|------|
| `main/UserInput.cpp` | ADC init, readADCState(), inputTask, event queue API |
| `main/UserInput.hpp` | InputEvent enum, UserInput class (Init/EnableISR/GetEvent/PeekEvent) |
| `main/UIMenu.cpp` | Event dispatch (ROOT/PANEL_IN state machine) |
| `main/UIMenu.hpp` | NavState enum, Panel enum |
| `main/menupages/UIMenuPageMod.cpp` | Dirty-flag skipEncoders removal |
| `components/drivers/encoder.cpp` | PCNT encoder driver |
