cmake_minimum_required(VERSION 3.22)

project(GranularPlugin VERSION 0.1.0 LANGUAGES CXX)

option(GRANULAR_BUILD_TESTS  "Build the engine tests"            ON)
option(GRANULAR_BUILD_TOOLS  "Build the command-line renderer"   ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CAPICOLA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/capicola")
if(NOT EXISTS "${CAPICOLA_DIR}/lib/KeyframeRecorder.h")
    message(FATAL_ERROR
        "Capicola firmware not found. Run:\n"
        "  git submodule update --init external/capicola")
endif()

# The firmware's DSP core: header-only, standard C++ only. SYSTEM keeps its
# warnings out of our builds.
add_library(capicola_dsp INTERFACE)
target_include_directories(capicola_dsp SYSTEM INTERFACE "${CAPICOLA_DIR}/lib")

# Floating-point contraction (FMA) is off everywhere so the port and the
# firmware engine round identically and the golden test can demand bit-exact
# output on any compiler.
set(GRANULAR_FP_FLAGS
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off>)

add_library(granular_core STATIC
    core/CapicolaEngine.cpp
    core/CapicolaProcessor.cpp)
target_include_directories(granular_core PUBLIC core)
target_link_libraries(granular_core PUBLIC capicola_dsp)
target_compile_options(granular_core PRIVATE
    ${GRANULAR_FP_FLAGS}
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)

if(GRANULAR_BUILD_TOOLS)
    add_executable(capicola_render tools/render/capicola_render.cpp)
    target_include_directories(capicola_render PRIVATE tools tools/render)
    target_link_libraries(capicola_render PRIVATE granular_core)
    target_compile_options(capicola_render PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)
endif()

if(GRANULAR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
