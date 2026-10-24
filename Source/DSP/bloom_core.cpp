#include "bloom_core.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

float clampf(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }

}  // namespace

BloomCore::BloomCore() = default;
BloomCore::~BloomCore() = default;

void BloomCore::init(float sampleRate)
{
    sampleRate_ = clampf(sampleRate, 8000.0f, 192000.0f);
    // G0: no buffers yet. G2 onward allocate the delay lines here, for
    // sampleRate_ and the largest Size (DESIGN.md §9.5).
    reset();
}

void BloomCore::reset()
{
    // G0: no state yet.
}

void BloomCore::process(const float* inL, const float* inR,
                        float* outL, float* outR,
                        int numSamples)
{
    // G0: the mix law of DESIGN.md §8 with a wet signal of zero. At Mix 0 and
    // Output 0 dB both factors are exactly 1.0, so the output is bit-exact
    // to the input (GATES G0.4). Unsmoothed: G2 adds the per-sample
    // smoothers of DESIGN.md §9.1.
    const double theta = static_cast<double>(mix_.load(std::memory_order_relaxed)) / 100.0 * kPi / 2.0;
    const double outGain = std::pow(10.0, static_cast<double>(outputDb_.load(std::memory_order_relaxed)) / 20.0);
    const float dryGain = static_cast<float>(std::cos(theta) * outGain);

    for (int i = 0; i < numSamples; ++i) {
        // Read both inputs before writing: out may alias in.
        const float l = inL[i];
        const float r = inR[i];
        outL[i] = l * dryGain;
        outR[i] = r * dryGain;
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
