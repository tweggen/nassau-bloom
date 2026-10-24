# NassauBloom plugin bundles (thin wrapper over the Nassau SDK -> iPlug2).
#
# One call emits VST3 (.vst3), AU v2 (.component) and CLAP (.clap) from the
# same sources. PLUG_HAS_UI is 0 until the editor gate (docs/GATES.md G9), so
# there is no NassauBloomUI.cpp yet and hosts show their generic editor.
#
# ci/install.sh and ci/uninstall.sh read this call with sed: the plugin name
# must stay on the `nassau_add_plugin(` line and FORMATS on a line of its own,
# as in the sibling repos.

nassau_add_plugin(NassauBloom
    FORMATS VST3 AU CLAP
    SOURCES NassauBloomPlugin.cpp
    LINK    nassau_bloom_core)
