// Host validation of the ported Beads DSP core used by the Mbeads plugin.
//
// The core is exercised exactly the way the TBD audio task will call it:
// 44.1kHz host sample rate, fixed 32-sample blocks, separate input and output
// frame buffers. No hardware, ESP-IDF or PSRAM is involved.
//
// Build and run with tests/host/run_mbeads_tests.sh (exit code 0 = pass).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "beads/dsp/processor.h"
#include "beads/dsp/delay_line.h"
#include "beads/dsp/trigger_generator.h"

namespace {

const size_t kBlockSize = 32;
const float kHostSampleRate = 44100.0f;
const float kDigitalEngineRate = 44100.0f;
const float kVintageEngineRate = 22050.0f;

// 3 x 262,144 bytes of delay memory + 32,768 x uint16_t reverb memory, the
// same sizes the plugin will allocate from PSRAM.
const size_t kDelayBankBytes = 262144;
const size_t kDelayBanks = 3;
const size_t kReverbSamples = 32768;

const double kTwoPi = 6.283185307179586476925286766559;

// RMS floor for the wet-only checks (no reverb, dry_wet = 1). The fully wet
// Digital/Vintage runs measure ~0.13, so this only catches a silent/broken
// engine. The default-patch check at time 0.5 is lower (the grain read head
// starts in unfilled buffer), but still well above the floor.
const double kWetRmsFloor = 0.01;

// -ffast-math implies -ffinite-math-only, so std::isfinite() may be folded
// away by the optimizer. Inspect the IEEE-754 exponent bits instead.
bool IsFinite(float x) {
  uint32_t bits = 0;
  std::memcpy(&bits, &x, sizeof(bits));
  return (bits & 0x7f800000u) != 0x7f800000u;
}

struct BlockStats {
  bool finite = true;
  double sum_squares = 0.0;
  size_t samples = 0;

  double rms() const {
    return samples ? std::sqrt(sum_squares / static_cast<double>(samples)) : 0.0;
  }

  void Add(const beads::FloatFrame* frames, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      finite &= IsFinite(frames[i].l) && IsFinite(frames[i].r);
      sum_squares += static_cast<double>(frames[i].l) * frames[i].l +
                     static_cast<double>(frames[i].r) * frames[i].r;
      samples += 2;
    }
  }
};

bool Expect(bool condition, const char* what) {
  std::printf("  [%s] %s\n", condition ? " ok " : "FAIL", what);
  return condition;
}

