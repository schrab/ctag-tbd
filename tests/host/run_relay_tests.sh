#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
mkdir -p "$HERE/build"
"${CXX:-g++}" -std=c++20 -O1 -g -pthread -Wall -Wextra -Wno-unused-parameter \
    -I "$HERE/relay_shim" -I "$HERE/shim" -I "$HERE" \
    -I "$ROOT/uimcu/ui" -I "$ROOT/uimcu/main" -I "$ROOT/protocol/include" \
    "$HERE/main.cpp" "$HERE/test_panel_relay.cpp" "$ROOT/protocol/src/protocol.cpp" \
    -o "$HERE/build/relay_tests"
"$HERE/build/relay_tests"
