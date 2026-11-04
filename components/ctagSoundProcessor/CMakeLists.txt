# get all sound processor files
file(GLOB SOUND_PROCESSORS ctagSoundProcessor*.cpp)
# remove item matching "ctagSoundProcessor.cpp" from ${SOUND_PROCESSORS}
# list(FILTER SOUND_PROCESSORS EXCLUDE REGEX "ctagSoundProcessor\\.cpp$")
# DrumRack removed from the bundle (app partition space); sources kept
list(FILTER SOUND_PROCESSORS EXCLUDE REGEX "ctagSoundProcessorDrumRack")
# Mbeads removed from the bundle; sources kept
list(FILTER SOUND_PROCESSORS EXCLUDE REGEX "ctagSoundProcessorMbeads")
# Freakwaves removed from the BBA2 bundle; sources kept. Its stereo block
# need (110,208 B incl. object) exceeds the BBA2 allocator budget that keeps
# the Elements object (115,844 B) in internal SRAM1 — loading it asserted
# and crashed the core (ctagSoundProcessorFreakwaves.cpp:850). Revisit with
# a dedicated allocation path if Freakwaves is wanted on BBA2.
if(CONFIG_TBD_PLATFORM_BBA2)
    list(FILTER SOUND_PROCESSORS EXCLUDE REGEX "ctagSoundProcessorFreakwaves")
endif()
# clean generated directory
set(SP_GEN_INCLUDE_DIR "${CMAKE_BINARY_DIR}/gen_include")
file(REMOVE_RECURSE "${SP_GEN_INCLUDE_DIR}")
file(MAKE_DIRECTORY "${SP_GEN_INCLUDE_DIR}")
# convert JSON descriptors to c headers and extract processor IDs
set(SP_INCLUDES "")
set(BIG_IF "")
# The Mutable modules reuse the same LUT_* macro names with DIFFERENT values
# in their resources.h (e.g. tides2 LUT_SINE_SIZE=1281 vs rplaits 641,
# braids LUT_ENV_EXPO=14 vs elements 11). The aggregator header includes all
# plugin hpps into one TU, so later resources.h redefine earlier macros.
# GCC <= 14 emitted warnings; GCC >= 15 errors under -Werror. #undef before
# every include so each module's header is expanded with its OWN values —
# the effective semantics of the warning-only builds.
set(SP_LUT_MACROS "")
file(GLOB_RECURSE MUTABLE_RESOURCES
        ${CMAKE_SOURCE_DIR}/components/mutable/eurorack/*/resources.h
        ${CMAKE_SOURCE_DIR}/components/mutable/rubato/*/resources.h)
foreach (RSRC ${MUTABLE_RESOURCES})
    file(STRINGS ${RSRC} RSRC_DEFINES REGEX "^[ \t]*#define[ \t]+LUT_[A-Za-z_0-9]+")
    foreach (D ${RSRC_DEFINES})
        string(REGEX MATCH "LUT_[A-Za-z_0-9]+" M ${D})
        if (M)
            list(APPEND SP_LUT_MACROS ${M})
        endif ()
    endforeach ()
endforeach ()
if (SP_LUT_MACROS)
    list(REMOVE_DUPLICATES SP_LUT_MACROS)
endif ()
set(SP_UNDEFS "")
foreach (M ${SP_LUT_MACROS})
    string(APPEND SP_UNDEFS "#ifdef ${M}\n#undef ${M}\n#endif\n")
endforeach ()
foreach (VAR ${SOUND_PROCESSORS})
    get_filename_component(MYFILE_WITHOUT_EXT ${VAR} NAME_WE)
    message(STATUS "Processing ${MYFILE_WITHOUT_EXT}")
    string(REGEX REPLACE "\\.[^.]*$" "" MYFILE_WITHOUT_EXT ${MYFILE_WITHOUT_EXT}) # using regex instead of NAME_WLE
    string(REGEX REPLACE "ctagSoundProcessor+" "" SP_ID ${MYFILE_WITHOUT_EXT})
    set(SP_INCLUDES "${SP_INCLUDES}${SP_UNDEFS}#include \"${MYFILE_WITHOUT_EXT}.hpp\"\n")
    # prepare big if variable for factory
    set(BIG_IF "${BIG_IF}if(type.compare(\"${SP_ID}\") == 0) processor = new ${MYFILE_WITHOUT_EXT}();\n")
endforeach ()

# write sound processor descriptor json and convert it to header file
file(WRITE "${SP_GEN_INCLUDE_DIR}/ctagSoundProcessors.hpp" "${SP_INCLUDES}")

# create big if structure for factory
configure_file("${CMAKE_CURRENT_LIST_DIR}/ctagSoundProcessorFactory.hpp.in" "${SP_GEN_INCLUDE_DIR}/ctagSoundProcessorFactory.hpp" @ONLY)

# add sources to config, headers added for intellij
file(GLOB SRCS_FILES
        *.cpp
        *.hpp
        helpers/*.cpp
        helpers/*.hpp
        gverb/*.c
        gverb/*.h
        freeverb/*.cc
        freeverb/*.hh
        freeverb/*.hpp
        freeverb3/*.hpp
        freeverb3/*.cpp
        freeverb3/*.h
        mifx/*.h
        polypad/*.hpp
        polypad/*.cpp
        filters/*.hpp
        filters/*.cpp
        fx/*.hpp
        fx/*.cpp
        tesselode/*.h
        tesselode/*.cpp
        airwindows/*.hpp
        airwindows/*.cpp
        synthesis/*.hpp
        synthesis/*.cpp
        vult/*.cpp
        vult/*.h
        memory/*.h
        memory/*.c
        SimpleComp/*.h
        SimpleComp/*.cpp
        "${SP_GEN_INCLUDE_DIR}/*.hpp"
        )
# DrumRack removed from the bundle — keep it out of the compiled library too
list(FILTER SRCS_FILES EXCLUDE REGEX "ctagSoundProcessorDrumRack")
# Mbeads removed from the bundle — keep it out of the compiled library too
list(FILTER SRCS_FILES EXCLUDE REGEX "ctagSoundProcessorMbeads")

if (TBD_SIM)
    add_library(ctagsp STATIC ${SRCS_FILES})
    target_include_directories(ctagsp PRIVATE . "${SP_GEN_INCLUDE_DIR}")
else ()
    idf_component_register(SRCS ${SRCS_FILES}
            INCLUDE_DIRS . helpers "${SP_GEN_INCLUDE_DIR}"
            PRIV_REQUIRES rapidjson esp-dsp mutable moog spi_flash instrumentation)
    target_compile_options(${COMPONENT_LIB} PRIVATE
            -Wno-unused-local-typedefs
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-float-conversion>
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-volatile>
            -ffast-math
            )
endif ()