// Runs num_blocks blocks of a 220Hz sine (amplitude 0.25) through the
// processor. Samples are only accumulated into the RMS statistics from block
// first_block_for_stats onwards, so a settling period can be skipped.
// When in_place is true the same frame buffer is used for input and output,
// which must be safe because the dry signal is captured before the wet engine
// runs.
// hold_gate supplies a held-high seed gate: without it the density clock is
// gated off and no grains are produced at all.
// time_base is the grain read position (0 = newest audio in the buffer).
// size_base above ~0.99 ramps the engine into delay mode (1.5 s transition)
// only when SetDelayModeEnabled(true) has been called; the v1 plugin keeps it
// disabled.
BlockStats RunBlocks(
    beads::Processor* processor,
    int num_blocks,
    double* phase,
    float dry_wet,
    bool freeze_all,
    bool toggle_seed,
    int first_block_for_stats,
    bool in_place = false,
    float density_base = 0.75f,
    bool hold_gate = false,
    float time_base = 0.5f,
    float reverb_amount = 0.25f,
    bool seed_button_on = false,
    float size_base = 0.5f) {
  beads::Parameters parameters;
  parameters.time.base = time_base;
  parameters.time.attenuverter = 0.0f;
  parameters.time.has_cv = false;
  parameters.time.cv = 0.0f;

  parameters.size.base = size_base;
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

  // 0.75: above the "manual taps" range, so the trigger generator free-runs
  // (when the seed gate is high) and the granular engine is genuinely
  // exercised.
  beads::ModulatedParameter density;
  density.base = density_base;
  density.cv = 0.0f;

  beads::Processor::MixParameters mix;
  mix.feedback = 0.0f;
  mix.dry_wet = dry_wet;
  mix.reverb_amount = reverb_amount;
  mix.output_gate = false;

  BlockStats stats;
  const double phase_increment = kTwoPi * 220.0 / kHostSampleRate;

  for (int block = 0; block < num_blocks; ++block) {
    beads::FloatFrame in[kBlockSize];
    beads::FloatFrame out_storage[kBlockSize];
    beads::FloatFrame* out = in_place ? in : out_storage;
    bool freeze[kBlockSize];
    stmlib::GateFlags seed_input[kBlockSize];

    // seed_button_on emulates the default patch (`seed = 1`): the seed button
    // merges a held-high gate into the trigger generator.
    const beads::SeedButtonState seed_state = seed_button_on
        ? beads::SEED_BUTTON_STATE_ON
        : ((toggle_seed && (block & 1))
               ? beads::SEED_BUTTON_STATE_OFF
               : (toggle_seed ? beads::SEED_BUTTON_STATE_ON
                              : beads::SEED_BUTTON_STATE_OFF));

    for (size_t i = 0; i < kBlockSize; ++i) {
      const float sample = 0.25f * static_cast<float>(std::sin(*phase));
      *phase += phase_increment;
      if (*phase > kTwoPi) {
        *phase -= kTwoPi;
      }
      in[i].l = sample;
      in[i].r = sample;
      if (!in_place) {
        out[i].l = 0.0f;
        out[i].r = 0.0f;
      }
      freeze[i] = freeze_all;
      seed_input[i] = hold_gate ? stmlib::GATE_FLAG_HIGH : stmlib::GATE_FLAG_LOW;
    }

    processor->Process(
        seed_state,
        seed_input,
        freeze,
        density,
        parameters,
        mix,
        in,
        out,
        kBlockSize);

    const bool collect = block >= first_block_for_stats;
    for (size_t i = 0; i < kBlockSize; ++i) {
      if (!IsFinite(out[i].l) || !IsFinite(out[i].r)) {
        stats.finite = false;
      }
      if (collect) {
        stats.sum_squares +=
            static_cast<double>(out[i].l) * static_cast<double>(out[i].l);
        stats.sum_squares +=
            static_cast<double>(out[i].r) * static_cast<double>(out[i].r);
        stats.samples += 2;
      }
    }
  }

  return stats;
}

// The trigger generator is stepped once per host sample: Processor::Process()
// renders it over the whole 32-sample block and the half-rate media downsample
// its output (triggers are preserved across the ratio group). Initialising it
// with the engine rate would therefore run the grain clock at twice the
// intended rate on Vintage/Clean/Dirty. This pins down the rate the generator
// is clocked at.
int CountTriggerRate(float init_sample_rate) {
  beads::TriggerGenerator generator;
  generator.Init(init_sample_rate);

  stmlib::GateFlags held_gate[kBlockSize];
  for (size_t i = 0; i < kBlockSize; ++i) {
    held_gate[i] = stmlib::GATE_FLAG_HIGH;
  }

  const int one_second_of_blocks =
      static_cast<int>(kHostSampleRate) / static_cast<int>(kBlockSize);
  int triggers = 0;
  for (int block = 0; block < one_second_of_blocks; ++block) {
    beads::TriggerGenerator::Output out[kBlockSize];
    generator.Render(
        beads::SEED_BUTTON_STATE_OFF,
        held_gate,
        0.0f,  // density 0: deterministic 130.81278 Hz pulse train
        0.0f,
        0.0f,
        out,
        kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
      triggers += out[i].trigger ? 1 : 0;
    }
  }
  return triggers;
}

