#pragma once

#define PLUG_NAME        "NassauBloom"
#define PLUG_MFR         "Nassau"
#define PLUG_VERSION_HEX  0x00000100
#define PLUG_VERSION_STR  "0.1.0"

#define PLUG_URL_STR       "https://github.com/tweggen/nassau-bloom"
#define PLUG_EMAIL_STR     "support@nassau.audio"
#define PLUG_COPYRIGHT_STR "Copyright 2026 Nassau"

#define PLUG_CLASS_NAME   NassauBloomPlugin

#define BUNDLE_NAME       "NassauBloom"
#define BUNDLE_MFR        "Nassau"
#define BUNDLE_DOMAIN     "com"
// NassauBloomPlugin.cpp passes iPlug2's per-format BUNDLE_ID (derived from
// BUNDLE_DOMAIN/BUNDLE_MFR/BUNDLE_NAME) to iplug::Config — not a hand-set id.

// 4-char codes — unique per plugin (AU + some VST3 hosts key on these).
#define PLUG_UNIQUE_ID    'Nbl1'
#define PLUG_MFR_ID       'Nass'

// Fresh 32-hex VST3 UID, unique to NassauBloom (generated for G0).
#define PLUG_UID_STR  "F9E8848907B94BAB8A60FA3A313E5E74"

// Mono in, stereo out is where a reverb on a mono source lives; the wrapper
// copies the one input to both core channels (docs/DESIGN.md §1.2).
#define PLUG_CHANNEL_IO        "1-2 2-2"
#define PLUG_LATENCY           0
// PLUG_TYPE: 0 = effect (aufx / CLAP audio-effect), 1 = instrument, 2 = MIDI fx.
#define PLUG_TYPE              0
#define PLUG_DOES_MIDI_IN      0
#define PLUG_DOES_MIDI_OUT     0
#define PLUG_DOES_MPE          0
#define PLUG_DOES_STATE_CHUNKS 0

// The 16-param surface of docs/DESIGN.md §10. The enum order lives in
// NassauBloomPlugin.h and is kept in lockstep via a static_assert.
#define PLUG_N_PARAMS   16
// Factory preset bank (docs/DESIGN.md §11). MUST equal kNumPresets in
// NassauBloomPlugin.h and nassau_presets::kPresetCount (both static_asserted).
#define PLUG_N_PRESETS  9

// PLUG_HAS_UI advertises the editor (GATES G9); its code is gated by
// IPLUG_EDITOR, which a forced-headless build sets to 0, so that build still
// links and simply reports no editor. The size is NassauBloomUI.cpp's layout
// (two rows of panels under a header), and the Config passes it as the min AND
// max editor size (G0.7): never 0, which iPlug2 clamps every reopened editor
// to (QBX-219).
#define PLUG_HAS_UI     1
#define PLUG_WIDTH      580
#define PLUG_HEIGHT     292
#define PLUG_FPS        60

#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0

// ---- VST3 -------------------------------------------------------------------
#define VST3_SUBCATEGORY "Fx|Reverb"

// ---- AUv2 (AudioComponents) -------------------------------------------------
// aufx effect; AU subtype == PLUG_UNIQUE_ID ('Nbl1'), manufacturer == PLUG_MFR_ID
// ('Nass'). The entry/factory/view names below must match the strings in
// resources/NassauBloom-AU-Info.plist (factoryFunction / NSPrincipalClass).
#define AUV2_ENTRY          NassauBloom_Entry
#define AUV2_ENTRY_STR      "NassauBloom_Entry"
#define AUV2_FACTORY        NassauBloom_Factory
#define AUV2_VIEW_CLASS     NassauBloom_View
#define AUV2_VIEW_CLASS_STR "NassauBloom_View"

// ---- CLAP -------------------------------------------------------------------
// iPlug2 derives the CLAP plugin id from BUNDLE_DOMAIN.BUNDLE_MFR.BUNDLE_NAME
// (-> "com.Nassau.NassauBloom"), the name/vendor/url/version from the PLUG_*
// macros above, and the feature list from CLAP_FEATURES below.
#define CLAP_MANUAL_URL  "https://github.com/tweggen/nassau-bloom"
#define CLAP_SUPPORT_URL "https://github.com/tweggen/nassau-bloom/issues"
#define CLAP_DESCRIPTION "Simple stereo reverb: early reflections, Lexicon-style tank, spring"
#define CLAP_FEATURES    "audio-effect", "reverb", "stereo"
