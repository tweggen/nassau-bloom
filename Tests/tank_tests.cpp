// NassauBloom Tank tests — Gate G4 (docs/GATES.md).
//
// The late network measured through BloomCore's public API (GATES R6): decay
// times as T30 in octave bands, energies, correlation, echo density, time
// invariance, and what a Size glide does. The one probe into internals,
// testTankD1Length(), is G4.10's stated R6 exception.

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

struct Late {
  float size = 12.0f;
  float decay = 2.0f;
  float diffusion = 70.0f;
  float modulation = 0.0f;
  int complexity = BloomCore::kComplexityPlain;
  float dampHigh = 20000.0f;  // bypassed
  float dampLow = 20.0f;      // bypassed
};

// A core whose wet output is the late network alone: ER Level 0, Mix 100 %,
// Width 100 %, input filters bypassed, no pre-delay.
void lateOnly(BloomCore& core, const Late& p) {
  core.init(static_cast<float>(kFs));
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

// The late-only impulse response for a unit impulse on BOTH inputs (a mono
// source), long enough for its T30 fit: the length the DESIGN.md simulations
// used, clamp(0.9·RT60 + 2.5 s, 3 s, 12 s).
void lateIr(const Late& p, std::vector<float>& l, std::vector<float>& r, double seconds = 0.0) {
  if (seconds <= 0.0) seconds = std::min(12.0, std::max(3.0, 0.9 * p.decay + 2.5));
  BloomCore core;
  lateOnly(core, p);
  const auto imp = testSignal(TestSignal::Impulse, kFs, seconds);
  run(core, imp, imp, l, r);
}

double meanEnergy(const std::vector<float>& l, const std::vector<float>& r) {
  return 0.5 * (energy(l) + energy(r));
}

}  // namespace

