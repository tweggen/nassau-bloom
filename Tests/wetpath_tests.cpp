// NassauBloom wet-path tests — Gate G2 (docs/GATES.md).
//
// The input filters, pre-delay, width, mix and output, with the two reverb
// paths still a wire (wet = the filtered, pre-delayed input). Every bit-exact
// claim of DESIGN.md §2, §3, §7 and §8 is checked here, before G3–G5 put a
// reverb in the way.

#include "bloom_core.h"
#include "test_util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr double kFs = 48000.0;

std::string fmt(const char* f, double a, double b = 0.0, double c = 0.0) {
  char buf[256];
  std::snprintf(buf, sizeof buf, f, a, b, c);
  return buf;
}

// A core at 48 kHz in the wet-only, everything-neutral state: Mix 100 %,
// filters bypassed, no pre-delay, Width 100 %, Output 0 dB.
void neutralWet(BloomCore& core) {
  core.init(static_cast<float>(kFs));
  core.setMix(100.0f);
  core.setInputHpf(20.0f);
  core.setInputLpf(20000.0f);
  core.setPreDelayMs(0.0f);
  core.setWidth(100.0f);
  core.setOutputDb(0.0f);
}

void runBlocks(BloomCore& core, const std::vector<float>& inL, const std::vector<float>& inR,
               std::vector<float>& outL, std::vector<float>& outR, int block) {
  outL.assign(inL.size(), 0.0f);
  outR.assign(inR.size(), 0.0f);
  const int n = static_cast<int>(inL.size());
  for (int pos = 0; pos < n; pos += block) {
    const int len = std::min(block, n - pos);
    core.process(&inL[static_cast<size_t>(pos)], &inR[static_cast<size_t>(pos)],
                 &outL[static_cast<size_t>(pos)], &outR[static_cast<size_t>(pos)], len);
  }
}

double maxStep(const std::vector<float>& v, size_t from) {
  double m = 0.0;
  for (size_t i = std::max<size_t>(from, 1); i < v.size(); ++i)
    m = std::max(m, std::fabs(static_cast<double>(v[i]) - static_cast<double>(v[i - 1])));
  return m;
}

}  // namespace