// Counts the grains that reach the output through the whole FX chain, using
// MixParameters::output_gate: with the gate enabled the right channel is
// forced to -0.8f while the trigger generator's gate is high, so rising edges
// there are exactly the grains the audio path will render. This is an
// integration check of Processor::Process (trigger generator -> gate_out_ ->
// output stage), not just of the generator in isolation.
int CountOutputGates(
    beads::Processor* processor,
    int num_blocks,
    float density_base) {
  beads::Parameters parameters;
  parameters.time.base = 0.02f;
  parameters.time.attenuverter = 0.0f;
  parameters.time.has_cv = false;
  parameters.time.cv = 0.0f;

  parameters.size.base = 0.5f;
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
  density.base = density_base;
  density.cv = 0.0f;

  beads::Processor::MixParameters mix;
  mix.feedback = 0.0f;
  mix.dry_wet = 0.0f;
  mix.reverb_amount = 0.0f;
  mix.output_gate = true;

  stmlib::GateFlags held_gate[kBlockSize];
  bool freeze[kBlockSize];
  for (size_t i = 0; i < kBlockSize; ++i) {
    held_gate[i] = stmlib::GATE_FLAG_HIGH;
    freeze[i] = false;
  }

  int edges = 0;
  bool previous_high = false;
  for (int block = 0; block < num_blocks; ++block) {
    beads::FloatFrame in[kBlockSize];
    beads::FloatFrame out[kBlockSize];
    for (size_t i = 0; i < kBlockSize; ++i) {
      in[i].l = 0.0f;
      in[i].r = 0.0f;
    }

    processor->Process(
        beads::SEED_BUTTON_STATE_OFF,
        held_gate,
        freeze,
        density,
        parameters,
        mix,
        in,
        out,
        kBlockSize);

    for (size_t i = 0; i < kBlockSize; ++i) {
      const bool high = out[i].r < -0.4f;
      if (high && !previous_high) {
        ++edges;
      }
      previous_high = high;
    }
  }
  return edges;
}

}  // namespace

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

  const int num_blocks = 2000;
  for (int b = 0; b < num_blocks; ++b) {
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

BlockStats RunSingleDense16(
    uint8_t* buffers[],
    uint16_t* reverb_buffer) {
  beads::Processor processor;
  processor.Init(buffers, reverb_buffer, kHostSampleRate);
  processor.Configure(beads::Processor::MEDIA_DIGITAL, false);
  processor.SetMaxActiveGrains(16);
  processor.SetHelperMaxActiveGrains(0);

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

  BlockStats single_stats;
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

    processor.Process(
        beads::SEED_BUTTON_STATE_OFF,
        held_gate,
        freeze,
        density,
        parameters,
        mix,
        in,
        out,
        kBlockSize,
        nullptr,
        nullptr);

    if (b >= 200) {
      single_stats.Add(out, kBlockSize);
    }
  }

  return single_stats;
}

