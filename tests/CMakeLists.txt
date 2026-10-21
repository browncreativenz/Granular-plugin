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
