#!/usr/bin/env bash
#
# Host unit tests for the modulation helpers (ctagTempo, ctagSeq16, ctagGate16).
#
# Builds the real helper sources -- not copies -- against a thin FreeRTOS shim,
# so the tests exercise production code on the build host with no ESP-IDF
# toolchain, hardware, or network required.
#
# Usage:  tests/host/run_tests.sh
# Exit code 0 = all tests passed.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
HELPERS="$ROOT/components/ctagSoundProcessor/helpers"
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
    "$HERE/main.cpp" \
    "$HERE/test_ctagTempo.cpp" \
    "$HERE/test_ctagSeq16.cpp" \
    "$HELPERS/ctagTempo.cpp" \
    "$HELPERS/ctagSeq16.cpp" \
    "$HELPERS/ctagGate16.cpp" \
    -o "$OUT/host_tests"

echo "==> Running"
"$OUT/host_tests"