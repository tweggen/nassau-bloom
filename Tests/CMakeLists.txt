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
