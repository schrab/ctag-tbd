# CTAG TBD — Spec

## G GOAL

Embedded Eurorack audio module (ESP32) with 50+ DSP plugins, dual-core audio, OLED UI, CV/trig I/O, Wi-Fi web UI.

## C CONSTRAINTS

- ESP32-D0WD-V3 @ 240 MHz, 16 MB flash, PSRAM.
- Audio block budget: 174,150 cycles per 32-sample block @ 44.1 kHz.
- No heap/VLA/blocking in audio Process().
- Plugins allocate from PSRAM.

## I INTERFACES

- ctagSoundProcessor plugin base (Process/Init).
- REST debug endpoints (/debug/*).
- SPIFFS param presets (mui/mp jsn).

## V INVARIANTS

V1: Audio task must not exceed 174,150 cycles/block average.
V2: Elements resonator master gain 0.125 (SoftLimit non-hard).
V3: Elements modal resonator ≤ 12 modes on single core (overload).
V5: Elements second-core — Part::Process (resonator+exciters+reverb) runs on a core-0 helper task, one 32-sample block per call.
V6: Elements second-core input — blow_in/strike_in captured at block start into the helper; helper never reads live input.
V7: Elements second-core latency — resonator output delayed one block (~0.73 ms); acceptable.
V8: Elements second-core state — patch snapshot copied into helper block; no mutex on part; core 1 writes patch only before queuing.
V9: Elements second-core fallback — single-core at 12 modes if helper task creation fails or deadlines missed.
V10: Elements second-core modes — restore set_resolution(52) first; step down to 32/24 if overloaded; 0.125 gain retained.
V11: Elements second-core reverb — reverb stays inside Part::Process on core 0.

## T TASKS

| id | status | task |
|---|---|---|
| T1 | . | Elements second-core offload (designs/elements-second-core.md) |
| T2 | . | Elements second-core: implement helper task, snapshot, debug endpoints, hardware gates |

## B BUGS

| id | date | cause | fix |
|---|---|---|---|
EOF