#!/usr/bin/env bash
#
# Host unit tests for the Marbles random-modulator core (Mutable Instruments
# sources from the eurorack submodule) and the stored input-source vocabulary.
#
# Builds the real submodule and helper sources -- not copies -- so the tests
# exercise production code on the build host with no ESP-IDF toolchain,
# hardware, or network required.
#
# Usage:  tests/host/run_tests.sh
# Exit code 0 = all tests passed.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
HELPERS="$ROOT/components/ctagSoundProcessor/helpers"
EURORACK="$ROOT/components/mutable/eurorack"
SHIM="$HERE/shim"
OUT="$HERE/build"

CXX="${CXX:-g++}"
mkdir -p "$OUT"

echo "==> Building host tests"
"$CXX" -std=c++20 -O1 -g \
    -Wall -Wextra -Wno-unused-parameter \
    -I "$SHIM" \
    -I "$HERE" \
    -I "$HELPERS" \
    -I "$ROOT/main" \
    -I "$EURORACK" \
    "$HERE/main.cpp" \
    "$HERE/test_marbles_core.cpp" \
    "$HERE/test_input_source.cpp" \
    "$EURORACK/marbles/resources.cc" \
    "$EURORACK/marbles/random/t_generator.cc" \
    "$EURORACK/marbles/random/x_y_generator.cc" \
    "$EURORACK/marbles/random/output_channel.cc" \
    "$EURORACK/marbles/random/quantizer.cc" \
    "$EURORACK/marbles/random/discrete_distribution_quantizer.cc" \
    "$EURORACK/marbles/random/lag_processor.cc" \
    "$EURORACK/marbles/ramp/ramp_extractor.cc" \
    "$EURORACK/stmlib/dsp/units.cc" \
    -o "$OUT/host_tests"

echo "==> Running"
"$OUT/host_tests"
