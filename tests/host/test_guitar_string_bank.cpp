/*
 * Host tests for TBStrings' 4-string guitar bank (rings::GuitarStringBank).
 * The real submodule sources are compiled into the host binary.
 */
#include "test_framework.hpp"

#include "rings/dsp/guitar_string_bank.h"

#include <cmath>
#include <cstring>

namespace {
float Energy(const float* x, size_t n) {
  float e = 0.0f;
  for (size_t i = 0; i < n; ++i) e += x[i] * x[i];
  return e;
}
}  // namespace

TEST(string_bank_pluck_produces_sound) {
  rings::GuitarStringBank bank;
  bank.Init(false);  // Sympathetic (no dispersion)
  bank.SetGlideMs(0.0f);
  bank.SetNote(0, 40.0f, true);  // low E
  float out[32], aux[32], in[32] = {0.0f};
  rings::GuitarStringBankConfig cfg;
  float energy = 0.0f;
  for (int b = 0; b < 32; ++b) {
    bank.Process(cfg, in, out, aux, 32);
    for (int i = 0; i < 32; ++i) {
      CHECK(std::isfinite(out[i]));
      CHECK(std::isfinite(aux[i]));
    }
    energy += Energy(out, 32);
  }
  CHECK(energy > 1e-5f);
}

TEST(string_bank_four_strings_are_independent) {
  rings::GuitarStringBank a, b;
  a.Init(true);
  b.Init(true);
  float out[32], aux[32], in[32] = {0.0f};
  rings::GuitarStringBankConfig cfg;

  // One string plucked on A, all four on B -> outputs must differ.
  a.SetNote(0, 40.0f, true);
  for (int s = 0; s < 4; ++s) b.SetNote(s, 40.0f + 5.0f * s, true);
  float ea = 0.0f, eb = 0.0f;
  for (int k = 0; k < 48; ++k) {
    a.Process(cfg, in, out, aux, 32);
    ea += Energy(out, 32);
    b.Process(cfg, in, out, aux, 32);
    eb += Energy(out, 32);
  }
  CHECK(ea > 0.0f);
  CHECK(eb > ea);
}

TEST(string_bank_mute_kills_the_previous_note) {
  rings::GuitarStringBank bank;
  bank.Init(false);
  bank.SetNote(0, 52.0f, true);
  float out[32], aux[32], in[32] = {0.0f};
  rings::GuitarStringBankConfig cfg;
  for (int k = 0; k < 16; ++k) bank.Process(cfg, in, out, aux, 32);
  bank.Mute(0);
  float energy = 0.0f;
  for (int k = 0; k < 48; ++k) {
    bank.Process(cfg, in, out, aux, 32);
    energy += Energy(out, 32) + Energy(aux, 32);
  }
  CHECK(energy < 1e-4f);
}

TEST(string_bank_fifth_string_is_a_pedal_and_velocity_scales_excitation) {
  rings::GuitarStringBank loud, soft;
  loud.Init(false);
  soft.Init(false);
  float out[32], aux[32], in[32] = {0.0f};
  rings::GuitarStringBankConfig cfg;
  // The dedicated pedal string (4) sustains its own pitch independently.
  loud.SetNote(rings::kGuitarPedalString, 43.0f, true, 1.0f);  // G2 open pedal
  soft.SetNote(rings::kGuitarPedalString, 43.0f, true, 0.3f);
  float el = 0.0f, es = 0.0f;
  for (int k = 0; k < 32; ++k) {
    loud.Process(cfg, in, out, aux, 32);
    el += Energy(out, 32) + Energy(aux, 32);
    soft.Process(cfg, in, out, aux, 32);
    es += Energy(out, 32) + Energy(aux, 32);
  }
  CHECK(el > 0.0f);
  CHECK(es > 0.0f);
  CHECK(es < el);   // velocity ladder must be audible
}

TEST(string_bank_new_note_on_same_string_is_finite_with_glide) {
  rings::GuitarStringBank bank;
  bank.Init(true);  // Inharmonic (dispersion), worst case
  bank.SetGlideMs(8.0f);
  float out[32], aux[32], in[32] = {0.0f};
  rings::GuitarStringBankConfig cfg;
  for (int round = 0; round < 6; ++round) {
    // Retrigger the SAME string with a different note: old note must be
    // muted and the pitch glided, with no NaN/damage.
    bank.SetNote(1, 45.0f + 3.0f * round, true);
    for (int k = 0; k < 12; ++k) {
      bank.Process(cfg, in, out, aux, 32);
      for (int i = 0; i < 32; ++i) CHECK(std::isfinite(out[i]));
    }
  }
}
