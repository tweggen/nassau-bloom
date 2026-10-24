#include "NassauBloomPlugin.h"
#include "IPlug_include_in_plug_src.h"
#include "nassau_presets.h"
#include "nassau_state.h"
#include <algorithm>
#include <cstdint>

using namespace iplug;

// nassau_presets.h's indices are plain ints (so the SDK-free tests can share
// them) and MUST match EParams value for value.
static_assert(nassau_presets::kMix == kMix, "preset index drift: kMix");
static_assert(nassau_presets::kInputHpf == kInputHpf, "preset index drift: kInputHpf");
static_assert(nassau_presets::kInputLpf == kInputLpf, "preset index drift: kInputLpf");
static_assert(nassau_presets::kPreDelay == kPreDelay, "preset index drift: kPreDelay");
static_assert(nassau_presets::kSize == kSize, "preset index drift: kSize");
static_assert(nassau_presets::kErLevel == kErLevel, "preset index drift: kErLevel");
static_assert(nassau_presets::kLateLevel == kLateLevel, "preset index drift: kLateLevel");
static_assert(nassau_presets::kDecay == kDecay, "preset index drift: kDecay");
static_assert(nassau_presets::kDampHigh == kDampHigh, "preset index drift: kDampHigh");
static_assert(nassau_presets::kDampLow == kDampLow, "preset index drift: kDampLow");
static_assert(nassau_presets::kDiffusion == kDiffusion, "preset index drift: kDiffusion");
static_assert(nassau_presets::kModulation == kModulation, "preset index drift: kModulation");
static_assert(nassau_presets::kWidth == kWidth, "preset index drift: kWidth");
static_assert(nassau_presets::kComplexity == kComplexity, "preset index drift: kComplexity");
static_assert(nassau_presets::kAlgorithm == kAlgorithm, "preset index drift: kAlgorithm");
static_assert(nassau_presets::kOutputGain == kOutputGain, "preset index drift: kOutputGain");
static_assert(nassau_presets::kParamCount == kNumParams, "preset index drift: kNumParams");
static_assert(nassau_presets::kPresetCount == kNumPresets, "preset index drift: kNumPresets");

Config NassauBloomPlugin::MakePluginConfig()
{
    return Config(
        PLUG_N_PARAMS,
        PLUG_N_PRESETS,
        PLUG_CHANNEL_IO,
        PLUG_NAME,
        PLUG_NAME,
        PLUG_MFR,
        PLUG_VERSION_HEX,
        PLUG_UNIQUE_ID,
        PLUG_MFR_ID,
        PLUG_LATENCY,
        PLUG_DOES_MIDI_IN,
        PLUG_DOES_MIDI_OUT,
        PLUG_DOES_MPE,
        PLUG_DOES_STATE_CHUNKS,
        kEffect,
        PLUG_HAS_UI,
        PLUG_WIDTH,
        PLUG_HEIGHT,
        false,
        // The editor's min/max width and height. NOT 0, 0, 0, 0 as in the
        // sibling repos: iPlug2 replays the remembered size through
        // IGraphics::Resize on every reopen, which clamps to these, so zeros
        // make every editor after the first one 0x0 (QBX-219, GATES G0.7).
        PLUG_WIDTH, PLUG_WIDTH, PLUG_HEIGHT, PLUG_HEIGHT,
        // BUNDLE_ID is iPlug2's per-format bundle identifier (e.g.
        // com.Nassau.audiounit.NassauBloom for AU). It MUST match each
        // format's CFBundleIdentifier: the AU Cocoa-UI query looks the bundle
        // up by this id, so a mismatch crashes auval.
        BUNDLE_ID,
        ""
    );
}

NassauBloomPlugin::NassauBloomPlugin(const InstanceInfo& info)
    // Qualify iplug::Plugin: in the CLAP build the CLAP-helpers headers also
    // define a `Plugin` template, so the unqualified name would be ambiguous.
    : iplug::Plugin(info, MakePluginConfig())
{
    // ---- Parameters, from the one table in nassau_presets.h -----------------
    for (int i = 0; i < kNumParams; ++i) {
        const auto& p = nassau_presets::Params()[static_cast<size_t>(i)];
        IParam* param = GetParam(i);
        switch (p.kind) {
            case nassau_presets::Kind::Enum:
                param->InitEnum(p.name, static_cast<int>(p.defaultValue),
                                {p.enumLabels[0], p.enumLabels[1]});
                break;
            case nassau_presets::Kind::Frequency:
                param->InitFrequency(p.name, p.defaultValue, p.minValue, p.maxValue, p.step);
                break;
            case nassau_presets::Kind::Double:
                if (p.shape != 1.0)
                    param->InitDouble(p.name, p.defaultValue, p.minValue, p.maxValue, p.step,
                                      p.unit, 0, "", IParam::ShapePowCurve(p.shape));
                else
                    param->InitDouble(p.name, p.defaultValue, p.minValue, p.maxValue, p.step,
                                      p.unit);
                break;
        }
    }

    // ---- Factory preset bank, from the one table in nassau_presets.h --------
    // Each preset is the raw positional-double layout MakePresetFromNamedParams
    // builds internally, so MakePresetFromChunk reproduces it exactly and the
    // recall goes through UnserializeState's unframed legacy path (the
    // nassau-zermatt pattern; GATES G8.1 checks the recall).
    for (const auto& preset : nassau_presets::Presets()) {
        IByteChunk chunk;
        for (double v : preset.values) chunk.Put(&v);
        MakePresetFromChunk(preset.name, chunk);
    }
    assert(NPresets() == kNumPresets && "Preset count out of sync with PLUG_N_PRESETS");
}

