# Panel-controller firmware (ESP32-C3 Super Mini), doc/ui-split-sdd.md §5.1.
# The menu/panel UI runs here; the DSP stays on the A1S, reached over the
# shared link protocol.
cmake_minimum_required(VERSION 3.13)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IDF_TARGET esp32c3)

# Shared code from the parent repo: the link protocol (compiled by both
# firmwares and tests/host) and RapidJSON (header-only, single copy).
list(APPEND EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../protocol"
        "${CMAKE_CURRENT_LIST_DIR}/../components/rapidjson")

# pull in idf specific items, config vars etc.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ctag-uimcu)

# Same RapidJSON knobs as the A1S build — the ported pages are written
# against those semantics.
idf_build_set_property(COMPILE_DEFINITIONS -DRAPIDJSON_ALLOCATOR_DEFAULT_CHUNK_CAPACITY=4096 APPEND)
idf_build_set_property(COMPILE_DEFINITIONS -DRAPIDJSON_HAS_STDSTRING=1 APPEND)

# firmware version include (same mechanism as the A1S project)
execute_process(
        COMMAND bash -c "git describe --tags --abbrev=4 2>/dev/null || git rev-parse --short HEAD"
        OUTPUT_VARIABLE TBD_FW)
string(STRIP "${TBD_FW}" TBD_FW)
set(TBD_HW "C3-PANEL")
configure_file(${CMAKE_CURRENT_LIST_DIR}/version.hpp.in ${CMAKE_BINARY_DIR}/gen_include/version.hpp @ONLY)
idf_build_set_property(INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/gen_include APPEND)
