# NassauBloom plugin bundles (thin wrapper over the Nassau SDK -> iPlug2).
#
# One call emits VST3 (.vst3), AU v2 (.component) and CLAP (.clap) from the
# same sources. The SDK picks the UI backend; -DNASSAU_FORCE_HEADLESS=ON forces
# the no-UI build, and NassauBloomUI.cpp, wholly inside #if IPLUG_EDITOR, then
# compiles to nothing (docs/GATES.md G9.3).
#
# ci/install.sh and ci/uninstall.sh read this call with sed: the plugin name
# must stay on the `nassau_add_plugin(` line and FORMATS on a line of its own,
# as in the sibling repos.

nassau_add_plugin(NassauBloom
    FORMATS VST3 AU CLAP
    SOURCES NassauBloomPlugin.cpp NassauBloomUI.cpp
    LINK    nassau_bloom_core)
