// NassauBloom Spring tests — Gate G5 (docs/GATES.md).
//
// The Spring measured through BloomCore's public API (GATES R6): decay times
// as T30 at 1 kHz, the band limit as FFT-bin octave energies, the
// calibration, stability and decorrelation. G5.1 measures the dispersion
// cascade on its own, as dsp_tests measures the other DSP primitives: the AC
// is about the cascade, and through the closed loop its group delay cannot be
// separated from the loop's.

#include "bloom_core.h"
#include "test_util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr double kFs = 48000.0;

std::string fmt(const char* f, double a, double b = 0.0, double c = 0.0, double d = 0.0) {
  char buf[320];
  std::snprintf(buf, sizeof buf, f, a, b, c, d);
  return buf;
}

// DESIGN.md §6.3's reference: Size 10, RT60 2 s, Diffusion 65 %, damping
// 4500 / 20 Hz.
struct Late {
  float size = 10.0f;
  float decay = 2.0f;
  float diffusion = 65.0f;
  float modulation = 0.0f;
  int complexity = BloomCore::kComplexityPlain;
  float dampHigh = 4500.0f;
  float dampLow = 20.0f;  // bypassed
};

// A core whose wet output is the Spring alone: Algorithm Spring, ER Level 0,
// Mix 100 %, Width 100 %, input filters bypassed, no pre-delay.
void springOnly(BloomCore& core, const Late& p) {
  core.init(static_cast<float>(kFs));
  core.setAlgorithm(BloomCore::kAlgorithmSpring);
  core.setMix(100.0f);
  core.setErLevel(0.0f);
  core.setLateLevel(100.0f);
  core.setWidth(100.0f);
  core.setInputHpf(20.0f);
  core.setInputLpf(20000.0f);
  core.setPreDelayMs(0.0f);
  core.setOutputDb(0.0f);
  core.setSize(p.size);
  core.setDecay(p.decay);
  core.setDiffusion(p.diffusion);
  core.setModulation(p.modulation);
  core.setComplexity(p.complexity);
  core.setDampHigh(p.dampHigh);
  core.setDampLow(p.dampLow);
}

void run(BloomCore& core, const std::vector<float>& inL, const std::vector<float>& inR,
         std::vector<float>& outL, std::vector<float>& outR, int block = 512) {
  outL.assign(inL.size(), 0.0f);
  outR.assign(inR.size(), 0.0f);
  const int n = static_cast<int>(inL.size());
  for (int pos = 0; pos < n; pos += block) {
    const int len = std::min(block, n - pos);
    core.process(&inL[static_cast<size_t>(pos)], &inR[static_cast<size_t>(pos)],
                 &outL[static_cast<size_t>(pos)], &outR[static_cast<size_t>(pos)], len);
  }
}

// The Spring's impulse response for a unit impulse on both inputs, long
// enough for its T30 fit (as tank_tests' lateIr).
void springIr(const Late& p, std::vector<float>& l, std::vector<float>& r, double seconds = 0.0) {
  if (seconds <= 0.0) seconds = std::min(12.0, std::max(3.0, 0.9 * p.decay + 2.5));
  BloomCore core;
  springOnly(core, p);
  const auto imp = testSignal(TestSignal::Impulse, kFs, seconds);
  run(core, imp, imp, l, r);
}

double meanEnergy(const std::vector<float>& l, const std::vector<float>& r) {
  return 0.5 * (energy(l) + energy(r));
}

}  // namespace

