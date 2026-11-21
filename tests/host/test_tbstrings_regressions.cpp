/* Focused regressions for TBStrings' real string-bank DSP and note consumer. */
#include "test_framework.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

// State inspection stays in this test translation unit, never in production APIs.
#include "rings/dsp/dsp.h"
#include "rings/dsp/plucker.h"
#include "rings/dsp/limiter.h"
#include "stmlib/dsp/filter.h"
#include "helpers/speaker_ir_44100.h"
#include "helpers/tbd_guitar_dist.h"
#define private public
#include "rings/dsp/guitar_string_bank.h"
#undef private
#include "stmlib/dsp/units.h"

namespace {
constexpr size_t kBlock = rings::kMaxBlockSize;

float Frequency(float midi) {
  return stmlib::SemitonesToRatio(midi - 69.f) * rings::a3;
}

float Render(rings::GuitarStringBank& bank,
             const rings::GuitarStringBankConfig& config, int blocks) {
  float in[kBlock] {}, out[kBlock], aux[kBlock];
  float energy = 0.f;
  for (int b = 0; b < blocks; ++b) {
    bank.Process(config, in, out, aux, kBlock);
    for (size_t i = 0; i < kBlock; ++i) {
      CHECK(std::isfinite(out[i]));
      CHECK(std::isfinite(aux[i]));
      energy += out[i] * out[i] + aux[i] * aux[i];
    }
  }
  return energy;
}

float LateEnergy(float damping, bool dispersion) {
  auto bank = std::make_unique<rings::GuitarStringBank>();
  bank->Init(dispersion);
  rings::GuitarStringBankConfig config;
  config.damping = 0.8f;
  config.brightness = 0.8f;
  stmlib::Random::Seed(0x1234);
  bank->SetNote(0, 45.f, true, 0.25f);
  if (damping >= 0.f) bank->SetDampingOverride(0, damping);
  Render(*bank, config, 300);  // 218 ms: past the ghost/transient's decay.
  return Render(*bank, config, 300);
}
}  // namespace

TEST(tbstrings_retrigger_removes_late_inharmonic_energy) {
  auto bank = std::make_unique<rings::GuitarStringBank>();
  bank->Init(true);
  rings::GuitarStringBankConfig config;
  config.damping = 0.95f;
  config.brightness = 0.8f;
  bank->SetNote(0, 40.f, true);
  CHECK(Render(*bank, config, 100) > 1e-5f);
  // A zero-velocity retrigger isolates the old vibration from new excitation.
  // Scaling only the main line leaves stretch/filter energy returning later.
  bank->SetNote(0, 64.f, true, 0.f);
  Render(*bank, config, 32);
  const float late = Render(*bank, config, 256);
  printf("INHARMONIC old-note late energy=%g\n", late);
  CHECK(late < 1e-12f);
}

TEST(tbstrings_glide_survives_companion_clear_and_other_strings_snap) {
  auto bank = std::make_unique<rings::GuitarStringBank>();
  bank->Init(false);
  rings::GuitarStringBankConfig config;
  bank->SetNote(0, 45.f, true);
  Render(*bank, config, 1);
  const float start = bank->string_[0].frequency_;
  const float target = Frequency(69.f);
  bank->SetGlideMs(50.f);
  bank->SetGlideEnabled(true);
  bank->SetNote(0, 69.f, true);
  Render(*bank, config, 1);
  const float first = bank->string_[0].frequency_;
  CHECK(first > start && first < target);
  bank->SetGlideEnabled(false);  // one-block VARG companion has ended
  bank->SetNote(1, 57.f, true);  // an unrelated note must snap
  Render(*bank, config, 1);
  CHECK_NEAR(bank->string_[1].frequency_, Frequency(57.f), 1e-8f);
  CHECK(bank->string_[0].frequency_ > first);
  CHECK(bank->string_[0].frequency_ < target * 0.95f);
  Render(*bank, config, 67);  // total 69 blocks = approximately one 50 ms tau
  const float remaining = (target - bank->string_[0].frequency_) / (target - start);
  printf("GLIDE 50ms remaining fraction=%g (expected ~0.367)\n", remaining);
  CHECK(remaining > 0.35f && remaining < 0.39f);
  Render(*bank, config, 1000);
  CHECK_NEAR(bank->string_[0].frequency_, target, target * 1e-5f);
}

TEST(tbstrings_normal_retrigger_snaps_its_own_active_glide) {
  auto bank = std::make_unique<rings::GuitarStringBank>();
  bank->Init(false);
  rings::GuitarStringBankConfig config;
  bank->SetNote(0, 45.f, true);
  Render(*bank, config, 1);
  bank->SetGlideMs(50.f);
  bank->SetGlideEnabled(true);
  bank->SetNote(0, 69.f, true);
  Render(*bank, config, 10);
  CHECK(bank->string_[0].frequency_ < Frequency(69.f) * 0.95f);
  bank->SetGlideEnabled(false);
  bank->SetNote(0, 57.f, true);
  Render(*bank, config, 1);
  CHECK_NEAR(bank->string_[0].frequency_, Frequency(57.f), 1e-8f);
}

TEST(tbstrings_glide_request_is_latched_at_note_on) {
  auto bank = std::make_unique<rings::GuitarStringBank>();
  bank->Init(false);
  rings::GuitarStringBankConfig config;
  bank->SetNote(0, 45.f, true);
  Render(*bank, config, 1);
  bank->SetGlideMs(30.f);
  bank->SetGlideEnabled(true);
  bank->SetNote(0, 69.f, true);
  bank->SetGlideEnabled(false);  // clearing before rendering must also be safe
  Render(*bank, config, 2);
  CHECK(bank->string_[0].frequency_ < Frequency(69.f) * 0.95f);
}

