// NassauBloom early-reflection tests — Gate G3 (docs/GATES.md).
//
// computeErTaps() is tested directly (GATES R6's stated exception: it is a
// pure function), and the ER path through BloomCore is MEASURED: impulse
// energy, stereo, and what a Size change does to a running signal.

#include "bloom_core.h"
#include "bloom_er.h"
#include "test_util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace bloom;

namespace {

constexpr double kFs = 48000.0;

std::string fmt(const char* f, double a, double b = 0.0, double c = 0.0) {
  char buf[256];
  std::snprintf(buf, sizeof buf, f, a, b, c);
  return buf;
}

double tapEnergy(const ErTaps& t) {
  double e = 0.0;
  for (int i = 0; i < t.count; ++i) e += static_cast<double>(t.gain[i]) * t.gain[i];
  return e;
}

// A core whose wet output is the ER path alone: Late Level 0, Mix 100 %,
// Width 100 %, input filters and wall damping bypassed, no pre-delay.
void erOnly(BloomCore& core, float size, int complexity) {
  core.init(static_cast<float>(kFs));
  core.setMix(100.0f);
  core.setLateLevel(0.0f);
  core.setErLevel(100.0f);
  core.setWidth(100.0f);
  core.setInputHpf(20.0f);
  core.setInputLpf(20000.0f);
  core.setDampHigh(20000.0f);
  core.setDampLow(20.0f);
  core.setPreDelayMs(0.0f);
  core.setOutputDb(0.0f);
  core.setSize(size);
  core.setComplexity(complexity);
}

void run(BloomCore& core, const std::vector<float>& inL, const std::vector<float>& inR,
         std::vector<float>& outL, std::vector<float>& outR, int from, int to, int block = 512) {
  for (int pos = from; pos < to; pos += block) {
    const int len = std::min(block, to - pos);
    core.process(&inL[static_cast<size_t>(pos)], &inR[static_cast<size_t>(pos)],
                 &outL[static_cast<size_t>(pos)], &outR[static_cast<size_t>(pos)], len);
  }
}

}  // namespace

