# Radio-free DSP test — Elements helper A/B results (2026-10-26)

Branch `test/radio-free-dsp`. Question: with WiFi/REST and BLE MIDI never
initialized (`TBD_RADIO_FREE=1`), can the Elements second-core helper sustain
52 modes? Protocol: pinned `elementsHelperModeOverride` **before** plugin
load (the pin is only honored while the helper is alive), 40–60 s of
`/debug/getAudioStats` @2 Hz over the STX/ETX UART0 debug channel. Budget:
174,150 cycles/block (240 MHz, 32 samples @ 44.1 kHz).

## Results (helper render cycles, core 0)

| Configuration                 | 52 modes median (min–max)   | % of budget | auto ladder |
|-------------------------------|-----------------------------|-------------|-------------|
| Baseline (radios on)          | 234,286 (219k–293k), n=98   | 135%        | collapses to 12 |
| Radio-free, reverb in PSRAM   | 238,942 (224k–310k), n=91   | 137%        | collapses to 12 |
| Radio-free, reverb in DRAM    | 224,448 (214k–250k), n=70   | 129%        | collapses to 12 |
| Radio-free, pinned 32         | 202,262                     | 116%        | — |
| Radio-free, pinned 24         | 186,694                     | 107%        | — |

Single-core 12-mode fallback (core 1, whole audio-task span `audioCycles`):
baseline median 175,480 (100.8% — overload flicker) vs radio-free 160,637
(92%). PERF `Elements` avg at 12-mode fallback: 123.6k cyc (PSRAM reverb) →
109.7k cyc (internal reverb).

## Conclusions

1. **Radio contention was NOT why 52 modes collapse.** Helper render cost is
   unchanged within noise with radios fully removed. The V9 ladder will
   always settle at the 12-mode single-core fallback on this chip.
2. **The render decomposes as ~125k fixed + ~1.9k cycles per mode per block**
   (helper meter, internal-reverb build). The fixed part (exciter, bowed
   string machinery, reverb, output stages) is the dominant cost and does not
   scale with mode count. The documented "single-core 12 modes ≈ 112 µs" is
   stale: the real 12-mode Part render is ~110k cycles (~460 µs).
3. **Reverb buffer in internal DRAM saves ~14.5k cycles/block** (~6%).
   Radio-free frees enough DRAM that the 64 KB buffer fits internal; the
   alloc now prefers internal with PSRAM fallback.
4. **Radio-free is still a large win, just not for Elements resolution:**
   internal DRAM free after boot 57.8 KB → 192.3 KB (largest block 53.2 KB →
   110.6 KB), app image −825 KB (3.61 MB → 2.78 MB, NimBLE/WiFi/REST call
   trees garbage-collected), 12-mode fallback 100.8% → 92% of budget,
   faster boot (no WiFi retry/coex calibration), INT_WDT workaround
   unnecessary.

## Implications

- Full-resolution (52-mode) Elements needs the mode bank to shrink ~2.5–3× —
  exactly the ESP32-S3 LX7 128-bit SIMD case (52 = 13 × 4 lanes, SoA across
  SVFs). At 2× the total lands ~175k (knife's edge); at 2.5–3× it fits with
  margin. The ~125k fixed floor remains: on S3 at the same 240 MHz it alone
  consumes 72% of the budget.
- For the planned ESP32-C3 UI/connectivity split, radio-free is the correct
  DSP-side end state regardless of the Elements outcome.
- Known-issue follow-up: fallback `audioCycles` spikes to ~244–258k in both
  builds — worth a dedicated look at exciter/PSRAM stalls under real input.

## Build/test artifacts

- `/tmp/ctag-baseline.bin`, `/tmp/ctag-radiofree.bin`,
  `/tmp/ctag-radiofree-int.bin` (flashed)
- Measurement JSONs: `/tmp/rf_baseline.json`, `/tmp/rf_radiofree.json`,
  `/tmp/rf_32.json`, `/tmp/rf_24.json`, `/tmp/rf_52int.json`
- Scripts: `/tmp/rf_ab.py` (A/B), `/tmp/rf_sustain.py` (pinned-N)

## Bugs found on the way

- `debug_ui` task (4 KB) overflowed and rebooted the device whenever
  `/debug/setActivePlugin` ran the full plugin delete+create chain — fixed to
  8 KB in this branch. This also silently corrupted earlier measurement
  attempts (post-reboot boots loaded the persisted plugin and self-collapsed,
  masquerading as fresh data).
- Loading Elements over a *running* Elements instance is the heavy path that
  triggers the overflow; boot-time creation is fine.
