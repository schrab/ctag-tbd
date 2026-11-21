#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
mkdir -p "$HERE/build"
"${CXX:-g++}" -std=c++20 -O1 -g -pthread -Wall -Wextra -Wno-unused-parameter \
    -DRAPIDJSON_HAS_STDSTRING=1 \
    ${PANEL_TEST_FLAGS:-} \
    -I "$HERE/panel_shim" -I "$HERE/shim" -I "$HERE" \
    -I "$ROOT/uimcu/ui" -I "$ROOT/uimcu/main" -I "$ROOT/main" \
    -I "$ROOT/protocol/include" -I "$ROOT/components/rapidjson" \
    -I "$ROOT/components/ctagSoundProcessor" \
    "$HERE/main.cpp" "$HERE/test_panel_ui.cpp" "$HERE/test_preset_append.cpp" \
    "$ROOT/components/ctagSoundProcessor/ctagSPDataModel.cpp" \
    "$ROOT/components/ctagSoundProcessor/ctagDataModelBase.cpp" \
    "$ROOT/protocol/src/protocol.cpp" \
    "$ROOT/uimcu/ui/DspProxyLink.cpp" \
    "$ROOT/uimcu/ui/menupages/UIMenuPageParams.cpp" \
    "$ROOT/uimcu/ui/menupages/UIMenuPageSystem.cpp" \
    "$ROOT/uimcu/ui/menupages/UIMenuPageTape.cpp" \
    "$ROOT/uimcu/ui/menupages/UIMenuPageMidi.cpp" \
    "$ROOT/uimcu/ui/menupages/UIMenuPageHome.cpp" \
    -o "$HERE/build/panel_tests"
"$HERE/build/panel_tests"
