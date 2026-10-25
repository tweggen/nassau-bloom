// NassauBloom switching tests — Gate G6.1–G6.3 (docs/GATES.md).
//
// A switch of Algorithm or Complexity crossfades the late output over 2048
// samples (DESIGN.md §9.4). Measured two ways:
//   * as a BLEND (the convention gate-crossfades-by-blend-not-step-bound): the
//     switched output must equal old + f·(new − old) sample by sample, with
//     old and new taken from reference runs that never crossfade. That pins
//     the crossfade's shape and length, and that the new structure starts
//     where §9.4 says (from reset for Tank/Spring, from the running tank for
//     Plain/Dense).
//   * by the AC's step bound, on a tail: no sample-to-sample step during the
//     switch above 4x the largest step of the steady state before it.
// The test hooks testLateCrossfade() and testLateStructuresRun() are G6's
// stated R6 exceptions.

#include "bloom_core.h"
#include "test_util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr double kFs = 48000.0;
constexpr int kFade = 2048;
constexpr int kS0 = 48000;          // first switch (a sub-block boundary)
constexpr int kS1 = kS0 + 24000;    // second switch, back
constexpr int kLen = kS1 + 24000;

std::string fmt(const char* f, double a, double b = 0.0, double c = 0.0, double d = 0.0) {
  char buf[320];
  std::snprintf(buf, sizeof buf, f, a, b, c, d);
  return buf;
}

// The late network alone, as tank_tests' lateOnly: ER 0, Mix 100 %, Width
// 100 %, input filters bypassed, no pre-delay; Size 12, RT 2 s, Diffusion
// 70 %, Mod 0, damping bypassed.
void lateOnly(BloomCore& core, int algorithm, int complexity) {
  core.init(static_cast<float>(kFs));
  core.setMix(100.0f);
  core.setErLevel(0.0f);
  core.setLateLevel(100.0f);
  core.setWidth(100.0f);
  core.setInputHpf(20.0f);
  core.setInputLpf(20000.0f);
  core.setPreDelayMs(0.0f);
  core.setOutputDb(0.0f);
  core.setSize(12.0f);
  core.setDecay(2.0f);
  core.setDiffusion(70.0f);
  core.setModulation(0.0f);
  core.setDampHigh(20000.0f);
  core.setDampLow(20.0f);
  core.setAlgorithm(algorithm);
  core.setComplexity(complexity);
}

// Runs `in` through `core` in blocks of at most 512 that never straddle
// kS0 or kS1, calling `at(sample)` before the block starting at each of them,
// so every run shares the same sub-block grid. Records how many late
// structures each sample ran.
void runSwitched(BloomCore& core, const std::vector<float>& in, std::vector<float>& l, std::vector<float>& r,
                 const std::function<void(int)>& at, std::vector<int>* structures = nullptr) {
  l.assign(in.size(), 0.0f);
  r.assign(in.size(), 0.0f);
  if (structures) structures->assign(in.size(), 0);
  const int n = static_cast<int>(in.size());
  for (int pos = 0; pos < n;) {
    if (pos == kS0 || pos == kS1) at(pos);
    int len = std::min(512, n - pos);
    for (int cut : {kS0, kS1})
      if (pos < cut && pos + len > cut) len = cut - pos;
    if (structures) {
      for (int i = 0; i < len; ++i) {
        const size_t k = static_cast<size_t>(pos + i);
        core.process(&in[k], &in[k], &l[k], &r[k], 1);
        (*structures)[k] = core.testLateStructuresRun();
      }
    } else {
      const size_t k = static_cast<size_t>(pos);
      core.process(&in[k], &in[k], &l[k], &r[k], len);
    }
    pos += len;
  }
}

std::vector<float> zeroedBefore(std::vector<float> x, int n) {
  std::fill(x.begin(), x.begin() + n, 0.0f);
  return x;
}

