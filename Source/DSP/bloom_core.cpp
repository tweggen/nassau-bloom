#include "bloom_core.h"

#include <algorithm>
#include <cmath>

using bloom::kPi;

namespace {

float clampf(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }

constexpr double kHpfBypassHz = 20.0;     // DESIGN.md §2
constexpr double kLpfBypassHz = 20000.0;  // DESIGN.md §2
constexpr double kMaxPreDelayMs = 250.0;  // DESIGN.md §3

// DESIGN.md §9.1 time constants.
constexpr double kGainTau = 0.020;
constexpr double kPreDelayTau = 0.050;
constexpr double kFilterTau = 0.030;

}  // namespace

BloomCore::BloomCore() = default;
BloomCore::~BloomCore() = default;

void BloomCore::init(float sampleRate)
{
    sampleRate_ = clampf(sampleRate, 8000.0f, 192000.0f);
    fs_ = static_cast<double>(sampleRate_);

    preMax_ = std::ceil(kMaxPreDelayMs / 1000.0 * fs_);
    for (auto& d : pre_) d.init(static_cast<int>(preMax_) + 4);

    const double subBlockRate = fs_ / kSubBlock;
    dryGain_.setTimeConstant(kGainTau, fs_);
    wetGain_.setTimeConstant(kGainTau, fs_);
    outGain_.setTimeConstant(kGainTau, fs_);
    width_s_.setTimeConstant(kGainTau, fs_);
    preDelay_.setTimeConstant(kPreDelayTau, fs_);
    hpfHz_.setTimeConstant(kFilterTau, subBlockRate);
    lpfHz_.setTimeConstant(kFilterTau, subBlockRate);

    reset();
}

void BloomCore::reset()
{
    for (auto& f : hpf_) f.reset();
    for (auto& f : lpf_) f.reset();
    for (auto& d : pre_) d.reset();
    hpfOn_ = lpfOn_ = false;
    // The smoothers are snapped by the next process(), from the targets as
    // they are THEN (DESIGN.md §9.6).
    snapPending_ = true;
}

// Load the parameter targets, step the per-sub-block smoothers and set the
// coefficients. The only place the audio path calls a transcendental (R5).
void BloomCore::beginSubBlock()
{
    const double mix = static_cast<double>(mix_.load(std::memory_order_relaxed));
    // Equal-power mix (DESIGN.md §8). The end points are set exactly:
    // cos(π/2) is 6.1e-17 in double, not 0, and the wet-only output must be
    // exactly the wet.
    double dry, wet;
    if (mix <= 0.0)        { dry = 1.0; wet = 0.0; }
    else if (mix >= 100.0) { dry = 0.0; wet = 1.0; }
    else {
        const double theta = mix / 100.0 * kPi / 2.0;
        dry = std::cos(theta);
        wet = std::sin(theta);
    }
    dryGain_.setTarget(dry);
    wetGain_.setTarget(wet);
    const double outDb = static_cast<double>(outputDb_.load(std::memory_order_relaxed));
    outGain_.setTarget(outDb == 0.0 ? 1.0 : std::pow(10.0, outDb / 20.0));
    width_s_.setTarget(static_cast<double>(width_.load(std::memory_order_relaxed)) / 100.0);
    preDelay_.setTarget(std::min(preMax_,
        static_cast<double>(preDelayMs_.load(std::memory_order_relaxed)) / 1000.0 * fs_));
    hpfHz_.setTarget(static_cast<double>(inputHpf_.load(std::memory_order_relaxed)));
    lpfHz_.setTarget(static_cast<double>(inputLpf_.load(std::memory_order_relaxed)));

    if (snapPending_) {
        for (bloom::Smoother* s : {&dryGain_, &wetGain_, &outGain_, &width_s_, &preDelay_, &hpfHz_, &lpfHz_})
            s->snap();
        snapPending_ = false;
    } else {
        hpfHz_.step();
        lpfHz_.step();
    }

    // Input filters (DESIGN.md §2). Bypassed exactly at the end of the range;
    // switching in starts from zeroed state, deliberately.
    const bool hpfOn = hpfHz_.value > kHpfBypassHz;
    const bool lpfOn = lpfHz_.value < kLpfBypassHz;
    if (hpfOn && !hpfOn_) for (auto& f : hpf_) f.reset();
    if (lpfOn && !lpfOn_) for (auto& f : lpf_) f.reset();
    hpfOn_ = hpfOn;
    lpfOn_ = lpfOn;
    if (hpfOn_) for (auto& f : hpf_) f.setHighpass(hpfHz_.value, fs_);
    if (lpfOn_) {
        const double fc = std::min(lpfHz_.value, 0.45 * fs_);
        for (auto& f : lpf_) f.setLowpass(fc, fs_);
    }
}

