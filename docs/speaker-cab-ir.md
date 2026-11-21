# Speaker cabinet IR on TBStrings (A1S, 44.1 kHz)

## What it is

A mono 512-tap FIR convolution of the TBStrings string-bus signal with the
Hesu 2x12 V30 cabinet impulse response, placed **after** the Rings limiter
and mono-summed to both buses (same topology as the rp2350 rack version).

- IR source: `/home/ubuntu/mi-rp2350/tools/Hesu 2x12 V30 - 440 center edge.wav`
  (44.1 kHz mono 24-bit, 1.83 s).
- Generated with the existing script:
  `python3 /home/ubuntu/mi-rp2350/tools/ir2array.py "<wav>" 512 44100`
  → `components/ctagSoundProcessor/helpers/speaker_ir_44100.h`.
  The script pre-delay-compacts from sample 1172, peak-normalizes and applies
  a 64-sample raised-cosine tail fade. 512 taps @ 44.1 kHz = **11.6 ms**.
- Param: `Cabinet IR` (`cab_mix`, 0 = bypass … 4095 = fully wet) in the
  mui spec and factory preset. Default 0.
- FIR state (`cab_ir_`, `cab_hist_`) is allocated in **internal DRAM** by the
  wrapper; PSRAM per-tap costs ~17 cycles on this core.

## Three bugs found during bring-up

1. **Makeup gain 4× too hot** — `2.5 / IR_RMS ≈ 1.06` slammed the post-cab
   `SoftLimit` into obvious "bitcrushed" distortion. Must use the rack's
   `kSpeakerConvGain = 0.25` on the peak-normalized float IR.
2. **Ring-wrap out-of-bounds read** — the first 8×-unrolled single loop
   checked the wrap *after* reading 8 taps, so a write pointer near the end
   read 4–7 floats past the 512-sample history → constant background noise
   (worst at k=511). Fixed by splitting the ring into two linear slices.
   A mirrored 2× history buffer was **not** viable: the extra 2 KB exhausted
   internal DRAM and `LinkEndpoint::Start()` aborted at boot (the ~24 kB
   contiguous link-UART rule).
3. **Time-reversed IR** — the circular delay line read history *forward*
   from the write pointer, pairing `h[1]` with the OLDEST sample. Fixed to
   `fir_dot_backward` (two backward linear slices), giving
   `h[0]·newest, h[1]·1-ago, …` — the rack's `h[k]·w[−k]` causal order.

Host regression tests (`tests/host/test_guitar_string_bank.cpp`):
- `speaker_cab_fir_wraps_without_artifacts` — impulse response is exactly
  `h[k]·0.25` at every write offset including the wrap (worst error 0).
- `speaker_cab_group_matches_scalar` — all block sizes agree bit-exactly.
- `tbstrings_full_chain_stays_bounded` — strings → chainsaw → limiter → cab
  with max damping + full cab wet stays finite and below full scale.

## Performance and the Xtensa ceiling

- Cab cost: **~64k cycles/block** (512 MACs × 32 samples).
- Audio task with cab on: **134k median / up to 158k max** (budget 174,150).
- `cpuOverload 0`, `i2sErr 0`, `diagLoopOver` static on the bench.

The FIR inner loop is `lsi` + `madd.s` at **~3 instructions per MAC** — the
Xtensa LX6 scalar FPU floor. Measured and **rejected**:

- 8 samples/tap (one IR load for 8 MACs): 8 accumulators spill to the stack.
- Linear delay line + 4-sample groups: register rotation keeps ~3 instr/MAC
  and the 512-float shift per block pushes the cost to 72k (worse than 64k).

Do not retry these. The real headroom options are:

1. **Half-rate cab** (recommended): the IR has no content above ~8 kHz
   (0.43 % at 8–12 kHz, 0.01 % above 12 kHz). Decimate to 22.05 kHz, convolve
   with a 256-tap IR (same 11.6 ms, every other coefficient), interpolate
   back. ~3× fewer MACs → cab ~22k. The rack already has the halfband
   coefficient tables (`kHalfbandDec` / `kHalfbandInt` in `rack_dist.h`).
2. **Core-0 helper**: removes 64k from the audio core, but internal DRAM is
   at the ceiling and a helper task needs a ~4 kB internal stack.

## Bench procedure

- App-only flash for firmware-only changes: `0x10000` with esptool.
- The `Cabinet IR` param is a live setting; use
  `/debug/setPluginParam {"id":"cab_mix","value":4095,"ch":0}`. Set it on the
  panel and STORE to persist — live edits do not survive a plugin reload.
- VU is peak-hold: do **not** use it for level diagnostics. Use
  `/debug/getAudioStats` (`audioCycles`, `cpuOverload`, `i2sErr*`,
  `diagLoopOver`, `diagStallWr`) and `/debug/getLoopHist {"len":64}`.
- After any `spiffs_push_file.py` run, move the `<file>.device-backup.jsn`
  files out of `spiffs_image/` — the `mui-` prefix makes
  SPManagerDataModel register duplicate plugins on the next boot.
