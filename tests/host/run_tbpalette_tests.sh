#!/usr/bin/env bash
#
# Host unit test for TBPalette (Rubato Plaits 98 models, namespace rplaits)
# and coexistence with the untouched eurorack plaits tree (TBDaits 24 models).
#
# Builds real production sources from both trees on the build host with g++.
# No ESP-IDF toolchain or hardware required.
#
# Usage:  tests/host/run_tbpalette_tests.sh
# Exit code 0 = all tests passed.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
MUTABLE="$ROOT/components/mutable"
EURORACK="$MUTABLE/eurorack"
RUBATO="$MUTABLE/rubato"
OUT="$HERE/build"

CXX="${CXX:-g++}"
mkdir -p "$OUT"

echo "==> Building TBPalette host smoke test"
# Gather sources
RUBATO_SRCS=(
    "$RUBATO/rplaits/dsp/voice.cc"
    "$RUBATO/rplaits/dsp/envelope.cc"
    "$RUBATO/rplaits/dsp/parameter_randomizer.cc"
    "$RUBATO/rplaits/dsp/chords/chord_bank.cc"
    "$RUBATO/rplaits/dsp/engine"/*.cc
    "$RUBATO/rplaits/dsp/engine2"/*.cc
    "$RUBATO/rplaits/dsp/fm"/*.cc
    "$RUBATO/rplaits/dsp/speech"/*.cc
    "$RUBATO/rplaits/dsp/physical_modelling"/*.cc
    "$RUBATO/rplaits/resources.cc"
)

EURORACK_SRCS=(
    "$EURORACK/plaits/dsp/voice.cc"
    "$EURORACK/plaits/dsp/chords/chord_bank.cc"
    "$EURORACK/plaits/dsp/engine"/*.cc
    "$EURORACK/plaits/dsp/engine2"/*.cc
    "$EURORACK/plaits/dsp/fm"/*.cc
    "$EURORACK/plaits/dsp/physical_modelling"/*.cc
    "$EURORACK/plaits/dsp/speech"/*.cc
    "$EURORACK/plaits/resources.cc"
    "$EURORACK/stmlib/utils/random.cc"
    "$EURORACK/stmlib/dsp/units.cc"
    "$EURORACK/stmlib/dsp/atan.cc"
)

"$CXX" -std=c++17 -O1 -ffast-math -DTEST \
    -Wall -Wno-unused-variable -Wno-unused-parameter \
    -I "$EURORACK" \
    -I "$RUBATO" \
    "$HERE/test_tbpalette_smoke.cpp" \
    "${RUBATO_SRCS[@]}" \
    "${EURORACK_SRCS[@]}" \
    -o "$OUT/test_tbpalette_smoke"

echo "==> Running TBPalette host smoke test"
"$OUT/test_tbpalette_smoke"
