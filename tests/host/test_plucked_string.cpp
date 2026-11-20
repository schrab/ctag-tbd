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
// First-difference-to-energy ratio: a rough but stable "brightness" metric.
float Brightness(const float* x, size_t n) {
  float d = 0.0f;
  for (size_t i = 1; i < n; ++i) {
    const float v = x[i] - x[i - 1];
    d += v * v;
  }
  return d / (Energy(x, n) + 1e-9f);
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
  s.Pluck(220.0f, 0.5f, 0.5f, 0.0f, 0.5f, 0.8f);
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
  soft.Pluck(330.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.8f);
  hard.Pluck(330.0f, 0.5f, 1.0f, 0.0f, 0.5f, 0.8f);

  float a[4096], b[4096];
  for (size_t i = 0; i < 4096; ++i) {
    a[i] = soft.Process();
    b[i] = hard.Process();
  }
  CHECK(Energy(b + 3072, 1024) < Energy(a + 3072, 1024));
}

TEST(plucked_string_brightness_is_not_inverted) {
  int16_t buf_a[rings::kPluckedStringLength];
  int16_t buf_b[rings::kPluckedStringLength];
  rings::PluckedString dark, bright;
  dark.Init(buf_a);
  bright.Init(buf_b);
  dark.Pluck(220.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.8f);
  bright.Pluck(220.0f, 1.0f, 0.5f, 0.0f, 0.5f, 0.8f);
  float a[2048], b[2048];
  for (size_t i = 0; i < 2048; ++i) {
    a[i] = dark.Process();
    b[i] = bright.Process();
  }
  // Higher brightness must mean MORE high-frequency content. This test
  // pins the sign of the loop lowpass (it was inverted on first release).
  CHECK(Brightness(b, 2048) > Brightness(a, 2048));
}

TEST(plucked_string_structure_and_position_shape_the_string) {
  int16_t b0[rings::kPluckedStringLength];
  int16_t b1[rings::kPluckedStringLength];
  int16_t b2[rings::kPluckedStringLength];
  int16_t b3[rings::kPluckedStringLength];
  rings::PluckedString plain, stiff, bridge, middle;
  plain.Init(b0); stiff.Init(b1); bridge.Init(b2); middle.Init(b3);
  plain.Pluck(220.0f, 0.5f, 0.5f, 0.0f, 0.5f, 0.8f);
  stiff.Pluck(220.0f, 0.5f, 0.5f, 1.0f, 0.5f, 0.8f);
  bridge.Pluck(220.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.8f);
  middle.Pluck(220.0f, 0.5f, 0.5f, 0.0f, 1.0f, 0.8f);
  float p[1024], s[1024], br[1024], mi[1024];
  for (int i = 0; i < 1024; ++i) {
    p[i] = plain.Process(); s[i] = stiff.Process();
    br[i] = bridge.Process(); mi[i] = middle.Process();
  }
  // structure (stiffness allpass) and position (pluck-point comb) must both
  // audibly change the string — neither may be a dead knob.
  CHECK(Energy(s, 1024) > 0.0f);
  CHECK(Brightness(s, 1024) != Brightness(p, 1024));
  CHECK(Energy(mi, 1024) > 0.0f);
  CHECK(Brightness(mi, 1024) != Brightness(br, 1024));
}

TEST(plucked_string_clear_silences) {
  int16_t buf[rings::kPluckedStringLength];
  rings::PluckedString s;
  s.Init(buf);
  s.Pluck(110.0f, 0.8f, 0.2f, 0.0f, 0.5f, 0.9f);
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
  part.set_fx(rings::FX_NONE);

  rings::Patch patch;
  patch.brightness = 0.5f;
  patch.damping = 0.5f;
  patch.structure = 0.3f;
  patch.position = 0.5f;
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
  a.Pluck(261.63f, 0.7f, 0.4f, 0.2f, 0.5f, 0.75f);
  b.Pluck(261.63f, 0.7f, 0.4f, 0.2f, 0.5f, 0.75f);
  for (int i = 0; i < 1024; ++i) {
    CHECK(a.Process() == b.Process());
  }
}