int main() {
  std::cout << "=== NassauBloom Wet-Path Tests (G2) ===\n\n";

  const auto noiseL = testSignal(TestSignal::Xorshift, kFs, 2.0, 0xB100Bu);
  const auto noiseR = testSignal(TestSignal::Xorshift, kFs, 2.0, 0x5EED5u);

  // ---- G2.1 ---------------------------------------------------------------
  std::cout << "Group: the neutral wet path is bit-exact (G2.1)\n";
  for (int block : {1, 32, 33, 512}) {
    BloomCore core;
    neutralWet(core);
    std::vector<float> oL, oR;
    runBlocks(core, noiseL, noiseR, oL, oR, block);
    check(fmt("block %.0f: Mix 100, filters bypassed, Pre 0, Width 100: wet == input", block).c_str(),
          oL == noiseL && oR == noiseR);
  }

  // ---- G2.2 ---------------------------------------------------------------
  std::cout << "\nGroup: pre-delay timing (G2.2)\n";
  {
    const auto imp = testSignal(TestSignal::Impulse, kFs, 0.1);
    BloomCore core;
    neutralWet(core);
    core.setPreDelayMs(37.0f);
    std::vector<float> oL, oR;
    runBlocks(core, imp, imp, oL, oR, 512);
    bool onlyThere = true;
    for (size_t i = 0; i < oL.size(); ++i)
      if (i != 1776) onlyThere &= oL[i] == 0.0f && oR[i] == 0.0f;
    check("37.0 ms: the impulse lands at sample 1776, value exactly 1.0",
          oL[1776] == 1.0f && oR[1776] == 1.0f);
    check("37.0 ms: every other sample is exactly 0", onlyThere);

    BloomCore c2;
    neutralWet(c2);
    c2.setPreDelayMs(37.01f);
    runBlocks(c2, imp, imp, oL, oR, 512);
    const double a = oL[1776], b = oL[1777];
    double rest = 0.0;
    for (size_t i = 0; i < oL.size(); ++i)
      if (i != 1776 && i != 1777) rest = std::max(rest, std::fabs(static_cast<double>(oL[i])));
    check(fmt("37.01 ms: split between 1776 (%.4f) and 1777 (%.4f), summing to 1", a, b).c_str(),
          a > 0.0 && b > 0.0 && std::fabs(a + b - 1.0) < 1e-6 && rest == 0.0);
    // 37.01 ms at 48 kHz is 1776.48 samples: 0.52 / 0.48 by linear read.
    check(fmt("37.01 ms: the split matches 1776.48 samples (1777 weight %.4f, want 0.48 +- 0.001)", b).c_str(),
          std::fabs(b - 0.48) < 0.001);
  }

  // ---- G2.3 ---------------------------------------------------------------
  std::cout << "\nGroup: equal-power mix (G2.3)\n";
  {
    const int n = static_cast<int>(noiseL.size());
    const auto wet = testSignal(TestSignal::Xorshift, kFs, 2.0, 0xC0FFEEu);
    auto rmsAt = [&](float mix) {
      BloomCore core;
      neutralWet(core);
      core.setMix(mix);
      std::vector<float> oL(noiseL.size()), oR(noiseL.size());
      for (int pos = 0; pos < n; pos += 512) {
        const int len = std::min(512, n - pos);
        core.testInjectWet(&wet[static_cast<size_t>(pos)], &wet[static_cast<size_t>(pos)]);
        core.process(&noiseL[static_cast<size_t>(pos)], &noiseL[static_cast<size_t>(pos)],
                     &oL[static_cast<size_t>(pos)], &oR[static_cast<size_t>(pos)], len);
      }
      return 20.0 * std::log10(rms(oL, 0, n));
    };
    const double r0 = rmsAt(0.0f), r50 = rmsAt(50.0f), r100 = rmsAt(100.0f);
    check(fmt("Mix 50 %%: %.3f dB vs Mix 0 %.3f dB (want +-0.1)", r50, r0).c_str(),
          std::fabs(r50 - r0) <= 0.1);
    check(fmt("Mix 50 %%: %.3f dB vs Mix 100 %.3f dB (want +-0.1)", r50, r100).c_str(),
          std::fabs(r50 - r100) <= 0.1);
  }

  // ---- G2.4 ---------------------------------------------------------------
  std::cout << "\nGroup: width (G2.4)\n";
  {
    BloomCore core;
    neutralWet(core);
    core.setWidth(0.0f);
    std::vector<float> oL, oR;
    runBlocks(core, noiseL, noiseR, oL, oR, 512);
    check("Width 0 %: wet L == wet R bit-exactly", oL == oR);

    const int n = static_cast<int>(noiseL.size());
    std::vector<float> side(noiseL), negSide(noiseL.size());
    for (size_t i = 0; i < side.size(); ++i) negSide[i] = -side[i];
    auto injected = [&](float widthPct, const std::vector<float>& wl, const std::vector<float>& wr,
                        std::vector<float>& ol, std::vector<float>& orr) {
      BloomCore c;
      neutralWet(c);
      c.setWidth(widthPct);
      ol.assign(wl.size(), 0.0f);
      orr.assign(wl.size(), 0.0f);
      for (int pos = 0; pos < n; pos += 512) {
        const int len = std::min(512, n - pos);
        c.testInjectWet(&wl[static_cast<size_t>(pos)], &wr[static_cast<size_t>(pos)]);
        c.process(&noiseL[static_cast<size_t>(pos)], &noiseR[static_cast<size_t>(pos)],
                  &ol[static_cast<size_t>(pos)], &orr[static_cast<size_t>(pos)], len);
      }
    };
    injected(200.0f, side, negSide, oL, oR);
    const double gain = 20.0 * std::log10(rms(oL, 0, n) / rms(side, 0, n));
    check(fmt("Width 200 %% on a pure-side wet: %+.4f dB (want +6.02 +- 0.01)", gain).c_str(),
          std::fabs(gain - 6.0206) <= 0.01);

    bool midUnchanged = true;
    for (float wPct : {0.0f, 50.0f, 100.0f, 150.0f, 200.0f}) {
      injected(wPct, side, side, oL, oR);
      midUnchanged &= oL == side && oR == side;
    }
    check("pure-mid wet: unchanged, bit-exactly, at Width 0/50/100/150/200 %", midUnchanged);
  }

  // ---- G2.5 ---------------------------------------------------------------
  std::cout << "\nGroup: automating the pre-delay does not click (G2.5)\n";
  {
    const auto sine = testSignal(TestSignal::Sine1k, kFs, 1.5);
    BloomCore core;
    neutralWet(core);
    std::vector<float> oL(sine.size()), oR(sine.size());
    const int n = static_cast<int>(sine.size());
    for (int pos = 0; pos < n; pos += 32) {
      // 0 → 250 ms over the first second, in 32-sample automation steps.
      const double t = std::min(1.0, pos / kFs);
      core.setPreDelayMs(static_cast<float>(250.0 * t));
      const int len = std::min(32, n - pos);
      core.process(&sine[static_cast<size_t>(pos)], &sine[static_cast<size_t>(pos)],
                   &oL[static_cast<size_t>(pos)], &oR[static_cast<size_t>(pos)], len);
    }
    const double sineStep = maxStep(sine, 1);
    const double outStep = maxStep(oL, 1);
    check(fmt("largest output step %.4f vs 4 x the sine's %.4f", outStep, 4.0 * sineStep).c_str(),
          outStep <= 4.0 * sineStep);
  }

  // ---- G2.6 ---------------------------------------------------------------
  std::cout << "\nGroup: the HPF crossing its bypass threshold (G2.6)\n";
  {
    BloomCore core;
    neutralWet(core);
    std::vector<float> oL(noiseL.size()), oR(noiseL.size());
    const int n = static_cast<int>(noiseL.size());
    for (int pos = 0; pos < n; pos += 512) {
      if (pos == 512 * 40) core.setInputHpf(25.0f);  // ~0.43 s in
      const int len = std::min(512, n - pos);
      core.process(&noiseL[static_cast<size_t>(pos)], &noiseR[static_cast<size_t>(pos)],
                   &oL[static_cast<size_t>(pos)], &oR[static_cast<size_t>(pos)], len);
    }
    double inPeak = 0.0, outPeak = 0.0;
    bool finite = true;
    for (size_t i = 0; i < oL.size(); ++i) {
      inPeak = std::max({inPeak, std::fabs(static_cast<double>(noiseL[i])), std::fabs(static_cast<double>(noiseR[i]))});
      outPeak = std::max({outPeak, std::fabs(static_cast<double>(oL[i])), std::fabs(static_cast<double>(oR[i]))});
      finite &= std::isfinite(oL[i]) && std::isfinite(oR[i]);
    }
    check("20 -> 25 Hz step: all output finite", finite);
    check(fmt("20 -> 25 Hz step: output peak %.3f within input peak %.3f + 1.0", outPeak, inPeak).c_str(),
          outPeak <= inPeak + 1.0);
    // And the filter really switched in: the wet is no longer the input.
    bool changed = false;
    for (size_t i = 512 * 41; i < oL.size(); ++i) changed |= oL[i] != noiseL[i];
    check("the HPF is active after the step (output differs from the input)", changed);
  }

  // ---- Start-up: no ramp after init (DESIGN.md §9.6, bloom_core.h) ---------
  std::cout << "\nGroup: parameters set after init() apply from the first sample\n";
  {
    BloomCore core;
    core.init(static_cast<float>(kFs));  // defaults: Mix 25 %
    core.setMix(0.0f);
    core.setOutputDb(0.0f);
    std::vector<float> oL, oR;
    runBlocks(core, noiseL, noiseR, oL, oR, 64);
    check("Mix 0 set after init: bit-exact dry from sample 0 (no 25 % -> 0 % ramp)",
          oL == noiseL && oR == noiseR);
  }

  return testSummary();
}
