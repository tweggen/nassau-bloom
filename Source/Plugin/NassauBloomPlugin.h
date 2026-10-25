#pragma once
#include "config.h"
#include "IPlug_include_in_plug_hdr.h"
#include "bloom_core.h"

#if IPLUG_EDITOR
#include "NassauBloomUI.h"   // guarded: not present in a headless build
#endif

// The parameter surface of docs/DESIGN.md §10: 16 params, final from G0 on.
// Names, ranges and defaults live in nassau_presets.h (Params()), which the
// ctor reads, so this enum is the only other place an index is written down.
//
// APPEND-ONLY PARAM DISCIPLINE (state compatibility): params are serialized
// positionally by enum index (see nassau_state.h). NEW params are APPENDED
// immediately before kNumParams and NEVER reordered or inserted in the
// middle. Bump PLUG_N_PARAMS in config.h and nassau_presets::kParamCount in
// the same commit (GATES R4).
enum EParams {
    kMix = 0,
    kInputHpf,
    kInputLpf,
    kPreDelay,
    kSize,
    kErLevel,
    kLateLevel,
    kDecay,
    kDampHigh,
    kDampLow,
    kDiffusion,
    kModulation,
    kWidth,
    kComplexity,
    kAlgorithm,
    kOutputGain,
    kNumParams   // == 16
};
static_assert(kNumParams == PLUG_N_PARAMS, "Parameter count mismatch");

// Factory preset count. Kept in lockstep with PLUG_N_PRESETS (config.h) and
// nassau_presets::kPresetCount (static_asserted in the .cpp).
static constexpr int kNumPresets = 9;
static_assert(kNumPresets == PLUG_N_PRESETS, "Preset count mismatch");

class NassauBloomPlugin final : public iplug::Plugin {
public:
    explicit NassauBloomPlugin(const iplug::InstanceInfo& info);

    void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
    void OnReset() override;
    void OnParamChange(int paramIdx) override;

    // Versioned, forward-compatible state. See nassau_state.h for the chunk
    // layout and the tolerant read rules.
    bool SerializeState(iplug::IByteChunk& chunk) const override;
    int  UnserializeState(const iplug::IByteChunk& chunk, int startPos) override;

private:
    static iplug::Config MakePluginConfig();

    BloomCore mCore;
    static constexpr int kMaxBlockSize = 8192;
    float mInL[kMaxBlockSize], mInR[kMaxBlockSize];
    float mOutL[kMaxBlockSize], mOutR[kMaxBlockSize];
};