int main() {
  std::cout << "=== NassauBloom Early-Reflection Tests (G3) ===\n\n";

  // ---- G3.1 ---------------------------------------------------------------
  std::cout << "Group: tap counts (G3.1)\n";
  {
    ErTaps t2, t3;
    computeErTaps(12.0, 2, kFs, ErMic::Left, t2);
    computeErTaps(12.0, 3, kFs, ErMic::Left, t3);
    check(fmt("order 2: %.0f taps enumerated (want 24)", t2.rawCount).c_str(), t2.rawCount == 24);
    check(fmt("order 3: %.0f taps enumerated (want 62)", t3.rawCount).c_str(), t3.rawCount == 62);
  }

  // ---- G3.2 ---------------------------------------------------------------
  std::cout << "\nGroup: normalised tap energy (G3.2)\n";
  {
    double worst = 0.0;
    for (double size : {2.0, 5.0, 12.0, 25.0, 40.0})
      for (int order : {2, 3})
        for (ErMic mic : {ErMic::Left, ErMic::Right}) {
          ErTaps t;
          computeErTaps(size, order, kFs, mic, t);
          worst = std::max(worst, std::fabs(tapEnergy(t) / 0.25 - 1.0));
        }
    check(fmt("sum gain^2 = 0.25 +- 0.1 %% for Size 2..40, both orders, both mics (worst %.4f %%)",
              100.0 * worst).c_str(),
          worst <= 0.001);
  }

  // ---- G3.3 ---------------------------------------------------------------
  std::cout << "\nGroup: measured impulse energy of the ER path (G3.3)\n";
  for (float size : {3.0f, 12.0f, 40.0f}) {
    for (int cx : {BloomCore::kComplexityPlain, BloomCore::kComplexityDense}) {
      BloomCore core;
      erOnly(core, size, cx);
      const auto imp = testSignal(TestSignal::Impulse, kFs, 0.5);
      std::vector<float> oL(imp.size()), oR(imp.size());
      run(core, imp, imp, oL, oR, 0, static_cast<int>(imp.size()));
      double eL = 0.0, eR = 0.0;
      for (size_t i = 0; i < oL.size(); ++i) {
        eL += static_cast<double>(oL[i]) * oL[i];
        eR += static_cast<double>(oR[i]) * oR[i];
      }
      check(fmt("Size %.0f, order %.0f: energy L %.5f (want 0.25 +- 1 %%)", size, cx == 0 ? 2 : 3, eL).c_str(),
            std::fabs(eL / 0.25 - 1.0) <= 0.01);
      check(fmt("Size %.0f, order %.0f: energy R %.5f (want 0.25 +- 1 %%)", size, cx == 0 ? 2 : 3, eR).c_str(),
            std::fabs(eR / 0.25 - 1.0) <= 0.01);
    }
  }

  // ---- G3.4 ---------------------------------------------------------------
  std::cout << "\nGroup: the first reflection's timing, from independent geometry (G3.4)\n";
  for (double size : {3.0, 12.0, 40.0}) {
    for (ErMic mic : {ErMic::Left, ErMic::Right}) {
      // DESIGN.md §4.1, written out again here rather than taken from erRoom().
      const double W = size, D = 0.72 * size, H = std::min(12.0, std::max(2.4, 0.38 * size));
      const double sx = 0.42 * W, sy = 0.28 * D, sz = 0.40 * H;
      const double s = std::min(1.2, std::max(0.3, 0.06 * size));
      const double mx = 0.5 * W + (mic == ErMic::Left ? -s : s), my = 0.68 * D, mz = 0.45 * H;
      auto dist = [&](double x, double y, double z) {
        return std::sqrt((x - mx) * (x - mx) + (y - my) * (y - my) + (z - mz) * (z - mz));
      };
      const double r0 = dist(sx, sy, sz);
      // The six first-order mirror images, one per wall; the earliest
      // reflection is the nearest of them.
      const double r1 = std::min({dist(-sx, sy, sz), dist(2 * W - sx, sy, sz),
                                  dist(sx, -sy, sz), dist(sx, 2 * D - sy, sz),
                                  dist(sx, sy, -sz), dist(sx, sy, 2 * H - sz)});
      const double want = (r1 - r0) / 343.0 * kFs;
      ErTaps t;
      computeErTaps(size, 2, kFs, mic, t);
      const std::string name = std::string(mic == ErMic::Left ? "L " : "R ") +
          fmt("Size %.0f: first tap %.0f samples vs geometry %.2f (+-1)", size, t.delay[0], want);
      check(name.c_str(), std::fabs(t.delay[0] - want) <= 1.0);
    }
  }

  // ---- G3.5 ---------------------------------------------------------------
  std::cout << "\nGroup: the longest tap fits the buffer (G3.5)\n";
  {
    const double fs = 192000.0;
    ErTaps l, r;
    computeErTaps(40.0, 3, fs, ErMic::Left, l);
    computeErTaps(40.0, 3, fs, ErMic::Right, r);
    const int maxL = l.delay[l.count - 1], maxR = r.delay[r.count - 1];
    const int buf = erMaxDelaySamples(fs);
    check(fmt("192 kHz, Size 40, order 3: longest L tap %.0f (want 63380 +- 2)", maxL).c_str(),
          std::abs(maxL - 63380) <= 2);
    check(fmt("192 kHz, Size 40, order 3: longest R tap %.0f (want 61685 +- 2)", maxR).c_str(),
          std::abs(maxR - 61685) <= 2);
    check(fmt("both shorter than the ER buffer's %.0f samples", buf).c_str(), maxL < buf && maxR < buf);
  }

  // ---- G3.6 ---------------------------------------------------------------
  std::cout << "\nGroup: L and R differ (G3.6)\n";
  {
    ErTaps l, r;
    computeErTaps(12.0, 2, kFs, ErMic::Left, l);
    computeErTaps(12.0, 2, kFs, ErMic::Right, r);
    const int n = std::min(l.count, r.count);
    int differ = 0;
    for (int i = 0; i < n; ++i) differ += std::abs(l.delay[i] - r.delay[i]) >= 1 ? 1 : 0;
    check(fmt("Size 12: %.0f of %.0f rank-paired taps differ in delay (want >= 50 %%)", differ, n).c_str(),
          2 * differ >= n);

    BloomCore core;
    erOnly(core, 12.0f, BloomCore::kComplexityPlain);
    const auto imp = testSignal(TestSignal::Impulse, kFs, 0.2);
    std::vector<float> oL(imp.size()), oR(imp.size());
    run(core, imp, imp, oL, oR, 0, static_cast<int>(imp.size()));
    check("measured: a mono impulse gives an ER output with L != R", oL != oR);
  }

  // ---- G3.7 ---------------------------------------------------------------
  std::cout << "\nGroup: a Size step crossfades without a click (G3.7)\n";
  {
    const auto nL = testSignal(TestSignal::Xorshift, kFs, 1.5, 0xB100Bu);
    const auto nR = testSignal(TestSignal::Xorshift, kFs, 1.5, 0x5EED5u);
    const int n = static_cast<int>(nL.size());
    const int change = 24000;  // 0.5 s in, a sub-block boundary
    BloomCore core, fresh;
    erOnly(core, 5.0f, BloomCore::kComplexityPlain);
    erOnly(fresh, 25.0f, BloomCore::kComplexityPlain);
    std::vector<float> oL(nL.size()), oR(nL.size()), fL(nL.size()), fR(nL.size());
    run(core, nL, nR, oL, oR, 0, change, 32);
    const int before = core.testErRecomputeCount();
    core.setSize(25.0f);
    run(core, nL, nR, oL, oR, change, n, 32);
    run(fresh, nL, nR, fL, fR, 0, n, 32);

    // The click half runs on a SINE, not on noise. Noise already steps by
    // up to ~0.8 per sample, so on noise even a hard switch between the two
    // tap sets (crossfade disabled) stayed under 3x and passed: watched. The
    // ER output of a sine is again a sine, smooth, and a switch shows.
    {
      const auto sine = testSignal(TestSignal::Sine1k, kFs, 1.0);
      BloomCore sc;
      erOnly(sc, 5.0f, BloomCore::kComplexityPlain);
      std::vector<float> sL(sine.size()), sR(sine.size());
      run(sc, sine, sine, sL, sR, 0, change, 32);
      sc.setSize(25.0f);
      run(sc, sine, sine, sL, sR, change, static_cast<int>(sine.size()), 32);
      double stepBefore = 0.0, stepAfter = 0.0;
      for (int i = change - 4800; i < change; ++i)
        stepBefore = std::max(stepBefore, std::fabs(static_cast<double>(sL[i]) - sL[i - 1]));
      for (int i = change; i < change + 9600; ++i)
        stepAfter = std::max(stepAfter, std::fabs(static_cast<double>(sL[i]) - sL[i - 1]));
      check(fmt("Sine1k: largest step after the change %.4f vs 3 x the 100 ms before (%.4f)", stepAfter,
                3.0 * stepBefore).c_str(),
            stepAfter <= 3.0 * stepBefore);
    }
    // The sensitive half: across the 1024 crossfade samples the output IS the
    // linear blend old + f·(new − old), f = k/1024 on the k-th sample, of a
    // core that stayed at Size 5 and one that was always at Size 25. A hard
    // switch (crossfade disabled) passed both step checks above on the
    // samples tried; it fails this on every sample but the last.
    {
      BloomCore stay;
      erOnly(stay, 5.0f, BloomCore::kComplexityPlain);
      std::vector<float> aL(nL.size()), aR(nL.size());
      run(stay, nL, nR, aL, aR, 0, n, 32);
      double worst = 0.0;
      for (int k = 1; k <= 1024; ++k) {
        const int i = change + k - 1;
        const double f = k / 1024.0;
        const double want = aL[i] + f * (static_cast<double>(fL[i]) - aL[i]);
        worst = std::max(worst, std::fabs(oL[i] - want));
      }
      check(fmt("during the crossfade the output is the linear old/new blend (worst error %.2g, want < 1e-5)",
                worst).c_str(),
            worst < 1e-5);
    }
    check(fmt("one recompute for one change (%.0f)", core.testErRecomputeCount() - before).c_str(),
          core.testErRecomputeCount() - before == 1);
    // Done within 2 x 1024 samples: from then on the output IS a Size-25
    // core's, bit-exactly (same input history, same taps).
    bool same = true;
    for (int i = change + 2048; i < n; ++i) same &= oL[i] == fL[i] && oR[i] == fR[i];
    check("after 2 x 1024 samples the output equals a fresh Size-25 core's, bit-exactly", same);
  }

  // ---- G3.8 ---------------------------------------------------------------
  std::cout << "\nGroup: no recompute while Size holds (G3.8)\n";
  {
    BloomCore core;
    erOnly(core, 12.0f, BloomCore::kComplexityPlain);
    const auto x = testSignal(TestSignal::Xorshift, kFs, 2.0);
    std::vector<float> oL(x.size()), oR(x.size());
    run(core, x, x, oL, oR, 0, static_cast<int>(x.size()), 64);
    check(fmt("2 s at a constant Size: %.0f computation (the initial one)", core.testErRecomputeCount()).c_str(),
          core.testErRecomputeCount() == 1);
    // Re-setting the same value, and a move under the 0.5 % threshold, are
    // not changes either.
    core.setSize(12.0f);
    run(core, x, x, oL, oR, 0, 4800, 64);
    core.setSize(12.05f);
    run(core, x, x, oL, oR, 0, 4800, 64);
    check(fmt("same value, then +0.4 %%: still %.0f", core.testErRecomputeCount()).c_str(),
          core.testErRecomputeCount() == 1);
  }

  return testSummary();
}
