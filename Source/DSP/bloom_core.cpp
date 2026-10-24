#include "bloom_core.h"

#include <algorithm>
#include <cmath>

using bloom::kPi;

namespace {

float clampf(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }

constexpr double kHpfBypassHz = 20.0;     // DESIGN.md §2
constexpr double kLpfBypassHz = 20000.0;  // DESIGN.md §2
constexpr double kMaxPreDelayMs = 250.0;  // DESIGN.md §3
constexpr double kDampHighBypassHz = 20000.0;  // DESIGN.md §5.4
constexpr double kDampLowBypassHz = 20.0;      // DESIGN.md §5.4
constexpr int kErCrossfade = 1024;             // DESIGN.md §4.5
constexpr double kErRecomputeRatio = 0.005;    // DESIGN.md §4.5: 0.5 %

// The Tank's "as designed" output gain (DESIGN.md §5.6, GATES G4.3): the
// factor that puts the late-only impulse response at the reference settings
// (Size 12, RT60 2 s, damping bypassed, Plain, Diffusion 70 %, Mod 0, a unit
// impulse on both inputs) at a mean per-channel energy of 0.25. Measured, not
// derived: Tests/tank_tests.cpp prints the raw energy it comes from.
constexpr double kLateGain = 0.261721;
constexpr double kLateRtExponent = -0.25;   // (RT60 / 2)^−¼, §5.6
constexpr double kLateSizeExponent = 0.42;  // S^0.42, §5.6

// The Spring's "as designed" output gain (DESIGN.md §6.3, GATES G5.4), measured
// as kLateGain is, at Size 10, RT60 2 s, Diffusion 65 %, damping 4500 / 20 Hz.
// The (RT60/2)^−¼ compensation is the Tank's.
constexpr double kSpringGain = 0.708526;

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
    for (auto& d : er_) d.init(bloom::erMaxDelaySamples(fs_) + 4);
    tank_.init(fs_);
    spring_.init(fs_);

    const double subBlockRate = fs_ / kSubBlock;
    dryGain_.setTimeConstant(kGainTau, fs_);
    wetGain_.setTimeConstant(kGainTau, fs_);
    outGain_.setTimeConstant(kGainTau, fs_);
    width_s_.setTimeConstant(kGainTau, fs_);
    preDelay_.setTimeConstant(kPreDelayTau, fs_);
    hpfHz_.setTimeConstant(kFilterTau, subBlockRate);
    lpfHz_.setTimeConstant(kFilterTau, subBlockRate);
    erGain_.setTimeConstant(kGainTau, fs_);
    lateGain_.setTimeConstant(kGainTau, fs_);
    dampHighHz_.setTimeConstant(kFilterTau, subBlockRate);
    dampLowHz_.setTimeConstant(kFilterTau, subBlockRate);
    decay_s_.setTimeConstant(kFilterTau, subBlockRate);
    diffusion_s_.setTimeConstant(kFilterTau, subBlockRate);
    modulation_s_.setTimeConstant(kFilterTau, subBlockRate);
    erRecomputes_ = 0;

    reset();
}

void BloomCore::reset()
{
    for (auto& f : hpf_) f.reset();
    for (auto& f : lpf_) f.reset();
    for (auto& d : pre_) d.reset();
    for (auto& d : er_) d.reset();
    for (auto& f : erLp_) f.reset();
    for (auto& f : erHp_) f.reset();
    hpfOn_ = lpfOn_ = false;
    erLpOn_ = erHpOn_ = false;
    erFade_ = 0;
    erOrder_ = 0;  // forces a fresh tap set on the next process()
    tank_.reset();
    spring_.reset();
    activeAlgorithm_ = algorithm_.load(std::memory_order_relaxed);
    // The smoothers are snapped by the next process(), from the targets as
    // they are THEN (DESIGN.md §9.6).
    snapPending_ = true;
}

