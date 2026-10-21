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
BlockStats RunBlocks(
    beads::Processor* processor,
    int num_blocks,
    double* phase,
    float dry_wet,
    bool freeze_all,
    bool toggle_seed,
    int first_block_for_stats,
    bool in_place = false) {
  beads::Parameters parameters;
  parameters.time.base = 0.5f;
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

  // 0.75: above the "manual taps" range, so the trigger generator free-runs
  // and the granular engine is genuinely exercised.
  beads::ModulatedParameter density;
  density.base = 0.75f;
  density.cv = 0.0f;

  beads::Processor::MixParameters mix;
  mix.feedback = 0.0f;
  mix.dry_wet = dry_wet;
  mix.reverb_amount = 0.25f;
  mix.output_gate = false;

  BlockStats stats;
  const double phase_increment = kTwoPi * 220.0 / kHostSampleRate;

  for (int block = 0; block < num_blocks; ++block) {
    beads::FloatFrame in[kBlockSize];
    beads::FloatFrame out_storage[kBlockSize];
    beads::FloatFrame* out = in_place ? in : out_storage;
    bool freeze[kBlockSize];
    stmlib::GateFlags seed_input[kBlockSize];

    const beads::SeedButtonState seed_state = (toggle_seed && (block & 1))
        ? beads::SEED_BUTTON_STATE_OFF
        : (toggle_seed ? beads::SEED_BUTTON_STATE_ON
                       : beads::SEED_BUTTON_STATE_OFF);

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
      seed_input[i] = stmlib::GATE_FLAG_LOW;
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

}  // namespace

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

  beads::Processor processor;
  processor.Init(buffers, reverb_buffer, kHostSampleRate);
  std::printf("sizeof(beads::Processor) = %zu bytes\n",
              sizeof(beads::Processor));
  std::printf("extension memory = %zu bytes (delay) + %zu bytes (reverb)\n",
              kDelayBanks * kDelayBankBytes,
              kReverbSamples * sizeof(uint16_t));
  // ~30.7KB on a 64-bit host; keep a loose bound so object bloat is noticed.
  ok &= Expect(sizeof(beads::Processor) <= 40 * 1024,
               "sizeof(beads::Processor) stays within budget");

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

  // Fully wet, to prove the granular engine - not just the dry path - makes
  // sound.
  BlockStats digital_wet = RunBlocks(
      &processor, 500, &phase, 1.0f, false, false, 0);
  ok &= Expect(digital_wet.finite, "fully wet output is finite");
  ok &= Expect(digital_wet.rms() > 1e-6, "granular engine is non-silent");
  std::printf("  wet rms = %.6f\n", digital_wet.rms());

  // In-place processing (in == out) must stay correct: the dry signal is
  // captured before the wet engine writes the output.
  BlockStats in_place = RunBlocks(
      &processor, 50, &phase, 0.5f, false, false, 0, true);
  ok &= Expect(in_place.finite, "in-place (in == out) output is finite");
  ok &= Expect(in_place.rms() > 0.0, "in-place output is non-silent");
  std::printf("  in-place rms = %.6f\n", in_place.rms());

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

  for (size_t i = 0; i < kDelayBanks; ++i) {
    std::free(buffers[i]);
  }
  std::free(reverb_buffer);

  std::printf("%s\n", ok ? "PASS: mbeads core" : "FAIL: mbeads core");
  return ok ? 0 : 1;
}
