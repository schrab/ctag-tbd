/*
 * Host tests for the TBStrings Karplus-Strong plucked-string voice.
 * Exercises the real rings/dsp/plucked_string.h used by the chord engine.
 */
#include "test_framework.hpp"

#include "rings/dsp/plucked_string.h"
#include "rings/dsp/string_synth_part.h"
#include "helpers/strum_plan.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
float Energy(const float* x, size_t n) {
  float e = 0.0f;
  for (size_t i = 0; i < n; ++i) e += x[i] * x[i];
  return e;
}
}  // namespace

TEST(plucked_string_silent_until_plucked) {
  int16_t buf[rings::kPluckedStringLength];
  rings::PluckedString s;
  s.Init(buf);
  CHECK(!s.started());
  for (int i = 0; i < 64; ++i) CHECK(s.Process() == 0.0f);
}

TEST(plucked_string_rings_and_decays) {
  int16_t buf[rings::kPluckedStringLength];
  rings::PluckedString s;
  s.Init(buf);
  s.Pluck(220.0f, 0.5f, 0.5f, 0.8f);
  CHECK(s.started());

  float samples[4096];
  for (size_t i = 0; i < 4096; ++i) {
    samples[i] = s.Process();
    CHECK(std::isfinite(samples[i]));
    CHECK(fabsf(samples[i]) <= 1.0f);
  }
  const float early = Energy(samples, 512);
  const float late = Energy(samples + 3584, 512);
  CHECK(early > 1e-6f);
  CHECK(late < early);
}

TEST(plucked_string_damping_shortens_decay) {
  int16_t buf_a[rings::kPluckedStringLength];
  int16_t buf_b[rings::kPluckedStringLength];
  rings::PluckedString soft, hard;
  soft.Init(buf_a);
  hard.Init(buf_b);
  soft.Pluck(330.0f, 0.5f, 0.0f, 0.8f);
  hard.Pluck(330.0f, 0.5f, 1.0f, 0.8f);

  float a[4096], b[4096];
  for (size_t i = 0; i < 4096; ++i) {
    a[i] = soft.Process();
    b[i] = hard.Process();
  }
  CHECK(Energy(b + 3072, 1024) < Energy(a + 3072, 1024));
}

TEST(plucked_string_clear_silences) {
  int16_t buf[rings::kPluckedStringLength];
  rings::PluckedString s;
  s.Init(buf);
  s.Pluck(110.0f, 0.8f, 0.2f, 0.9f);
  for (int i = 0; i < 200; ++i) s.Process();
  s.Clear();
  CHECK(!s.started());
  for (int i = 0; i < 128; ++i) CHECK(s.Process() == 0.0f);
}

TEST(plucked_chord_engine_chord_change_stays_in_bounds) {
  // Regression: PlanStrum's chord-change Clear used to iterate all 4 groups
  // while sizing slots by the ACTIVE polyphony. At polyphony 1 that indexed
  // plucked_[16..31] past the 12-voice array (StoreProhibited on device).
  static uint16_t reverb[32768];
  static int16_t plucks[rings::kStringSynthVoices * rings::kPluckedStringLength];
  rings::StringSynthPart part;
  part.Init(reverb, plucks);
  part.set_plucked_strings(true);
  part.set_polyphony(1);
  part.set_strum_style(tbd::STRUM_PENDULUM);
  part.set_fx(rings::FX_REVERB);

  rings::Patch patch;
  patch.brightness = 0.5f;
  patch.damping = 0.5f;
  patch.position = 0.3f;
  rings::PerformanceState ps;
  ps.note = 60.0f;
  ps.tonic = 12.0f;
  ps.internal_exciter = true;
  ps.strum = true;
  float in[32] = {0.0f};
  float out[32], aux[32];
  part.Process(ps, patch, in, out, aux, 32);
  CHECK(std::isfinite(out[0]));

  ps.chord = 5;  // chord change -> PlanStrum -> Clear on every active slot
  ps.strum = true;
  part.Process(ps, patch, in, out, aux, 32);
  CHECK(std::isfinite(out[0]));
  CHECK(std::isfinite(aux[0]));
}

TEST(plucked_string_is_deterministic) {
  int16_t buf_a[rings::kPluckedStringLength];
  int16_t buf_b[rings::kPluckedStringLength];
  rings::PluckedString a, b;
  a.Init(buf_a);
  b.Init(buf_b);
  a.Pluck(261.63f, 0.7f, 0.4f, 0.75f);
  b.Pluck(261.63f, 0.7f, 0.4f, 0.75f);
  for (int i = 0; i < 1024; ++i) {
    CHECK(a.Process() == b.Process());
  }
}