// The blend check over one switch at `s`: the output is `oldRef` before s,
// oldRef + f·(newRef − oldRef) with f = (k+1)/2048 for k = 0 … 2047, and
// newRef from s + 2048 until `end`. Returns the largest deviation and the
// measured crossfade length: the samples from s up to and including the
// first one that is wholly newRef for good (the k = 2047 one, f = 1).
struct Blend { double worst; int length; };
Blend blendCheck(const std::vector<float>& y, const std::vector<float>& oldRef, const std::vector<float>& newRef,
                 int s, int end) {
  double worst = 0.0;
  for (int n = s - 4800; n < end; ++n) {
    const size_t i = static_cast<size_t>(n);
    float want;
    if (n < s) want = oldRef[i];
    else if (n < s + kFade) {
      const float f = static_cast<float>(n - s + 1) / static_cast<float>(kFade);
      want = oldRef[i] + f * (newRef[i] - oldRef[i]);
    } else want = newRef[i];
    worst = std::max(worst, static_cast<double>(std::fabs(y[i] - want)));
  }
  int last = s - 1;  // the last sample that is not yet newRef
  for (int n = s; n < end; ++n)
    if (std::fabs(y[static_cast<size_t>(n)] - newRef[static_cast<size_t>(n)]) > 1e-7f) last = n;
  return {worst, last + 2 - s};
}

double maxStep(const std::vector<float>& y, int from, int to) {
  double m = 0.0;
  for (int i = from + 1; i < to; ++i)
    m = std::max(m, static_cast<double>(std::fabs(y[static_cast<size_t>(i)] - y[static_cast<size_t>(i - 1)])));
  return m;
}

}  // namespace

