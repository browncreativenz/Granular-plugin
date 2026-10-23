cmake_minimum_required(VERSION 3.22)

# Must be set before project(). 11.0 is the first macOS with Apple Silicon.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(GranularPlugin VERSION 0.4.1 LANGUAGES C CXX)

option(GRANULAR_BUILD_PLUGIN "Build the JUCE plugin (fetches JUCE)" ON)
option(GRANULAR_BUILD_TESTS  "Build the engine tests"               ON)
option(GRANULAR_BUILD_TOOLS  "Build the command-line renderer"      ON)
option(GRANULAR_COPY_PLUGIN  "Install the plugin into the user plug-in folders after building" ON)

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
endif()

if(GRANULAR_BUILD_PLUGIN)
    # JUCE 8 (AGPLv3 for open-source use). To build against a JUCE checkout you
    # already have, pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE.
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.16-security
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(plugin)
endif()

# After the plugin, so the tests can use its target when it is built.
if(GRANULAR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
