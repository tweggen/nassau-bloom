# ---- G0: the core's bit-exact dry path -------------------------------------
add_executable(core_tests core_tests.cpp)
target_link_libraries(core_tests nassau_bloom_core)
target_compile_options(core_tests PRIVATE ${NASSAU_WARNING_FLAGS})
add_test(NAME CoreTests COMMAND core_tests)

# ---- G0: versioned-state framing helper ------------------------------------
# Pure, dependency-free: nassau_state.h carries no iPlug2 includes, so this
# builds and runs with no SDK. Covers same-count round-trip, fewer-stored
# (defaults kept), more-stored (tail skipped, no overread) and the bad-magic
# legacy fallback that factory presets also take.
add_executable(state_tests state_tests.cpp)
target_include_directories(state_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source/Plugin)
target_compile_options(state_tests PRIVATE ${NASSAU_WARNING_FLAGS})
add_test(NAME StateTests COMMAND state_tests)

# ---- G0 (G7 extends it): the parameter table and the factory presets -------
# nassau_presets.h is SDK-free and is the one copy the plugin reads too.
add_executable(preset_tests preset_tests.cpp)
target_link_libraries(preset_tests nassau_bloom_core)
target_include_directories(preset_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source/Plugin)
target_compile_options(preset_tests PRIVATE ${NASSAU_WARNING_FLAGS})
add_test(NAME PresetTests COMMAND preset_tests)

# ---- G1: DSP primitives (Source/DSP/bloom_dsp.h) ---------------------------
# Header-only; no link against the core needed.
add_executable(dsp_tests dsp_tests.cpp)
target_include_directories(dsp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source/DSP)
target_compile_options(dsp_tests PRIVATE ${NASSAU_WARNING_FLAGS})
add_test(NAME DspTests COMMAND dsp_tests)

# ---- G2: the wet path with the reverb paths still a wire --------------------
add_executable(wetpath_tests wetpath_tests.cpp)
target_link_libraries(wetpath_tests nassau_bloom_core)
target_compile_options(wetpath_tests PRIVATE ${NASSAU_WARNING_FLAGS})
add_test(NAME WetPathTests COMMAND wetpath_tests)

# ---- G3: early reflections (bloom_er.h and the ER path) -------------------
add_executable(er_tests er_tests.cpp)
target_link_libraries(er_tests nassau_bloom_core)
target_compile_options(er_tests PRIVATE ${NASSAU_WARNING_FLAGS})
add_test(NAME ErTests COMMAND er_tests)

# ---- G4: the Tank ---------------------------------------------------------
add_executable(tank_tests tank_tests.cpp)
target_link_libraries(tank_tests nassau_bloom_core)
target_compile_options(tank_tests PRIVATE ${NASSAU_WARNING_FLAGS})
add_test(NAME TankTests COMMAND tank_tests)

# ---- G5: the Spring -------------------------------------------------------
add_executable(spring_tests spring_tests.cpp)
target_link_libraries(spring_tests nassau_bloom_core)
target_compile_options(spring_tests PRIVATE ${NASSAU_WARNING_FLAGS})
add_test(NAME SpringTests COMMAND spring_tests)

# ---- G6: switching Algorithm and Complexity ----------------------------------
add_executable(switch_tests switch_tests.cpp)
target_link_libraries(switch_tests nassau_bloom_core)
target_compile_options(switch_tests PRIVATE ${NASSAU_WARNING_FLAGS})
add_test(NAME SwitchTests COMMAND switch_tests)

# ---- G6.4: golden-reference parity (the nassau-eq pattern) -----------------
# bloom_golden generate|verify. The committed fixture (fixtures/golden.bin) is
# the frozen output of every preset; the GoldenParity ctest runs `verify`.
set(GOLDEN_FIXTURE "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/golden.bin")
add_executable(bloom_golden bloom_golden.cpp)
target_link_libraries(bloom_golden nassau_bloom_core)
target_include_directories(bloom_golden PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source/Plugin)
target_compile_options(bloom_golden PRIVATE ${NASSAU_WARNING_FLAGS})
target_compile_definitions(bloom_golden PRIVATE GOLDEN_FIXTURE_PATH="${GOLDEN_FIXTURE}")
# fopen() is the portable spelling; MSVC would have fopen_s.
if(MSVC)
  target_compile_definitions(bloom_golden PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME GoldenParity COMMAND bloom_golden verify "${GOLDEN_FIXTURE}")

# ---- G6.5 / G6.3: benchmark (NOT a ctest; run by hand) ---------------------
# CPU per preset and memory per instance, from a steady clock: a benchmark,
# not part of the pass/fail suite.
add_executable(bloom_bench bloom_bench.cpp)
target_link_libraries(bloom_bench nassau_bloom_core)
target_include_directories(bloom_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source/Plugin)
target_compile_options(bloom_bench PRIVATE ${NASSAU_WARNING_FLAGS})
