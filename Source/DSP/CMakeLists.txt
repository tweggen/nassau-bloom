add_library(nassau_bloom_core
    bloom_core.cpp
    bloom_core.h
)

target_include_directories(nassau_bloom_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(nassau_bloom_core PRIVATE ${NASSAU_WARNING_FLAGS})

# Release builds of the core at the SDK's portable optimisation level (MSVC
# Release already implies /O2, so NASSAU_RELEASE_OPT_FLAGS is empty there).
# NO fast-math: the late network is a feedback loop (docs/GATES.md R7).
target_compile_options(nassau_bloom_core PRIVATE
    $<$<CONFIG:Release>:${NASSAU_RELEASE_OPT_FLAGS}>)