int main() {
  bool ok = true;

  uint8_t* buffers[kDelayBanks];
  for (size_t i = 0; i < kDelayBanks; ++i) {
    buffers[i] = static_cast<uint8_t*>(std::malloc(kDelayBankBytes));
    if (!buffers[i]) {
      std::printf("FAIL: could not allocate delay bank %zu\n", i);
      return 1;
    }
    // Zero the memory: the test must be deterministic, and unlike the plugin
    // we do not want to depend on reuse of a previous allocation.
    std::memset(buffers[i], 0, kDelayBankBytes);
  }

  uint16_t* reverb_buffer = static_cast<uint16_t*>(
      std::malloc(kReverbSamples * sizeof(uint16_t)));
  if (!reverb_buffer) {
    std::printf("FAIL: could not allocate reverb buffer\n");
    return 1;
  }
  std::memset(reverb_buffer, 0, kReverbSamples * sizeof(uint16_t));

  ok &= TestSnapshotReads(buffers);

  beads::Processor processor;
  processor.Init(buffers, reverb_buffer, kHostSampleRate);
  std::printf("sizeof(beads::Processor) = %zu bytes\n",
              sizeof(beads::Processor));
  std::printf("extension memory = %zu bytes (delay) + %zu bytes (reverb)\n",
              kDelayBanks * kDelayBankBytes,
              kReverbSamples * sizeof(uint16_t));
  // ~30.7KB on a 64-bit host; keep a loose bound so object bloat is noticed.
  ok &= Expect(sizeof(beads::Processor) <= 48 * 1024,
               "sizeof(beads::Processor) stays within budget");

  // --- Trigger generator rate ---------------------------------------------
  // The generator is stepped at the host rate (see Processor::Configure), so
  // that is the rate it must be initialised with.
  std::printf("Trigger generator:\n");
  const int triggers_at_host_rate = CountTriggerRate(kHostSampleRate);
  const int triggers_at_engine_rate = CountTriggerRate(kVintageEngineRate);
  std::printf("  %d triggers/s at 44.1k init, %d at 22.05k init\n",
              triggers_at_host_rate, triggers_at_engine_rate);
  ok &= Expect(triggers_at_host_rate >= 125 && triggers_at_host_rate <= 135,
               "~130.8 triggers/s at the rate it is stepped at");
  const double trigger_rate_ratio =
      static_cast<double>(triggers_at_engine_rate) /
      static_cast<double>(triggers_at_host_rate);
  ok &= Expect(trigger_rate_ratio > 1.95 && trigger_rate_ratio < 2.05,
               "half-rate init doubles the grain rate");

  // --- Output gate integration ---------------------------------------------
  // Density 0 makes the pulse train deterministic (130.81278Hz), and the
  // trigger generator is stepped at the host rate for every media, so both
  // media must deliver the same number of grains to the output.
  std::printf("Output gate (integration):\n");
  const int one_second_of_blocks =
      static_cast<int>(kHostSampleRate) / static_cast<int>(kBlockSize);
  processor.Configure(beads::Processor::MEDIA_DIGITAL, false);
  const int digital_gates = CountOutputGates(&processor, one_second_of_blocks, 0.0f);
  processor.Configure(beads::Processor::MEDIA_VINTAGE_DIGITAL, false);
  const int vintage_gates = CountOutputGates(&processor, one_second_of_blocks, 0.0f);
  std::printf("  %d grains/s digital, %d grains/s vintage\n",
              digital_gates, vintage_gates);
  ok &= Expect(digital_gates >= 125 && digital_gates <= 135,
               "Digital delivers ~130 grains/s through the output gate");
  ok &= Expect(vintage_gates >= 125 && vintage_gates <= 135,
               "Vintage delivers ~130 grains/s through the output gate");
  ok &= Expect(std::abs(digital_gates - vintage_gates) <= 2,
               "Digital and Vintage trigger the same grain rate");

  double phase = 0.0;

  // --- Digital (44.1kHz engine, 16-bit, ratio 1) ---------------------------
  std::printf("Digital:\n");
  processor.Configure(beads::Processor::MEDIA_DIGITAL, false);
  ok &= Expect(processor.media() == beads::Processor::MEDIA_DIGITAL,
               "media is MEDIA_DIGITAL");
  ok &= Expect(processor.sample_rate() == kDigitalEngineRate,
               "sample_rate() == 44100");
  ok &= Expect(processor.ratio() == 1, "ratio() == 1");

  BlockStats digital = RunBlocks(
      &processor, 2000, &phase, 0.5f, false, false, 100);
  ok &= Expect(digital.finite, "2000 blocks produce finite output");
  ok &= Expect(digital.rms() > 0.0, "output is non-silent after 100 blocks");
  std::printf("  rms = %.6f\n", digital.rms());

  // Fully wet with a held seed gate and density 0, so the pulse train
  // free-runs deterministically, and with the grain read head close to the
  // write head (time 0.02) so the grains actually read the audio already in the
  // delay line. This proves the granular engine - not just the dry path or the
  // reverb tail - makes sound.
  BlockStats digital_wet = RunBlocks(
      &processor, 500, &phase, 1.0f, false, false, 0, false, 0.0f, true, 0.02f,
      0.0f);
  ok &= Expect(digital_wet.finite, "fully wet output is finite");
  ok &= Expect(digital_wet.rms() > kWetRmsFloor,
               "granular engine alone is audible (no reverb, fully wet)");
  std::printf("  wet rms = %.6f\n", digital_wet.rms());

  // The shipped default patch's grain settings must be audible on a fresh
  // load: density 3072 (0.75, above the manual-tap zone) and seed 1 (the seed
  // button holds the density clock's gate high). Without the seed gate the
  // clock never runs. time stays at the shipped 0.5, so the run has to fill
  // the 2.18 s read position before the grains read real audio.
  std::printf(
      "Default patch grain settings (density 3072, seed 1, time 0.5):\n");
  BlockStats default_patch = RunBlocks(
      &processor, 5000, &phase, 1.0f, false, false, 400, false,
      3072.0f / 4095.0f, false, 0.5f, 0.0f, true);
  ok &= Expect(default_patch.finite, "default patch output is finite");
  ok &= Expect(default_patch.rms() > kWetRmsFloor,
               "default patch grain settings are audible (wet only, no reverb)");
  std::printf("  default patch wet rms = %.6f\n", default_patch.rms());

  // In-place processing (in == out) must stay correct: the dry signal is
  // captured before the wet engine writes the output.
  BlockStats in_place = RunBlocks(
      &processor, 50, &phase, 0.5f, false, false, 0, true);
  ok &= Expect(in_place.finite, "in-place (in == out) output is finite");
  ok &= Expect(in_place.rms() > 0.0, "in-place output is non-silent");
  std::printf("  in-place rms = %.6f\n", in_place.rms());

  // --- Delay mode (Size above ~0.99, disabled in the v1 plugin) ------------
  // The DSP core still implements the 8-tap multitap delay; the plugin keeps
  // it disabled because it exceeds the TBD audio-task budget. Enable it here
  // to keep the core path covered by the host test.
  // Size at maximum ramps the engine from grains to the 8-tap multitap delay:
  // the transition takes 1.5 s and the wet path fades in over 0.75 s. The
  // ramp-in run is fully wet (dry_wet = 1) with no reverb and no seed gate, so
  // a non-silent result proves the delay wet path itself, not the dry signal or
  // a grain tail. 3000 blocks is ~2.18 s, enough to cross the full transition.
  std::printf("Delay mode transition (DSP core):\n");
  processor.Configure(beads::Processor::MEDIA_DIGITAL, false);
  processor.SetDelayModeEnabled(true);
  BlockStats delay_in = RunBlocks(
      &processor, 3000, &phase, 1.0f, false, false, 2600, false, 0.75f,
      false, 0.1f, 0.0f, false, 1.0f);
  ok &= Expect(delay_in.finite, "delay mode output is finite");
  ok &= Expect(delay_in.rms() > 0.001, "delay mode wet path is audible");
  std::printf("  delay mode rms = %.6f\n", delay_in.rms());

  // Ramp Size back down: the engine returns to grains once the transition
  // crosses zero (~block 930) and the granular wet path fades back in over the
  // next 0.75 s. The held seed gate is re-enabled for this run only: without it
  // the density clock is gated off and the granular engine (unlike the delay)
  // would stay silent. Statistics start at block 1000 so they cover that return
  // transition and the settled granular output.
  BlockStats delay_out = RunBlocks(
      &processor, 3000, &phase, 1.0f, false, false, 1000, false, 0.75f,
      true, 0.1f, 0.0f, false, 0.5f);
  ok &= Expect(delay_out.finite, "delay-to-granular return is finite");
  ok &= Expect(delay_out.rms() > 0.001,
               "delay-to-granular return is non-silent");
  std::printf("  return rms = %.6f\n", delay_out.rms());

  // v1 plugin behavior: delay mode disabled, so Size = 1.0 must stay in the
  // granular engine and still produce audible output. This is the regression
  // guard for the Size-dependent CPU fix: the top of the range no longer jumps
  // into the expensive multitap delay.
  std::printf("Size maximum with delay mode disabled:\n");
  processor.SetDelayModeEnabled(false);
  processor.Configure(beads::Processor::MEDIA_DIGITAL, false);
  BlockStats max_size = RunBlocks(
      &processor, 800, &phase, 1.0f, false, false, 400, false, 0.75f,
      true, 0.1f, 0.0f, false, 1.0f);
  ok &= Expect(max_size.finite, "max Size output is finite");
  ok &= Expect(max_size.rms() > 0.001,
               "max Size stays granular and is audible");
  std::printf("  max Size granular rms = %.6f\n", max_size.rms());

  // The plugin caps concurrent grains so long Size / dense Density settings
  // cannot exceed the audio-task budget. Verify the cap at a deliberately
  // dense setting (Density 0 = fastest trigger rate, Size 1.0 = 4 s grains).
  std::printf("Grain cap:\n");
  processor.SetMaxActiveGrains(4);
  BlockStats capped = RunBlocks(
      &processor, 1200, &phase, 1.0f, false, false, 0, false, 0.0f,
      true, 0.1f, 0.0f, false, 1.0f);
  ok &= Expect(capped.finite, "capped dense output is finite");
  ok &= Expect(processor.ActiveGrains() <= 4,
               "active grains stay within the cap");
  std::printf("  active grains = %d\n", processor.ActiveGrains());
  processor.SetMaxActiveGrains(32);

  // --- Vintage digital (22.05kHz engine, 12-bit, ratio 2) ------------------
  std::printf("Vintage digital:\n");
  processor.Configure(beads::Processor::MEDIA_VINTAGE_DIGITAL, false);
  ok &= Expect(processor.media() == beads::Processor::MEDIA_VINTAGE_DIGITAL,
               "media is MEDIA_VINTAGE_DIGITAL");
  ok &= Expect(processor.sample_rate() == kVintageEngineRate,
               "sample_rate() == 22050");
  ok &= Expect(processor.ratio() == 2, "ratio() == 2");

  BlockStats vintage = RunBlocks(
      &processor, 500, &phase, 0.5f, false, false, 0);
  ok &= Expect(vintage.finite, "500 blocks produce finite output");
  ok &= Expect(vintage.rms() > 0.0, "output is non-silent");
  std::printf("  rms = %.6f\n", vintage.rms());

  // Fully wet Vintage: covers the half-rate granular path (sample rate
  // conversion + downsampled triggers) with the read head near the write head.
  BlockStats vintage_wet = RunBlocks(
      &processor, 200, &phase, 1.0f, false, false, 0, false, 0.0f, true, 0.02f,
      0.0f);
  ok &= Expect(vintage_wet.finite, "fully wet Vintage output is finite");
  ok &= Expect(vintage_wet.rms() > kWetRmsFloor,
               "Vintage half-rate granular engine alone is audible");
  std::printf("  wet rms = %.6f\n", vintage_wet.rms());

  // --- Freeze / seed ------------------------------------------------------
  std::printf("Freeze / seed:\n");
  BlockStats frozen = RunBlocks(
      &processor, 50, &phase, 0.5f, true, false, 0);
  ok &= Expect(frozen.finite, "50 frozen blocks produce finite output");
  std::printf("  freeze rms = %.6f\n", frozen.rms());

  BlockStats seeded = RunBlocks(
      &processor, 50, &phase, 0.5f, false, true, 0);
  ok &= Expect(seeded.finite, "50 seeded blocks produce finite output");
  std::printf("  seed rms = %.6f\n", seeded.rms());

  // --- Tape modes ---------------------------------------------------------
  // Out of scope for the v1 UI, but they must keep working: Clean Tape covers
  // the 12-bit half-rate path with a plain dry copy, Dirty Tape covers the
  // relocated µ-law dry path and the host-rate wow and flutter.
  std::printf("Tape modes (deferred, smoke test):\n");
  processor.Configure(beads::Processor::MEDIA_CLEAN_TAPE, false);
  ok &= Expect(processor.sample_rate() == kVintageEngineRate,
               "Clean Tape engine rate == 22050");
  ok &= Expect(processor.ratio() == 2, "Clean Tape ratio() == 2");
  BlockStats clean_tape = RunBlocks(
      &processor, 32, &phase, 0.5f, false, false, 0);
  ok &= Expect(clean_tape.finite, "32 blocks of Clean Tape are finite");
  ok &= Expect(clean_tape.rms() > 0.0, "Clean Tape output is non-silent");
  std::printf("  rms = %.6f\n", clean_tape.rms());

  processor.Configure(beads::Processor::MEDIA_DIRTY_TAPE, false);
  ok &= Expect(processor.sample_rate() == kVintageEngineRate,
               "Dirty Tape engine rate == 22050");
  ok &= Expect(processor.ratio() == 2, "Dirty Tape ratio() == 2");
  BlockStats dirty_tape = RunBlocks(
      &processor, 32, &phase, 0.5f, false, false, 0);
  ok &= Expect(dirty_tape.finite, "32 blocks of Dirty Tape are finite");
  ok &= Expect(dirty_tape.rms() > 0.0, "Dirty Tape output is non-silent");
  std::printf("  rms = %.6f\n", dirty_tape.rms());

  // --- Split helper path (second core) -------------------------------
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

  for (size_t i = 0; i < kDelayBanks; ++i) {
    std::free(buffers[i]);
  }
  std::free(reverb_buffer);

  std::printf("%s\n", ok ? "PASS: mbeads core" : "FAIL: mbeads core");
  return ok ? 0 : 1;
}
