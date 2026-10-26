file(GLOB SRC_FILES *.cpp *.hpp menupages/*.cpp menupages/*.hpp)

# if not CONFIG_TBD_PLATFORM_BBA remove Midi.* files from SRC_FILES
if(NOT CONFIG_TBD_PLATFORM_BBA)
    list(REMOVE_ITEM SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/Midi.cpp)
    list(REMOVE_ITEM SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/Midi.hpp)
endif()

# Bluetooth MIDI receiver only when BT enabled
if(NOT CONFIG_BT_ENABLED)
    list(REMOVE_ITEM SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/BtMidiReceiver.cpp)
    list(REMOVE_ITEM SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/BtMidiReceiver.hpp)
    list(REMOVE_ITEM SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/menupages/UIMenuPageMidi.cpp)
    list(REMOVE_ITEM SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/menupages/UIMenuPageMidi.hpp)
endif()

set(MAIN_PRIV_REQUIRES "")
if(CONFIG_BT_ENABLED)
    set(MAIN_PRIV_REQUIRES "bt")
endif()

idf_component_register(SRCS ${SRC_FILES}
        INCLUDE_DIRS "." "menupages/"
        PRIV_REQUIRES ${MAIN_PRIV_REQUIRES})

# Radio-free build: WiFi/REST and BLE MIDI are never initialized at runtime.
# Goal: a clean core 0 (no NimBLE host task, no WiFi/LWIP activity) for the
# Elements second-core helper, plus the internal DRAM the radio stacks would
# otherwise hold. Test branch for the UI-on-ESP32-C3 split architecture.
option(TBD_RADIO_FREE
        "Radio-free: never init WiFi/REST or BLE MIDI" ON)
if(TBD_RADIO_FREE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TBD_RADIO_FREE=1)
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wno-unused-local-typedefs
        )

target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wno-unused-local-typedefs
        )

