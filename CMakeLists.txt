cmake_minimum_required(VERSION 3.15)
project(NassauBloom VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Locate the shared Nassau plugin SDK and apply its project prologue (the
# OBJC/OBJCXX enablement, the macOS deployment target, NASSAU_WARNING_FLAGS /
# NASSAU_RELEASE_OPT_FLAGS, and the MSVC /MT runtime selection). Same file,
# byte for byte, as nassau-eq / nassau-analogue / nassau-zermatt.
#
# Two constraints from the SDK's cmake/NassauPluginProject.cmake: this must
# come BEFORE the first add_subdirectory() (CMAKE_MSVC_RUNTIME_LIBRARY seeds
# each target's property at creation time, LNK2038 otherwise), and it must be
# included from THIS top-level scope (enable_language(OBJC) for iPlug2).
#
# Resolution order: -DNASSAU_SDK_DIR=..., then a sibling ../nassau-plugin-sdk,
# then FetchContent. Finding no SDK is not an error:
# NASSAU_PLUGIN_TARGETS_POSSIBLE goes false and the DSP core and tests build
# anyway.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/NassauSDK.cmake)

# DSP core (always builds; no framework dependencies).
add_subdirectory(Source/DSP)

# Plugin bundles: VST3 + AU + CLAP on macOS, VST3 + CLAP on Windows, none
# elsewhere. NassauPlugin.cmake pulls in iPlug2's own CMake system at include
# time, which is the other reason this stays at top-level scope.
if(NASSAU_PLUGIN_TARGETS_POSSIBLE)
    include(${NASSAU_SDK_DIR}/cmake/NassauPlugin.cmake)
    add_subdirectory(Source/Plugin)
endif()

# Tests (hand-rolled harness, always builds).
enable_testing()
add_subdirectory(Tests)