int main() {
  std::cout << "=== NassauBloom Tank Tests (G4) ===\n\n";

  // ---- G4.3 first: the calibration everything else is relative to ----------
  std::cout << "Group: calibration (G4.3)\n";
  {
    Late ref;  // Size 12, RT60 2 s, damping bypassed, Plain, Diffusion 70 %, Mod 0
    std::vector<float> l, r;
    lateIr(ref, l, r, 6.0);
    const double e = meanEnergy(l, r);
    std::cout << "  [INFO] reference late IR energy " << e
              << " -> the kLateGain that makes it 0.25 is (current kLateGain) x "
              << std::sqrt(0.25 / e) << "\n";
    check(fmt("reference late IR energy %.5f (want 0.25 +- 3 %%)", e).c_str(), std::fabs(e / 0.25 - 1.0) <= 0.03);
  }

  // ---- G4.1 ---------------------------------------------------------------
  std::cout << "\nGroup: RT60 as T30 at 1 kHz (G4.1)\n";
  {
    struct Cell { float rt, size; int cx; };
    const Cell cells[] = {{2.0f, 3.0f, 0}, {8.0f, 3.0f, 0}, {2.0f, 12.0f, 0}, {8.0f, 12.0f, 0},
                          {2.0f, 40.0f, 0}, {8.0f, 40.0f, 0}, {0.5f, 3.0f, 0}, {0.5f, 6.0f, 0},
                          {2.0f, 12.0f, 1}};
    for (const Cell& c : cells) {
      Late p;
      p.decay = c.rt;
      p.size = c.size;
      p.complexity = c.cx;
      std::vector<float> l, r;
      lateIr(p, l, r);
      const double tl = t30Band(l, 1000.0, kFs), tr = t30Band(r, 1000.0, kFs);
      const std::string name = fmt("RT %.1f s, Size %.0f, ", c.rt, c.size) + (c.cx ? "Dense" : "Plain") +
                               fmt(": T30 L %.3f s, R %.3f s (want +-20 %%)", tl, tr);
      check(name.c_str(), std::fabs(tl / c.rt - 1.0) <= 0.20 && std::fabs(tr / c.rt - 1.0) <= 0.20);
      std::cout << "         (L " << fmt("%+.1f", 100.0 * (tl / c.rt - 1.0)) << " %, R "
                << fmt("%+.1f", 100.0 * (tr / c.rt - 1.0)) << " %)\n";
    }
  }

  // ---- G4.2 ---------------------------------------------------------------
  std::cout << "\nGroup: RT60 does not move with Size (G4.2)\n";
  {
    Late a, b;
    a.decay = b.decay = 4.0f;
    a.size = 3.0f;
    b.size = 40.0f;
    std::vector<float> al, ar, bl, br;
    lateIr(a, al, ar);
    lateIr(b, bl, br);
    const double ta = t30Band(al, 1000.0, kFs), tb = t30Band(bl, 1000.0, kFs);
    check(fmt("RT 4 s: T30 at Size 3 %.3f s vs Size 40 %.3f s, differ by %.1f %% (want < 10 %%)", ta, tb,
              100.0 * std::fabs(ta / tb - 1.0)).c_str(),
          std::fabs(ta / tb - 1.0) < 0.10);
  }

  // ---- G4.4 ---------------------------------------------------------------
  std::cout << "\nGroup: energy vs decay (G4.4)\n";
  {
    Late a, b;
    a.decay = 2.0f;
    b.decay = 8.0f;
    std::vector<float> al, ar, bl, br;
    lateIr(a, al, ar, 12.0);
    lateIr(b, bl, br, 12.0);
    const double db = 10.0 * std::log10(meanEnergy(bl, br) / meanEnergy(al, ar));
    check(fmt("Size 12: RT 8 s holds %+.2f dB over RT 2 s (want +2.0 +- 1.0)", db).c_str(),
          std::fabs(db - 2.0) <= 1.0);
  }

  // ---- G4.11 --------------------------------------------------------------
  std::cout << "\nGroup: Size is not a level control (G4.11)\n";
  {
    std::vector<float> l, r;
    Late p;
    lateIr(p, l, r, 5.0);
    const double e12 = meanEnergy(l, r);
    for (float size : {3.0f, 17.0f}) {
      p.size = size;
      lateIr(p, l, r, 5.0);
      const double db = 10.0 * std::log10(meanEnergy(l, r) / e12);
      check(fmt("RT 2 s: Size %.0f holds %+.2f dB re Size 12 (want +-2)", size, db).c_str(), std::fabs(db) <= 2.0);
    }
  }

  // ---- G4.6 ---------------------------------------------------------------
  std::cout << "\nGroup: in-loop damping shortens the band decay (G4.6)\n";
  {
    Late hi, lo;
    hi.decay = lo.decay = 4.0f;
    hi.dampHigh = 3000.0f;
    lo.dampLow = 500.0f;
    std::vector<float> l, r;
    lateIr(hi, l, r, 8.0);
    const double h1 = t30Band(l, 1000.0, kFs), h8 = t30Band(l, 8000.0, kFs);
    check(fmt("Damp High 3 kHz: T30 8 kHz %.3f s / 1 kHz %.3f s = %.2f (want < 0.55)", h8, h1, h8 / h1).c_str(),
          h8 / h1 < 0.55);
    lateIr(lo, l, r, 8.0);
    const double l1 = t30Band(l, 1000.0, kFs), l125 = t30Band(l, 125.0, kFs);
    check(fmt("Damp Low 500 Hz: T30 125 Hz %.3f s / 1 kHz %.3f s = %.2f (want < 0.60)", l125, l1, l125 / l1).c_str(),
          l125 / l1 < 0.60);
  }

  // ---- G4.7 ---------------------------------------------------------------
  std::cout << "\nGroup: a mono input gives a decorrelated tail (G4.7)\n";
  {
    BloomCore core;
    Late p;
    lateOnly(core, p);
    const auto x = testSignal(TestSignal::Xorshift, kFs, 2.0);
    std::vector<float> l, r;
    run(core, x, x, l, r);
    const double c = correlation(l, r, static_cast<int>(0.2 * kFs), static_cast<int>(1.0 * kFs));
    check(fmt("L/R correlation over 0.2-1.0 s: %+.3f (want in [-0.3, 0.3])", c).c_str(), std::fabs(c) <= 0.3);
  }

  // ---- G4.8 ---------------------------------------------------------------
  std::cout << "\nGroup: time invariance at Mod 0 (G4.8)\n";
  {
    const int shift = 12345;
    const auto pluck = testSignal(TestSignal::PluckBurst, kFs, 2.0);
    std::vector<float> shifted(pluck.size() + shift, 0.0f);
    std::copy(pluck.begin(), pluck.end(), shifted.begin() + shift);
    for (float mod : {0.0f, 25.0f}) {
      // The plugin defaults (Size 12, RT60 2 s, Damp High 8 kHz, ER 100 %,
      // Mix 25 %), only the modulation chosen: the whole signal path.
      auto runDefaults = [mod](const std::vector<float>& in, std::vector<float>& l, std::vector<float>& r) {
        BloomCore core;
        core.init(static_cast<float>(kFs));
        core.setModulation(mod);
        run(core, in, in, l, r);
      };
      std::vector<float> l1, r1, l2, r2;
      runDefaults(pluck, l1, r1);
      runDefaults(shifted, l2, r2);
      bool same = true;
      for (size_t i = 0; i < pluck.size(); ++i) same &= l1[i] == l2[i + shift] && r1[i] == r2[i + shift];
      if (mod == 0.0f)
        check("Mod 0: delaying the input by 12345 samples delays the output, bit-exactly", same);
      else
        check("Mod 25 %: it does not (the modulation is running)", !same);
    }
  }

  // ---- G4.9 ---------------------------------------------------------------
  std::cout << "\nGroup: Dense is denser (G4.9)\n";
  {
    Late plain, dense;
    dense.complexity = BloomCore::kComplexityDense;
    std::vector<float> pl, pr, dl, dr;
    lateIr(plain, pl, pr, 0.5);
    lateIr(dense, dl, dr, 0.5);
    const double nPL = echoDensity(pl, kFs, 0.020, 0.100), nPR = echoDensity(pr, kFs, 0.020, 0.100);
    const double nDL = echoDensity(dl, kFs, 0.020, 0.100), nDR = echoDensity(dr, kFs, 0.020, 0.100);
    check(fmt("L: echo density 20-100 ms Plain %.3f, Dense %.3f, x%.2f (want >= 1.3)", nPL, nDL, nDL / nPL).c_str(),
          nDL >= 1.3 * nPL);
    check(fmt("R: echo density 20-100 ms Plain %.3f, Dense %.3f, x%.2f (want >= 1.3)", nPR, nDR, nDR / nPR).c_str(),
          nDR >= 1.3 * nPR);
  }

  // ---- G4.10 --------------------------------------------------------------
  std::cout << "\nGroup: a Size move is a glide (G4.10)\n";
  {
    // A sine, not noise, for the step half: on noise a step bound cannot see
    // a discontinuity (G3.7's lesson).
    const auto sine = testSignal(TestSignal::Sine1k, kFs, 3.0);
    BloomCore core;
    Late p;
    p.size = 6.0f;
    lateOnly(core, p);
    const int n = static_cast<int>(sine.size());
    const int change = static_cast<int>(1.5 * kFs);
    std::vector<float> l(sine.size()), r(sine.size());
    double prevLen = -1.0, worstLenStep = 0.0;
    for (int i = 0; i < n; ++i) {
      if (i >= change && i < change + 2400 && (i - change) % 32 == 0)  // 6 -> 16 m over 50 ms
        core.setSize(6.0f + 10.0f * static_cast<float>(i - change + 32) / 2400.0f);
      core.process(&sine[static_cast<size_t>(i)], &sine[static_cast<size_t>(i)], &l[static_cast<size_t>(i)],
                   &r[static_cast<size_t>(i)], 1);
      const double len = core.testTankD1Length();
      if (prevLen >= 0.0) worstLenStep = std::max(worstLenStep, len - prevLen);
      prevLen = len;
    }
    // A click is broadband: measure what lands above 8 kHz (a 4th-order
    // high-pass, which takes the 1 kHz sine down by ~72 dB) during and after
    // the glide, against the output's own peak before the change. Measured:
    // a correct glide -48.9 dB (the linear-interpolation artefacts of the
    // moving reads); every length jumping straight to its target +7.2 dB.
    // NOT a step bound: a sine's steady amplitude through a reverb depends on
    // how the tank's modes fall around 1 kHz, which Size changes, so "largest
    // step after vs before" read 2.83x for a correct glide.
    std::vector<double> hf(l.begin(), l.end());
    hf = rbjFilter(rbjFilter(hf, true, 8000.0, kFs), true, 8000.0, kFs);
    double peakBefore = 0.0, hfSteady = 0.0, hfGlide = 0.0;
    for (int i = change - static_cast<int>(0.5 * kFs); i < change; ++i) {
      peakBefore = std::max(peakBefore, std::fabs(static_cast<double>(l[i])));
      hfSteady = std::max(hfSteady, std::fabs(hf[static_cast<size_t>(i)]));
    }
    for (int i = change; i < n; ++i) hfGlide = std::max(hfGlide, std::fabs(hf[static_cast<size_t>(i)]));
    const double hfDb = 20.0 * std::log10(hfGlide / peakBefore);
    std::cout << "  [INFO] above 8 kHz: steady " << fmt("%.1f", 20.0 * std::log10(hfSteady / peakBefore))
              << " dB, during/after the glide " << fmt("%.1f", hfDb) << " dB re the output peak\n";
    check(fmt("no click: content above 8 kHz during the glide %.1f dB re the output peak (want < -30)", hfDb).c_str(),
          hfDb < -30.0);
    check(fmt("the 24-30 read never runs backwards: largest length growth %.5f samples/sample (want < 1, and the 0.25 slew limit + 1 %% for rounded endpoints)",
              worstLenStep).c_str(),
          worstLenStep < 1.0 && worstLenStep <= 0.25 * 1.01);
  }

  // ---- G4.5 (last: the long one) ------------------------------------------
  std::cout << "\nGroup: stability at the extremes (G4.5)\n";
  {
    BloomCore core;
    Late p;
    p.decay = 20.0f;
    p.size = 40.0f;
    p.diffusion = 100.0f;
    p.modulation = 100.0f;
    p.complexity = BloomCore::kComplexityDense;
    lateOnly(core, p);
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
    check("RT 20 s, Size 40, Diff 100 %, Mod 100 %, Dense: 120 s all finite", finite);
    check(fmt("last second of the silence %.1f dB re its first (want < -60)", db).c_str(), db < -60.0);
  }

  return testSummary();
}