void BloomCore::process(const float* inL, const float* inR,
                        float* outL, float* outR,
                        int numSamples)
{
    bloom::ScopedNoDenormals noDenormals;

    const float* testL = testWetL_;
    const float* testR = testWetR_;
    testWetL_ = testWetR_ = nullptr;

    for (int start = 0; start < numSamples; start += kSubBlock) {
        const int end = std::min(numSamples, start + kSubBlock);
        beginSubBlock();

        for (int i = start; i < end; ++i) {
            // Read both inputs before writing: out may alias in.
            const float dryL = inL[i];
            const float dryR = inR[i];

            // ---- Wet path: input filters, pre-delay ----
            float xL = dryL, xR = dryR;
            if (hpfOn_) { xL = hpf_[0].process(xL); xR = hpf_[1].process(xR); }
            if (lpfOn_) { xL = lpf_[0].process(xL); xR = lpf_[1].process(xR); }

            // Write, then read: tap(1) is the sample just written, so a delay
            // of D samples is tapLinear(D + 1), and D = 0 is exact (§3).
            const double d = preDelay_.step() + 1.0;
            pre_[0].write(xL);
            pre_[1].write(xR);
            xL = pre_[0].tapLinear(d);
            xR = pre_[1].tapLinear(d);

            // ---- The reverb paths: a WIRE until G3–G5 ----
            float wL = xL, wR = xR;
            if (testL) { wL = testL[i]; wR = testR[i]; }

            // ---- Width (DESIGN.md §7), on the wet only; skipped at 100 % ----
            const double w = width_s_.step();
            if (w != 1.0) {
                const float m = 0.5f * (wL + wR);
                const float s = 0.5f * (wL - wR) * static_cast<float>(w);
                wL = m + s;
                wR = m - s;
            }

            // ---- Mix and output (DESIGN.md §8) ----
            const double gD = dryGain_.step();
            const double gW = wetGain_.step();
            const double gO = outGain_.step();
            // The end points ASSIGN rather than add to zero, so they are
            // bit-exact even for a −0.0 sample (0.0 + −0.0 is +0.0).
            double yL, yR;
            if (gW == 0.0)      { yL = gD * dryL; yR = gD * dryR; }
            else if (gD == 0.0) { yL = gW * wL;   yR = gW * wR; }
            else                { yL = gD * dryL + gW * wL; yR = gD * dryR + gW * wR; }
            outL[i] = static_cast<float>(yL * gO);
            outR[i] = static_cast<float>(yR * gO);
        }
    }
}

void BloomCore::setMix(float v)        { mix_.store(clampf(v, 0.0f, 100.0f), std::memory_order_relaxed); }
void BloomCore::setInputHpf(float v)   { inputHpf_.store(clampf(v, 20.0f, 1000.0f), std::memory_order_relaxed); }
void BloomCore::setInputLpf(float v)   { inputLpf_.store(clampf(v, 1000.0f, 20000.0f), std::memory_order_relaxed); }
void BloomCore::setPreDelayMs(float v) { preDelayMs_.store(clampf(v, 0.0f, 250.0f), std::memory_order_relaxed); }
void BloomCore::setSize(float v)       { size_.store(clampf(v, 2.0f, 40.0f), std::memory_order_relaxed); }
void BloomCore::setErLevel(float v)    { erLevel_.store(clampf(v, 0.0f, 200.0f), std::memory_order_relaxed); }
void BloomCore::setLateLevel(float v)  { lateLevel_.store(clampf(v, 0.0f, 200.0f), std::memory_order_relaxed); }
void BloomCore::setDecay(float v)      { decay_.store(clampf(v, 0.2f, 20.0f), std::memory_order_relaxed); }
void BloomCore::setDampHigh(float v)   { dampHigh_.store(clampf(v, 1000.0f, 20000.0f), std::memory_order_relaxed); }
void BloomCore::setDampLow(float v)    { dampLow_.store(clampf(v, 20.0f, 1000.0f), std::memory_order_relaxed); }
void BloomCore::setDiffusion(float v)  { diffusion_.store(clampf(v, 0.0f, 100.0f), std::memory_order_relaxed); }
void BloomCore::setModulation(float v) { modulation_.store(clampf(v, 0.0f, 100.0f), std::memory_order_relaxed); }
void BloomCore::setWidth(float v)      { width_.store(clampf(v, 0.0f, 200.0f), std::memory_order_relaxed); }
void BloomCore::setOutputDb(float v)   { outputDb_.store(clampf(v, -24.0f, 12.0f), std::memory_order_relaxed); }

void BloomCore::setComplexity(int v)
{
    complexity_.store(v == kComplexityDense ? kComplexityDense : kComplexityPlain, std::memory_order_relaxed);
}

void BloomCore::setAlgorithm(int v)
{
    algorithm_.store(v == kAlgorithmSpring ? kAlgorithmSpring : kAlgorithmTank, std::memory_order_relaxed);
}
