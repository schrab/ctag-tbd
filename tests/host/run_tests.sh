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
PROTOCOL="$ROOT/protocol"
RAPIDJSON="$ROOT/components/rapidjson"
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
    -I "$ROOT/components/ctagSoundProcessor" \
    -I "$ROOT/main" \
    -I "$PROTOCOL/include" \
    -I "$RAPIDJSON" \
    -I "$EURORACK" \
    -DMODTRANSPORT_CFG_PATH="\"$OUT/mt_cfg_test.jsn\"" \
    "$HERE/main.cpp" \
    "$HERE/test_marbles_core.cpp" \
    "$HERE/test_input_source.cpp" \
    "$HERE/test_link_protocol.cpp" \
    "$HERE/test_chain_budget.cpp" \
    "$HERE/test_render_ladder.cpp" \
    "$HERE/test_seq_mod.cpp" \
    "$HERE/test_varg_core.cpp" \
    "$HERE/test_chainsaw_dist.cpp" \
    "$ROOT/main/ModTransport.cpp" \
    "$ROOT/main/SeqEngine.cpp" \
    "$PROTOCOL/src/protocol.cpp" \
    "$EURORACK/marbles/resources.cc" \
    "$EURORACK/marbles/random/t_generator.cc" \
    "$EURORACK/marbles/random/x_y_generator.cc" \
    "$EURORACK/marbles/random/output_channel.cc" \
    "$EURORACK/marbles/random/quantizer.cc" \
    "$EURORACK/marbles/random/discrete_distribution_quantizer.cc" \
    "$EURORACK/marbles/random/lag_processor.cc" \
    "$EURORACK/marbles/ramp/ramp_extractor.cc" \
    "$EURORACK/stmlib/dsp/units.cc" \
    "$EURORACK/rings/dsp/string_synth_part.cc" \
    "$EURORACK/rings/resources.cc" \
    "$EURORACK/braids/resources.cc" \
    "$ROOT/components/ctagSoundProcessor/vult/vult_formantor.cpp" \
    -o "$OUT/host_tests"

echo "==> Running"
rm -f "$OUT/mt_cfg_test.jsn"   # section-IO test needs a fresh scratch file
"$OUT/host_tests"