int main() {
  std::cout << "=== NassauBloom Switching Tests (G6.1-G6.3) ===\n\n";
  const auto noise = testSignal(TestSignal::Xorshift, kFs, static_cast<double>(kLen) / kFs);
  const int tk = BloomCore::kAlgorithmTank, sp = BloomCore::kAlgorithmSpring;
  const int plain = BloomCore::kComplexityPlain, dense = BloomCore::kComplexityDense;
  const auto none = [](int) {};

  // ---- G6.1 as a blend ------------------------------------------------------
  std::cout << "Group: Tank -> Spring -> Tank is a 2048-sample blend (G6.1)\n";
  {
    std::vector<float> yl, yr, al, ar, bl, br, cl, cr;
    std::vector<int> structures;
    BloomCore sw;
    lateOnly(sw, tk, plain);
    runSwitched(sw, noise, yl, yr,
                [&](int s) { sw.setAlgorithm(s == kS0 ? sp : tk); }, &structures);
    // References: the Tank throughout; a Spring that hears only from kS0 on
    // (= starts from reset there); a Tank that hears only from kS1 on.
    BloomCore ra, rb, rc;
    lateOnly(ra, tk, plain);
    lateOnly(rb, sp, plain);
    lateOnly(rc, tk, plain);
    runSwitched(ra, noise, al, ar, none);
    runSwitched(rb, zeroedBefore(noise, kS0), bl, br, none);
    runSwitched(rc, zeroedBefore(noise, kS1), cl, cr, none);
    for (int c = 0; c < 2; ++c) {
      const auto& y = c ? yr : yl;
      const Blend b1 = blendCheck(y, c ? ar : al, c ? br : bl, kS0, kS1);
      const Blend b2 = blendCheck(y, c ? br : bl, c ? cr : cl, kS1, kLen);
      const char* ch = c ? "R" : "L";
      check((std::string(ch) + fmt(": Tank -> Spring: off the blend by at most %.2e (want <= 1e-6), fade %.0f samples (want 2048 +- 32)",
                                   b1.worst, b1.length)).c_str(),
            b1.worst <= 1e-6 && std::abs(b1.length - kFade) <= 32);
      check((std::string(ch) + fmt(": Spring -> Tank: off the blend by at most %.2e (want <= 1e-6), fade %.0f samples (want 2048 +- 32)",
                                   b2.worst, b2.length)).c_str(),
            b2.worst <= 1e-6 && std::abs(b2.length - kFade) <= 32);
    }
    // G6.3, deterministically: two structures during each crossfade, one
    // outside them.
    int two = 0, wrong = 0;
    for (int n = 0; n < kLen; ++n) {
      const bool fading = (n >= kS0 && n < kS0 + kFade) || (n >= kS1 && n < kS1 + kFade);
      two += structures[static_cast<size_t>(n)] == 2;
      wrong += structures[static_cast<size_t>(n)] != (fading ? 2 : 1);
    }
    check(fmt("G6.3: %.0f samples ran two structures (want 2 x 2048), %.0f samples ran the wrong number (want 0)",
              two, wrong).c_str(),
          two == 2 * kFade && wrong == 0);
  }

  // ---- G6.2 as a blend ------------------------------------------------------
  std::cout << "\nGroup: Plain -> Dense -> Plain is a 2048-sample blend from the running tank (G6.2)\n";
  {
    std::vector<float> yl, yr, al, ar, bl, br, cl, cr;
    BloomCore sw;
    lateOnly(sw, tk, plain);
    runSwitched(sw, noise, yl, yr, [&](int s) { sw.setComplexity(s == kS0 ? dense : plain); });
    // References: Plain throughout; a HARD switch to Dense at kS0 (the
    // forked tank, alone); hard switches at kS0 and back at kS1.
    BloomCore ra, rb, rc;
    lateOnly(ra, tk, plain);
    lateOnly(rb, tk, plain);
    lateOnly(rc, tk, plain);
    rb.testLateCrossfade(0);
    rc.testLateCrossfade(0);
    runSwitched(ra, noise, al, ar, none);
    runSwitched(rb, noise, bl, br, [&](int s) { if (s == kS0) rb.setComplexity(dense); });
    runSwitched(rc, noise, cl, cr, [&](int s) { rc.setComplexity(s == kS0 ? dense : plain); });
    for (int c = 0; c < 2; ++c) {
      const auto& y = c ? yr : yl;
      const Blend b1 = blendCheck(y, c ? ar : al, c ? br : bl, kS0, kS1);
      const Blend b2 = blendCheck(y, c ? br : bl, c ? cr : cl, kS1, kLen);
      const char* ch = c ? "R" : "L";
      check((std::string(ch) + fmt(": Plain -> Dense: off the blend by at most %.2e (want <= 1e-6), fade %.0f samples (want 2048 +- 32)",
                                   b1.worst, b1.length)).c_str(),
            b1.worst <= 1e-6 && std::abs(b1.length - kFade) <= 32);
      check((std::string(ch) + fmt(": Dense -> Plain: off the blend by at most %.2e (want <= 1e-6), fade %.0f samples (want 2048 +- 32)",
                                   b2.worst, b2.length)).c_str(),
            b2.worst <= 1e-6 && std::abs(b2.length - kFade) <= 32);
    }
    // The fork really continues the tail: right after the switch the Dense
    // tank's output is of the same level as Plain's (a tank from rest would
    // be silent for its first ~100 ms).
    const double before = rms(al, kS0 - 4800, kS0), after = rms(bl, kS0, kS0 + 4800);
    check(fmt("the forked Dense tank's first 100 ms: %.2f dB re Plain's last 100 ms (want within +-3)",
              20.0 * std::log10(after / before)).c_str(),
          std::fabs(20.0 * std::log10(after / before)) <= 3.0);
  }

  // ---- G6.1 / G6.2 by the AC's step bound, on a tail ------------------------
  std::cout << "\nGroup: no step above 4x the steady state during a switch, mid-tail (G6.1, G6.2)\n";
  {
    // Noise that stops 100 ms before each switch, so both switches (1.0 s
    // and 1.5 s) fall in a tail: 0 - 0.9 s, and again 1.1 - 1.4 s so the
    // structure switched to at 1.0 s has a tail of its own at 1.5 s. The
    // steady state is the 100 ms before each switch.
    std::vector<float> in(noise.size(), 0.0f);
    std::copy(noise.begin(), noise.begin() + kS0 - 4800, in.begin());
    std::copy(noise.begin() + kS0 + 4800, noise.begin() + kS1 - 4800, in.begin() + kS0 + 4800);
    struct Case { const char* name; int fromAlg, fromCx, toAlg, toCx; };
    const Case cases[] = {{"Tank -> Spring -> Tank", tk, plain, sp, plain},
                          {"Plain -> Dense -> Plain", tk, plain, tk, dense}};
    for (const Case& k : cases) {
      BloomCore core;
      lateOnly(core, k.fromAlg, k.fromCx);
      std::vector<float> l, r;
      runSwitched(core, in, l, r, [&](int s) {
        core.setAlgorithm(s == kS0 ? k.toAlg : k.fromAlg);
        core.setComplexity(s == kS0 ? k.toCx : k.fromCx);
      });
      for (int s : {kS0, kS1}) {
        const double steady = std::max(maxStep(l, s - 4800, s), maxStep(r, s - 4800, s));
        const double during = std::max(maxStep(l, s - 1, s + 2 * kFade), maxStep(r, s - 1, s + 2 * kFade));
        check((std::string(k.name) + fmt(" at %.1f s: largest step %.3g, %.2f x the steady state's %.3g (want <= 4)",
                                         s / kFs, during, during / steady, steady)).c_str(),
              during <= 4.0 * steady);
      }
    }
  }

  return testSummary();
}