void NassauBloomPlugin::OnReset()
{
    const double sr = GetSampleRate();
    if (sr >= 8000. && sr <= 192000.) {
        mCore.init(static_cast<float>(sr));
        // Re-push all params so the freshly-initialised core matches host state.
        for (int i = 0; i < kNumParams; ++i)
            OnParamChange(i);
    }
}

void NassauBloomPlugin::OnParamChange(int p)
{
    const IParam* param = GetParam(p);
    const float f = static_cast<float>(param->Value());
    switch (p) {
        case kMix:        mCore.setMix(f);                   break;
        case kInputHpf:   mCore.setInputHpf(f);              break;
        case kInputLpf:   mCore.setInputLpf(f);              break;
        case kPreDelay:   mCore.setPreDelayMs(f);            break;
        case kSize:       mCore.setSize(f);                  break;
        case kErLevel:    mCore.setErLevel(f);               break;
        case kLateLevel:  mCore.setLateLevel(f);             break;
        case kDecay:      mCore.setDecay(f);                 break;
        case kDampHigh:   mCore.setDampHigh(f);              break;
        case kDampLow:    mCore.setDampLow(f);               break;
        case kDiffusion:  mCore.setDiffusion(f);             break;
        case kModulation: mCore.setModulation(f);            break;
        case kWidth:      mCore.setWidth(f);                 break;
        case kComplexity: mCore.setComplexity(param->Int()); break;
        case kAlgorithm:  mCore.setAlgorithm(param->Int());  break;
        case kOutputGain:     mCore.setOutputDb(f);              break;
        default: break;
    }
}

void NassauBloomPlugin::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
    // PLUG_CHANNEL_IO "1-2 2-2": with one input connected, the core still
    // gets two channels, both the same signal (DESIGN.md §1.2). The tail's
    // stereo comes from the network, not from the input.
    const int  n      = std::min(nFrames, kMaxBlockSize);
    const bool monoIn = NInChansConnected() < 2;
    for (int i = 0; i < n; ++i) {
        mInL[i] = static_cast<float>(inputs[0][i]);
        mInR[i] = static_cast<float>(inputs[monoIn ? 0 : 1][i]);
    }
    mCore.process(mInL, mInR, mOutL, mOutR, n);
    for (int i = 0; i < n; ++i) {
        outputs[0][i] = static_cast<sample>(mOutL[i]);
        outputs[1][i] = static_cast<sample>(mOutR[i]);
    }
}

// ---- Versioned, forward-compatible state ------------------------------------
// Layout (see nassau_state.h): magic 'NsBl', uint32 version, uint32 paramCount,
// then paramCount doubles via the base SerializeParams.

bool NassauBloomPlugin::SerializeState(IByteChunk& chunk) const
{
    chunk.PutBytes(nassau_state::kMagic, 4);
    const uint32_t version = nassau_state::kStateVersion;
    const uint32_t count   = static_cast<uint32_t>(NParams());
    chunk.Put(&version);
    chunk.Put(&count);
    return SerializeParams(chunk);
}

int NassauBloomPlugin::UnserializeState(const IByteChunk& chunk, int startPos)
{
    // Read + validate the magic. On mismatch, fall back to a raw param chunk.
    // That is ALSO the path a factory preset takes: MakePresetFromChunk above
    // stores unframed raw doubles.
    unsigned char magic[4] = {0, 0, 0, 0};
    int pos = chunk.GetBytes(magic, 4, startPos);
    if (pos < 0 || !nassau_state::MagicMatches(magic))
        return UnserializeParams(chunk, startPos);

    uint32_t version = 0, storedCount = 0;
    pos = chunk.Get(&version, pos);
    pos = chunk.Get(&storedCount, pos);
    if (pos < 0)
        return pos;
    (void) version; // only one framing version so far; kept for future migration.

    const auto plan = nassau_state::PlanUnserialize(
        storedCount, static_cast<uint32_t>(NParams()));

    ENTER_PARAMS_MUTEX
    for (int i = 0; i < plan.paramsToRead && pos >= 0; ++i) {
        double v = 0.0;
        pos = chunk.Get(&v, pos);
        if (pos >= 0)
            GetParam(i)->Set(v);
    }
    LEAVE_PARAMS_MUTEX

    // Skip any trailing params a NEWER build wrote that this build doesn't know.
    if (pos >= 0 && plan.skipBytes > 0)
        pos += plan.skipBytes;

    OnParamReset(kPresetRecall);
    return pos;
}
