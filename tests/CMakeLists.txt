# Engine tests.
#
#   cmake -B build && cmake --build build && ctest --test-dir build

# The firmware's own host test suite for the DSP core, unchanged.
add_executable(firmware_tests "${CAPICOLA_DIR}/tests/host/capicola_tests.cpp")
target_include_directories(firmware_tests PRIVATE
    "${CAPICOLA_DIR}/lib"
    "${CAPICOLA_DIR}/src")   # jack_routing.h
add_test(NAME firmware_host_tests COMMAND firmware_tests)

# Port vs. firmware engine, bit for bit. The firmware's audio_engine.cpp is
# compiled through firmware_stubs/ in place of libDaisy.
add_executable(engine_golden
    engine_golden.cpp
    "${CAPICOLA_DIR}/src/audio/audio_engine.cpp")
target_include_directories(engine_golden PRIVATE
    firmware_stubs             # daisy_seed.h, util/CpuLoadMeter.h
    "${CAPICOLA_DIR}/src"      # audio/audio_engine.h
    "${CAPICOLA_DIR}/src/hal"  # hardware.h
    "${CMAKE_SOURCE_DIR}/tools")
target_link_libraries(engine_golden PRIVATE granular_core)
target_compile_options(engine_golden PRIVATE ${GRANULAR_FP_FLAGS})

foreach(scenario 0 1 2 3)
    add_test(NAME engine_golden_${scenario} COMMAND engine_golden ${scenario})
endforeach()

# Knob tapers and factory defaults against the firmware's documented values.
add_executable(params_test params_test.cpp)
target_link_libraries(params_test PRIVATE granular_core)
add_test(NAME params COMMAND params_test)

# Sample-rate conversion around the 48 kHz engine.
add_executable(resampler_test resampler_test.cpp)
target_link_libraries(resampler_test PRIVATE granular_core)
add_test(NAME resampler COMMAND resampler_test)

# The host-facing processor: wiring, modulation, other rates.
add_executable(processor_test processor_test.cpp)
target_include_directories(processor_test PRIVATE "${CMAKE_SOURCE_DIR}/tools")
target_link_libraries(processor_test PRIVATE granular_core)
add_test(NAME processor COMMAND processor_test)

# The JUCE wrapper against the core, when the plugin is being built. Uses the
# plugin's shared-code library with its include paths and definitions.
if(TARGET Capicola)
    add_executable(plugin_test plugin_test.cpp)
    target_include_directories(plugin_test PRIVATE
        "${CMAKE_SOURCE_DIR}/plugin"
        "${CMAKE_SOURCE_DIR}/tools"
        $<TARGET_PROPERTY:Capicola,INCLUDE_DIRECTORIES>)
    target_compile_definitions(plugin_test PRIVATE
        $<TARGET_PROPERTY:Capicola,COMPILE_DEFINITIONS>)
    target_link_libraries(plugin_test PRIVATE Capicola)
    # The checks compare floats exactly on purpose.
    target_compile_options(plugin_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-float-equal>)
    add_test(NAME plugin COMMAND plugin_test)
endif()