// Load the parameter targets, step the per-sub-block smoothers and set the
// coefficients. The only place the audio path calls a transcendental (R5).
void BloomCore::startErSet(int set, double size, int order)
{
    bloom::computeErTaps(size, order, fs_, bloom::ErMic::Left, erSet_[set][0]);
    bloom::computeErTaps(size, order, fs_, bloom::ErMic::Right, erSet_[set][1]);
    erSize_ = size;
    erOrder_ = order;
    ++erRecomputes_;
}

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
    erGain_.setTarget(static_cast<double>(erLevel_.load(std::memory_order_relaxed)) / 100.0);
    lateGain_.setTarget(static_cast<double>(lateLevel_.load(std::memory_order_relaxed)) / 100.0);
    dampHighHz_.setTarget(static_cast<double>(dampHigh_.load(std::memory_order_relaxed)));
    dampLowHz_.setTarget(static_cast<double>(dampLow_.load(std::memory_order_relaxed)));
    decay_s_.setTarget(static_cast<double>(decay_.load(std::memory_order_relaxed)));
    diffusion_s_.setTarget(static_cast<double>(diffusion_.load(std::memory_order_relaxed)) / 100.0);
    modulation_s_.setTarget(static_cast<double>(modulation_.load(std::memory_order_relaxed)) / 100.0);
    // The late network that runs. A switch starts the other one from reset;
    // G6 turns this into DESIGN.md §9.4's crossfade.
    {
        const int alg = algorithm_.load(std::memory_order_relaxed);
        if (alg != activeAlgorithm_) {
            if (alg == kAlgorithmSpring) spring_.reset(); else tank_.reset();
            activeAlgorithm_ = alg;
        }
    }
    const double sizeM = static_cast<double>(size_.load(std::memory_order_relaxed));
    if (activeAlgorithm_ == kAlgorithmSpring) spring_.setSizeTarget(sizeM);
    else tank_.setScaleTarget(bloom::tankScale(sizeM));

    const bool snap = snapPending_;
    if (snapPending_) {
        for (bloom::Smoother* s : {&dryGain_, &wetGain_, &outGain_, &width_s_, &preDelay_, &hpfHz_, &lpfHz_,
                                   &erGain_, &lateGain_, &dampHighHz_, &dampLowHz_,
                                   &decay_s_, &diffusion_s_, &modulation_s_})
            s->snap();
        snapPending_ = false;
    } else {
        for (bloom::Smoother* s : {&hpfHz_, &lpfHz_, &dampHighHz_, &dampLowHz_,
                                   &decay_s_, &diffusion_s_, &modulation_s_})
            s->step();
    }

    // Early-reflection taps (DESIGN.md §4.5): recomputed when Size has moved
    // by more than 0.5 % or the order changed, into the inactive set, which
    // then crossfades in. A change during a crossfade waits for it to end.
    {
        const double size = static_cast<double>(size_.load(std::memory_order_relaxed));
        const int order = complexity_.load(std::memory_order_relaxed) == kComplexityDense ? 3 : 2;
        if (snap || erOrder_ == 0) {
            startErSet(erActive_, size, order);
            erFade_ = 0;
        } else if (erFade_ == 0 &&
                   (order != erOrder_ || std::fabs(size - erSize_) > kErRecomputeRatio * erSize_)) {
            startErSet(1 - erActive_, size, order);
            erFade_ = kErCrossfade;
        }
    }

    // Wall damping on the ER sum (DESIGN.md §4.3), bypassed exactly at the
    // ends of the ranges like the input filters.
    const bool erLpOn = dampHighHz_.value < kDampHighBypassHz;
    const bool erHpOn = dampLowHz_.value > kDampLowBypassHz;
    if (erLpOn && !erLpOn_) for (auto& f : erLp_) f.reset();
    if (erHpOn && !erHpOn_) for (auto& f : erHp_) f.reset();
    erLpOn_ = erLpOn;
    erHpOn_ = erHpOn;
    if (erLpOn_) {
        const double fc = std::min(dampHighHz_.value, 0.45 * fs_);
        for (auto& f : erLp_) f.setLowpass(fc, fs_);
    }
    if (erHpOn_) for (auto& f : erHp_) f.setHighpass(dampLowHz_.value, fs_);

    // The Spring (DESIGN.md §6) and its output gain (§6.3); Modulation and
    // Complexity do nothing here (§6.4).
    if (activeAlgorithm_ == kAlgorithmSpring) {
        bloom::Spring::Block b;
        b.rt60 = decay_s_.value;
        b.diffusion = diffusion_s_.value;
        b.dampHighHz = dampHighHz_.value;
        b.dampLowOn = dampLowHz_.value > kDampLowBypassHz;
        b.dampLowHz = dampLowHz_.value;
        spring_.setBlock(b);
        lateOut_ = kSpringGain * std::pow(b.rt60 / 2.0, kLateRtExponent);
    }
    // The Tank (DESIGN.md §5), and its calibrated output gain (§5.6).
    else {
        bloom::Tank::Block b;
        b.rt60 = decay_s_.value;
        b.diffusion = diffusion_s_.value;
        b.excursion = modulation_s_.value * bloom::kTankExcursion * fs_ / bloom::kDattorroRate;
        b.dense = complexity_.load(std::memory_order_relaxed) == kComplexityDense;
        b.dampHighOn = dampHighHz_.value < kDampHighBypassHz;
        b.dampLowOn = dampLowHz_.value > kDampLowBypassHz;
        b.dampHighHz = dampHighHz_.value;
        b.dampLowHz = dampLowHz_.value;
        tank_.setBlock(b);
        lateOut_ = kLateGain * std::pow(b.rt60 / 2.0, kLateRtExponent) *
                   std::pow(tank_.scale(), kLateSizeExponent);
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

            // ---- Early reflections (DESIGN.md §4) ----
            // Each channel's mic hears that channel's input; with a mono
            // source the two tap sets still differ (§4.1), so the ER is stereo.
            er_[0].write(xL);
            er_[1].write(xR);
            float eL = 0.0f, eR = 0.0f;
            {
                const bloom::ErTaps& aL = erSet_[erActive_][0];
                const bloom::ErTaps& aR = erSet_[erActive_][1];
                // tap(d + 1): tap(1) is the sample just written (G2's rule).
                for (int t = 0; t < aL.count; ++t) eL += aL.gain[t] * er_[0].tap(aL.delay[t] + 1);
                for (int t = 0; t < aR.count; ++t) eR += aR.gain[t] * er_[1].tap(aR.delay[t] + 1);
                if (erFade_ > 0) {
                    const bloom::ErTaps& bL = erSet_[1 - erActive_][0];
                    const bloom::ErTaps& bR = erSet_[1 - erActive_][1];
                    float nL = 0.0f, nR = 0.0f;
                    for (int t = 0; t < bL.count; ++t) nL += bL.gain[t] * er_[0].tap(bL.delay[t] + 1);
                    for (int t = 0; t < bR.count; ++t) nR += bR.gain[t] * er_[1].tap(bR.delay[t] + 1);
                    // Linear crossfade, old -> new (DESIGN.md §4.5).
                    const float f = static_cast<float>(kErCrossfade - erFade_ + 1) / kErCrossfade;
                    eL += f * (nL - eL);
                    eR += f * (nR - eR);
                    if (--erFade_ == 0) erActive_ = 1 - erActive_;
                }
            }
            if (erLpOn_) { eL = erLp_[0].process(eL); eR = erLp_[1].process(eR); }
            if (erHpOn_) { eL = erHp_[0].process(eL); eR = erHp_[1].process(eR); }

            // ---- Late network: the Tank (§5) or the Spring (§6) ----
            float lL, lR;
            if (activeAlgorithm_ == kAlgorithmSpring) spring_.processSample(xL, xR, lL, lR);
            else tank_.processSample(xL, xR, lL, lR);
            lL = static_cast<float>(lL * lateOut_);
            lR = static_cast<float>(lR * lateOut_);

            // ---- Path levels and the wet sum (DESIGN.md §1) ----
            // Assigned, not added to zero, at a level of exactly 0 (G2's rule).
            const double gE = erGain_.step();
            const double gL = lateGain_.step();
            float wL, wR;
            if (testWire_)      { wL = xL; wR = xR; }
            else if (gE == 0.0) { wL = static_cast<float>(gL * lL); wR = static_cast<float>(gL * lR); }
            else if (gL == 0.0) { wL = static_cast<float>(gE * eL); wR = static_cast<float>(gE * eR); }
            else {
                wL = static_cast<float>(gE * eL + gL * lL);
                wR = static_cast<float>(gE * eR + gL * lR);
            }
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