int main() {
  std::cout << "=== NassauBloom Spring Tests (G5) ===\n\n";

  // ---- G5.1 ---------------------------------------------------------------
  std::cout << "Group: the dispersion cascade (G5.1)\n";
  {
    bloom::SpringDispersion disp;
    disp.init(kFs);
    disp.setChirp(bloom::springChirp(0.65));
    std::vector<float> h(1 << 15);
    for (size_t i = 0; i < h.size(); ++i) h[i] = disp.process(i == 0 ? 1.0f : 0.0f);
    std::cout << "  [INFO] K " << disp.k << ", a " << disp.a << "\n";

    std::vector<double> fr;
    for (int k = 0;; ++k) {
      const double f = 200.0 * std::pow(2.0, k / 6.0);
      if (f > 4500.0) break;
      fr.push_back(f);
    }
    fr.push_back(4500.0);
    bool monotonic = true;
    double worstVsFormula = 0.0, prev = -1e9;
    for (double f : fr) {
      const double tau = groupDelaySamples(h, f, kFs);
      monotonic &= tau > prev;
      prev = tau;
      worstVsFormula = std::max(worstVsFormula, std::fabs(tau - disp.groupDelay(f, kFs)));
    }
    const double g200 = groupDelaySamples(h, 200.0, kFs), g4k = groupDelaySamples(h, 4000.0, kFs),
                 g45 = groupDelaySamples(h, 4500.0, kFs);
    check(fmt("group delay rises monotonically over %.0f points, 200 Hz - 4.5 kHz", static_cast<double>(fr.size())).c_str(),
          monotonic);
    check(fmt("4 kHz - 200 Hz: %.2f ms (want >= 12, computed 14.5)", 1000.0 * (g4k - g200) / kFs).c_str(),
          (g4k - g200) / kFs >= 0.012);
    check(fmt("4.5 kHz - 200 Hz: %.2f ms (want >= 25, computed 26.5)", 1000.0 * (g45 - g200) / kFs).c_str(),
          (g45 - g200) / kFs >= 0.025);
    std::cout << "  [INFO] measured vs closed form: worst " << worstVsFormula << " samples\n";
  }

  // ---- G5.4 first: the calibration ----------------------------------------
  std::cout << "\nGroup: calibration (G5.4)\n";
  {
    Late ref;
    std::vector<float> l, r;
    springIr(ref, l, r, 6.0);
    const double e = meanEnergy(l, r);
    std::cout << "  [INFO] reference Spring IR energy " << e
              << " -> the kSpringGain that makes it 0.25 is (current kSpringGain) x "
              << std::sqrt(0.25 / e) << "\n";
    check(fmt("reference Spring IR energy %.5f (want 0.25 +- 3 %%)", e).c_str(), std::fabs(e / 0.25 - 1.0) <= 0.03);
  }

  // ---- G5.2 ---------------------------------------------------------------
  std::cout << "\nGroup: RT60 as T30 at 1 kHz (G5.2)\n";
  for (float rt : {1.0f, 2.2f, 5.0f}) {
    Late p;
    p.decay = rt;
    p.dampHigh = 20000.0f;  // DESIGN §6.1's simulation: the knob open, fc caps it
    std::vector<float> l, r;
    springIr(p, l, r);
    const double tl = t30Band(l, 1000.0, kFs), tr = t30Band(r, 1000.0, kFs);
    check(fmt("RT %.1f s: T30 L %.3f s (%+.1f %%), R %.3f s (want +-15 %%)", rt, tl, 100.0 * (tl / rt - 1.0), tr).c_str(),
          std::fabs(tl / rt - 1.0) <= 0.15 && std::fabs(tr / rt - 1.0) <= 0.15);
    std::cout << "         (R " << fmt("%+.1f", 100.0 * (tr / rt - 1.0)) << " %)\n";
  }

  // ---- G5.3 ---------------------------------------------------------------
  std::cout << "\nGroup: the transition frequency caps the band (G5.3)\n";
  {
    Late p;
    p.dampHigh = 20000.0f;
    std::vector<float> l, r;
    springIr(p, l, r, 6.0);
    for (int c = 0; c < 2; ++c) {
      const std::vector<float>& x = c ? r : l;
      const double db = 10.0 * std::log10(octaveEnergyFft(x, 8000.0, kFs) / octaveEnergyFft(x, 1000.0, kFs));
      check((std::string(c ? "R" : "L") +
             fmt(": 8 kHz octave %.2f dB re the 1 kHz octave (want <= -15, computed -16.5)", db)).c_str(),
            db <= -15.0);
    }
  }

  // ---- G5.6 ---------------------------------------------------------------
  std::cout << "\nGroup: a mono input gives a decorrelated tail (G5.6)\n";
  {
    BloomCore core;
    Late p;
    springOnly(core, p);
    const auto x = testSignal(TestSignal::Xorshift, kFs, 2.0);
    std::vector<float> l, r;
    run(core, x, x, l, r);
    const double c = correlation(l, r, static_cast<int>(0.2 * kFs), static_cast<int>(1.0 * kFs));
    check(fmt("L/R correlation over 0.2-1.0 s: %+.3f (want in [-0.3, 0.3])", c).c_str(), std::fabs(c) <= 0.3);
  }

  // ---- DESIGN §6.4: what the Spring ignores (not a numbered AC) -----------
  std::cout << "\nGroup: Modulation and Complexity do nothing in Spring mode (DESIGN 6.4)\n";
  {
    const auto x = testSignal(TestSignal::PluckBurst, kFs, 1.0);
    Late a, b;
    b.modulation = 100.0f;
    b.complexity = BloomCore::kComplexityDense;
    BloomCore ca, cb;
    springOnly(ca, a);
    springOnly(cb, b);
    std::vector<float> al, ar, bl, br;
    run(ca, x, x, al, ar);
    run(cb, x, x, bl, br);
    check("Mod 100 % + Dense is bit-identical to Mod 0 + Plain", al == bl && ar == br);
  }

  // ---- Size glides (DESIGN §6.1 + §9.3; not a numbered AC) ----------------
  std::cout << "\nGroup: a Size move glides T and lands on an integer\n";
  {
    BloomCore core;
    Late p;
    springOnly(core, p);
    const auto x = testSignal(TestSignal::Xorshift, kFs, 0.5);
    std::vector<float> l, r;
    run(core, x, x, l, r);
    core.setSize(30.0f);
    std::vector<float> in(static_cast<size_t>(kFs * 6.0), 0.0f), ol(in.size()), orr(in.size());
    double prev = core.testSpringLength(), worstStep = 0.0;
    for (size_t i = 0; i < in.size(); ++i) {
      core.process(&in[i], &in[i], &ol[i], &orr[i], 1);
      const double t = core.testSpringLength();
      worstStep = std::max(worstStep, std::fabs(t - prev));
      prev = t;
    }
    const double want = std::lround((0.025 + 0.030) * kFs);
    check(fmt("Size 10 -> 30: T ends at %.6f samples (want exactly %.0f)", prev, want).c_str(), prev == want);
    check(fmt("largest T step %.5f samples/sample (want <= 0.25)", worstStep).c_str(), worstStep <= 0.25 + 1e-12);
  }

  // ---- G5.5 (last: the long one) ------------------------------------------
  std::cout << "\nGroup: stability at the extremes (G5.5)\n";
  {
    BloomCore core;
    Late p;
    p.decay = 20.0f;
    p.size = 40.0f;
    p.diffusion = 100.0f;
    p.modulation = 100.0f;
    p.complexity = BloomCore::kComplexityDense;
    p.dampHigh = 20000.0f;
    springOnly(core, p);
    const auto noise = testSignal(TestSignal::Xorshift, kFs, 60.0);
    std::vector<float> in(noise);
    in.resize(static_cast<size_t>(120.0 * kFs), 0.0f);
    std::vector<float> l, r;
    run(core, in, in, l, r, 1024);
    bool finite = true;
    for (size_t i = 0; i < l.size(); ++i) finite &= std::isfinite(l[i]) && std::isfinite(r[i]);
    const int s0 = static_cast<int>(60.0 * kFs);
    const double first = rms(l, s0, s0 + static_cast<int>(kFs));
    const double last = rms(l, static_cast<int>(119.0 * kFs), static_cast<int>(120.0 * kFs));
    const double db = last > 0.0 ? 20.0 * std::log10(last / first) : -400.0;
    check("RT 20 s, Size 40, Diff 100 %, Mod 100 %, Dense, Spring: 120 s all finite", finite);
    check(fmt("last second of the silence %.1f dB re its first (want < -60)", db).c_str(), db < -60.0);
  }

  return testSummary();
}
