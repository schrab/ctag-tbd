// Host validation of TBPalette (Rubato Plaits 98 engines, namespace rplaits)
// and coexistence with the stock eurorack plaits tree (TBDaits 24 engines,
// namespace plaits).
//
// Builds the real sources from both trees with the host compiler and runs them
// at 44.1 kHz in 32-sample blocks, ensuring neither tree breaks the other, all
// 98 Rubato models render finite output, and memory fits within the arena.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "rplaits/dsp/voice.h"
#include "plaits/dsp/voice.h"

static char scratch_rplaits[32768];
static char scratch_plaits[16384];

template <typename VoiceT, typename PatchT, typename ModT, typename FrameT>
static bool Sweep(const char* tag, VoiceT& voice, PatchT& patch, ModT& mod,
                  FrameT* frames, int num_engines, size_t max_block) {
  for (int e = 0; e < num_engines; ++e) {
    patch.engine = e;
    // Enough blocks for the engine switch + steady-state rendering.
    for (int b = 0; b < 8; ++b) {
      mod.trigger = (b % 4 == 0) ? 1.0f : 0.0f;
      voice.Render(patch, mod, frames, max_block);
      for (size_t i = 0; i < max_block; ++i) {
        if (!std::isfinite(static_cast<float>(frames[i].out)) ||
            !std::isfinite(static_cast<float>(frames[i].aux))) {
          printf("%s engine %d: NON-FINITE output at block %d sample %zu\n",
                 tag, e, b, i);
          return false;
        }
      }
    }
  }
  printf("%s: all %d engines swept clean\n", tag, num_engines);
  return true;
}

int main() {
  printf("==> Checking sizeof probe gate\n");
  const size_t sz_rplaits = sizeof(rplaits::Voice);
  const size_t sz_plaits = sizeof(plaits::Voice);
  printf("sizeof(rplaits::Voice) = %zu bytes\n", sz_rplaits);
  printf("sizeof(plaits::Voice)  = %zu bytes\n", sz_plaits);

  // Gate check: object + 32 KB scratch must comfortably fit in the 110,592 B STEREO arena.
  assert(sz_rplaits <= 30000);
  assert(sz_rplaits + 32768 < 110592);

  // --- rplaits (Rubato Plaits, 98 engines) ---
  printf("==> Initializing rplaits::Voice (98 engines)\n");
  {
    stmlib::BufferAllocator alloc(scratch_rplaits, sizeof(scratch_rplaits));
    rplaits::Voice voice;
    voice.Init(&alloc);

    rplaits::Patch patch = {};
    patch.engine = 0;
    patch.decay = 0.5f;
    patch.lpg_colour = 0.5f;
    patch.note = 60.0f;
    patch.harmonics = 0.5f;
    patch.timbre = 0.5f;
    patch.morph = 0.5f;
    patch.aux_output_option = 1;             // True stereo ON (TBPalette default)
    patch.locked_frequency_pot_option = 1;   // 4th macro active
    patch.freqlock_param = 0.5f;

    rplaits::Modulations mod = {};
    mod.frequency_patched = false;
    mod.timbre_patched = false;
    mod.morph_patched = false;
    mod.trigger_patched = false;
    mod.level_patched = false;

    rplaits::Voice::Frame frames[32];
    if (!Sweep("rplaits", voice, patch, mod, frames, 98, 32)) {
      return 1;
    }
  }

  // --- plaits (eurorack, 24 engines untouched) ---
  printf("==> Initializing plaits::Voice (24 stock engines)\n");
  {
    stmlib::BufferAllocator alloc(scratch_plaits, sizeof(scratch_plaits));
    plaits::Voice voice;
    voice.Init(&alloc);

    plaits::Patch patch = {};
    patch.engine = 0;
    patch.decay = 0.5f;
    patch.lpg_colour = 0.5f;
    patch.note = 60.0f;

    plaits::Modulations mod = {};
    mod.frequency_patched = false;
    mod.timbre_patched = false;
    mod.morph_patched = false;
    mod.trigger_patched = false;
    mod.level_patched = false;

    plaits::Voice::Frame frames[32];
    if (!Sweep("plaits", voice, patch, mod, frames, 24, 32)) {
      return 1;
    }
  }

  printf("==> All coexistence checks passed! SMOKE OK\n");
  return 0;
}
