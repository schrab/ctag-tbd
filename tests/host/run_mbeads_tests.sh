#!/usr/bin/env bash
#
# Host validation of the ported Beads DSP core used by the Mbeads plugin.
#
# Builds the real Beads sources (no copies, no stubs) with the host compiler and
# runs them at 44.1kHz in fixed 32-sample blocks, which is how the TBD audio
# task calls the engine. No ESP-IDF toolchain, hardware or PSRAM required.
#
# Usage:  tests/host/run_mbeads_tests.sh
# Exit code 0 = all checks passed.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BEADS="$ROOT/components/mutable/eurorack"
OUT="$HERE/build"

CXX="${CXX:-g++}"
mkdir -p "$OUT"

echo "==> Building Mbeads core test"
"$CXX" -std=c++17 -O2 -ffast-math \
    -I "$BEADS" \
    "$HERE/mbeads_core_test.cpp" \
    "$BEADS/beads/dsp/processor.cc" \
    "$BEADS/beads/dsp/trigger_generator.cc" \
    "$BEADS/beads/dsp/delay_controller.cc" \
    "$BEADS/beads/dsp/delay_clock.cc" \
    "$BEADS/beads/dsp/mu_law.cc" \
    "$BEADS/beads/resources_min.cc" \
    "$BEADS/stmlib/utils/random.cc" \
    "$BEADS/stmlib/dsp/units.cc" \
    "$BEADS/stmlib/dsp/atan.cc" \
    -o "$OUT/mbeads_core_test"

echo "==> Running"
"$OUT/mbeads_core_test"