TEST(tbstrings_ghost_damping_lasts_until_normal_retrigger_only_on_its_string) {
  auto bank = std::make_unique<rings::GuitarStringBank>();
  bank->Init(false);
  rings::GuitarStringBankConfig config;
  config.damping = 0.8f;
  bank->SetNote(0, 45.f, true);
  bank->SetNote(1, 52.f, true);
  bank->SetDampingOverride(0, 0.1f);
  Render(*bank, config, 100);
  CHECK_NEAR(bank->string_[0].damping_, 0.1f, 1e-7f);
  CHECK_NEAR(bank->string_[1].damping_, 0.8f, 1e-7f);
  bank->SetNote(0, 48.f, true);  // normal note drops the previous ghost override
  Render(*bank, config, 1);
  CHECK_NEAR(bank->string_[0].damping_, 0.8f, 1e-7f);
}

TEST(tbstrings_ghost_and_transient_really_decay_quickly) {
  for (bool dispersion : {false, true}) {
    const float normal = LateEnergy(-1.f, dispersion);
    const float ghost = LateEnergy(0.1f, dispersion);
    const float transient = LateEnergy(0.f, dispersion);
    printf("DECAY dispersion=%d normal=%g ghost=%g transient=%g\n",
           dispersion, normal, ghost, transient);
    CHECK(normal > 1e-5f);
    CHECK(ghost < normal * 0.01f);
    CHECK(transient < ghost * 0.2f);
  }
}

TEST(tbstrings_arpeggio_consumer_routes_one_note_and_preserves_pedal_string) {
  for (int str = 0; str < rings::kGuitarStringCount; ++str) {
    // Arpeggio advertises a string but no whole-chord voicing, even when the
    // burst flag is clear. String 3 is the dedicated pedal and must not fold.
    const auto event = rings::RouteGuitarNote(str, -1, false, 0, 0.7f, 0);
    CHECK_EQ(event.count, 1);
    CHECK_EQ(event.string, str);
    CHECK_NEAR(event.velocity, 0.7f, 1e-7f);
    CHECK_EQ(event.damping, -1.f);
  }
  const auto block = rings::RouteGuitarNote(2, 17, false, 0, 0.7f, 0);
  CHECK_EQ(block.count, rings::kGuitarStringCount);
  CHECK_EQ(block.string, 2);
  const auto burst = rings::RouteGuitarNote(2, 17, true, 0, 0.7f, 0);
  CHECK_EQ(burst.count, 1);
  const auto manual = rings::RouteGuitarNote(-1, -1, false, 1, 1.f, 0);
  CHECK_EQ(manual.count, 1);
  CHECK_EQ(manual.string, 1);
}

TEST(tbstrings_ghost_consumer_emits_a_short_single_voice) {
  const auto ghost = rings::RouteGuitarNote(1, 17, false, 0, 0.8f, 1);
  CHECK_EQ(ghost.count, 1);  // a ghost must never re-pluck an entire chord
  CHECK_EQ(ghost.string, 1);
  CHECK_NEAR(ghost.velocity, 0.2f, 1e-7f);
  CHECK_NEAR(ghost.damping, 0.1f, 1e-7f);
  const auto transient = rings::RouteGuitarNote(3, -1, true, 0, 1.f, 2);
  CHECK_EQ(transient.string, 3);
  CHECK_NEAR(transient.velocity, 0.12f, 1e-7f);
  CHECK_EQ(transient.damping, 0.f);
}

TEST(tbstrings_note_damping_argument_resets_on_normal_retrigger) {
  auto bank = std::make_unique<rings::GuitarStringBank>();
  bank->Init(true);
  rings::GuitarStringBankConfig config;
  config.damping = 0.8f;
  bank->SetNote(1, 52.f, true);
  const auto ghost = rings::RouteGuitarNote(0, -1, true, 0, 1.f, 1);
  bank->SetNote(ghost.string, 45.f, true, ghost.velocity, ghost.damping);
  Render(*bank, config, 100);
  CHECK_NEAR(bank->string_[0].damping_, 0.1f, 1e-7f);
  CHECK_NEAR(bank->string_[1].damping_, 0.8f, 1e-7f);
  bank->SetNote(0, 48.f, true);
  Render(*bank, config, 1);
  CHECK_NEAR(bank->string_[0].damping_, 0.8f, 1e-7f);
}

TEST(tbstrings_manual_glide_default_snaps_and_enabled_request_glides) {
  auto bank = std::make_unique<rings::GuitarStringBank>();
  bank->Init(false);
  rings::GuitarStringBankConfig config;
  // The manual wrapper enables note-on glide, but its default time is zero.
  bank->SetGlideEnabled(true);
  bank->SetNote(0, 45.f, true);
  Render(*bank, config, 1);
  CHECK_NEAR(bank->string_[0].frequency_, Frequency(45.f), 1e-8f);
  bank->SetGlideMs(20.f);
  bank->SetNote(0, 57.f, true);
  Render(*bank, config, 1);
  CHECK(bank->string_[0].frequency_ > Frequency(45.f));
  CHECK(bank->string_[0].frequency_ < Frequency(57.f) * 0.95f);
}

TEST(tbstrings_dsp_storage_size) {
  printf("DSP SIZE bank=%zu String=%zu\n",
         sizeof(rings::GuitarStringBank), sizeof(rings::String));
}
