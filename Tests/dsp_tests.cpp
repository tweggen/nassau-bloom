// NassauBloom DSP primitive tests — Gate G1 (docs/GATES.md).
//
// Every primitive in Source/DSP/bloom_dsp.h is driven with real signals and
// MEASURED (GATES R6): gains from steady sines, group delay from the impulse
// response, delay from the phase of a sine, denormals from the samples.

#include "bloom_dsp.h"
#include "test_util.h"

#include <cfloat>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

using namespace bloom;

namespace {

std::string fmt(const char* f, double a, double b = 0.0, double c = 0.0) {
  char buf[256];
  std::snprintf(buf, sizeof buf, f, a, b, c);
  return buf;
}

// Group delay in samples at angular frequency w, measured from the impulse
// response h: τ(w) = Re{ Σ n·h[n]·e^−jwn / Σ h[n]·e^−jwn }. Exact for an IR
// that has decayed to nothing within h.
double groupDelayFromIr(const std::vector<float>& h, double w) {
  std::complex<double> num(0.0, 0.0), den(0.0, 0.0);
  for (size_t n = 0; n < h.size(); ++n) {
    const std::complex<double> e = std::polar(1.0, -w * static_cast<double>(n));
    den += static_cast<double>(h[n]) * e;
    num += static_cast<double>(n) * static_cast<double>(h[n]) * e;
  }
  return (num / den).real();
}

}  // namespace

int main() {
  std::cout << "=== NassauBloom DSP Primitive Tests (G1) ===\n\n";

  // ---- G1.1 Biquad --------------------------------------------------------
  std::cout << "Group: Biquad LP/HP (G1.1)\n";
  for (double fs : {44100.0, 48000.0, 96000.0}) {
    for (double fc : {100.0, 1000.0, 10000.0}) {
      Biquad lp, hp;
      lp.setLowpass(fc, fs);
      hp.setHighpass(fc, fs);
      const double lpAtFc = magDb([&](float x) { return lp.process(x); }, fc, fs);
      lp.reset();
      const double hpAtFc = magDb([&](float x) { return hp.process(x); }, fc, fs);
      hp.reset();
      check(fmt("LP fc %.0f fs %.0f: %.3f dB at fc (want -3.01 +- 0.1)", fc, fs, lpAtFc).c_str(),
            std::fabs(lpAtFc + 3.0103) <= 0.1);
      check(fmt("HP fc %.0f fs %.0f: %.3f dB at fc (want -3.01 +- 0.1)", fc, fs, hpAtFc).c_str(),
            std::fabs(hpAtFc + 3.0103) <= 0.1);
      if (fc <= 0.2 * fs) {
        const double lpStop = magDb([&](float x) { return lp.process(x); }, 2.0 * fc, fs);
        const double hpStop = magDb([&](float x) { return hp.process(x); }, 0.5 * fc, fs);
        lp.reset();
        hp.reset();
        check(fmt("LP fc %.0f fs %.0f: %.2f dB at 2fc (want <= -12.0)", fc, fs, lpStop).c_str(),
              lpStop <= -12.0);
        check(fmt("HP fc %.0f fs %.0f: %.2f dB at fc/2 (want <= -12.0)", fc, fs, hpStop).c_str(),
              hpStop <= -12.0);
      }
    }
  }

  // ---- G1.2 OnePole -------------------------------------------------------
  std::cout << "\nGroup: OnePole LP/HP (G1.2)\n";
  for (double fs : {44100.0, 48000.0, 96000.0}) {
    OnePole lp, hp;
    lp.setLowpass(1000.0, fs);
    hp.setHighpass(1000.0, fs);
    const double lpPass = magDb([&](float x) { return lp.process(x); }, 20.0, fs);
    const double hpPass = magDb([&](float x) { return hp.process(x); }, 0.4 * fs, fs);
    check(fmt("LP fc 1000 fs %.0f: %.4f dB at 20 Hz (want 0 +- 0.01)", fs, lpPass).c_str(),
          std::fabs(lpPass) <= 0.01);
    check(fmt("HP fc 1000 fs %.0f: %.4f dB at 0.4 fs (want 0 +- 0.01)", fs, hpPass).c_str(),
          std::fabs(hpPass) <= 0.01);
    for (double fc : {100.0, 1000.0, 0.1 * fs}) {
      OnePole l, h;
      l.setLowpass(fc, fs);
      h.setHighpass(fc, fs);
      const double la = magDb([&](float x) { return l.process(x); }, fc, fs);
      const double ha = magDb([&](float x) { return h.process(x); }, fc, fs);
      check(fmt("LP fc %.0f fs %.0f: %.3f dB at fc (want -3.0 +- 0.2)", fc, fs, la).c_str(),
            std::fabs(la + 3.0) <= 0.2);
      check(fmt("HP fc %.0f fs %.0f: %.3f dB at fc (want -3.0 +- 0.2)", fc, fs, ha).c_str(),
            std::fabs(ha + 3.0) <= 0.2);
    }
  }

  // ---- G1.3 Allpass -------------------------------------------------------
  std::cout << "\nGroup: Allpass is unity magnitude (G1.3)\n";
  {
    const double fs = 48000.0;
    for (double g : {-0.7, 0.3, 0.5, 0.65, 0.7}) {
      double worst = 0.0;
      for (int i = 0; i < 20; ++i) {
        // 20 log-spaced frequencies from 30 Hz to 20 kHz.
        const double f = 30.0 * std::pow(20000.0 / 30.0, i / 19.0);
        Allpass ap;
        ap.init(64);
        ap.g = g;
        const double db = magDb([&](float x) { return ap.process(x, 37); }, f, fs);
        worst = std::max(worst, std::fabs(db));
      }
      check(fmt("g %+.2f, M 37: worst |magnitude| %.4f dB over 20 freqs (want <= 0.01)", g, worst).c_str(),
            worst <= 0.01);
    }
  }

  // ---- G1.4 StretchedAllpass ----------------------------------------------
  std::cout << "\nGroup: StretchedAllpass group delay (G1.4)\n";
  {
    const double fs = 48000.0;
    const int k = 5;
    for (double a : {0.3, 0.6, 0.75}) {
      StretchedAllpass sa;
      sa.init(k);
      sa.set(k, a);
      std::vector<float> h(16384, 0.0f);
      for (size_t n = 0; n < h.size(); ++n) h[n] = sa.process(n == 0 ? 1.0f : 0.0f);

      const double w50 = 2.0 * kPi * 50.0 / fs;
      const double wTop = kPi / k;  // fs / (2K)
      const double w1k = 2.0 * kPi * 1000.0 / fs;
      const double lo = groupDelayFromIr(h, w50), hi = groupDelayFromIr(h, wTop);
      const double m1k = groupDelayFromIr(h, w1k);
      const double wantLo = k * (1.0 - a) / (1.0 + a), wantHi = k * (1.0 + a) / (1.0 - a);
      const double closed1k = stretchedAllpassGroupDelay(k, a, w1k);
      check(fmt("a %.2f: %.4f samples at 50 Hz vs K(1-a)/(1+a) = %.4f (+-3%%)", a, lo, wantLo).c_str(),
            std::fabs(lo / wantLo - 1.0) <= 0.03);
      check(fmt("a %.2f: %.4f samples at fs/2K vs K(1+a)/(1-a) = %.4f (+-3%%)", a, hi, wantHi).c_str(),
            std::fabs(hi / wantHi - 1.0) <= 0.03);
      check(fmt("a %.2f: closed form %.4f vs measured %.4f at 1 kHz (+-3%%)", a, closed1k, m1k).c_str(),
            std::fabs(closed1k / m1k - 1.0) <= 0.03);
    }
  }

  // ---- G1.5 DelayLine -----------------------------------------------------
  std::cout << "\nGroup: DelayLine reads (G1.5)\n";
  {
    const double fs = 48000.0, f = 1000.0;  // 48 samples per period exactly
    const int n = 48 * 1000 + 4096;
    std::vector<float> in(static_cast<size_t>(n)), outFrac(in.size()), outInt(in.size());
    for (int i = 0; i < n; ++i)
      in[static_cast<size_t>(i)] = static_cast<float>(0.5 * std::sin(2.0 * kPi * f * i / fs));
    DelayLine dl;
    dl.init(64);
    for (int i = 0; i < n; ++i) {
      dl.write(in[static_cast<size_t>(i)]);
      // After the write, tap(1) is in[i]: a delay of D samples is tap(D + 1).
      outFrac[static_cast<size_t>(i)] = dl.tapLinear(10.5 + 1.0);
      outInt[static_cast<size_t>(i)] = dl.tap(17 + 1);
    }
    const int from = 4096, to = 4096 + 48 * 1000;
    const Tone ti = toneOf(in, from, to, f, fs), tf = toneOf(outFrac, from, to, f, fs);
    double dphi = ti.phase - tf.phase;
    while (dphi < 0) dphi += 2.0 * kPi;
    const double delay = dphi / (2.0 * kPi * f / fs);
    check(fmt("linear read at 10.5: measured delay %.5f samples (want 10.5 +- 0.01)", delay).c_str(),
          std::fabs(delay - 10.5) <= 0.01);
    bool exact = true;
    for (int i = 17; i < n; ++i) exact &= outInt[static_cast<size_t>(i)] == in[static_cast<size_t>(i - 17)];
    check("integer read of 17: bit-exact to the input delayed by 17", exact);

    bool exactLin = true;
    DelayLine d2;
    d2.init(64);
    for (int i = 0; i < n; ++i) {
      d2.write(in[static_cast<size_t>(i)]);
      if (i >= 9) exactLin &= d2.tapLinear(9.0 + 1.0) == in[static_cast<size_t>(i - 9)];
    }
    check("tapLinear at an integer delay is bit-exact (no interpolation arithmetic)", exactLin);
    // init(d) needs d + 1 slots (d of history plus the next write), rounded
    // up to a power of two: 64 -> 128, 1000 -> 1024.
    check("init rounds up to a power of two holding >= maxDelay samples of history",
          d2.buf.size() == 128 && d2.capacity() >= 64 &&
          [] { DelayLine x; x.init(1000); return x.buf.size() == 1024 && x.capacity() >= 1000; }());
  }

  // ---- G1.6 RotLfo --------------------------------------------------------
  std::cout << "\nGroup: RotLfo (G1.6)\n";
  {
    const double fs = 48000.0, hz = 0.73;
    const long long n = 10000000LL;
    RotLfo lfo;
    lfo.setFrequency(hz, fs);
    lfo.setPhase(0.0);
    double worstAmp = 0.0;
    for (long long i = 1; i <= n; ++i) {
      lfo.tick();
      if (i % 32 == 0) lfo.renormalize();
      worstAmp = std::max(worstAmp, std::fabs(std::sqrt(lfo.c * lfo.c + lfo.s * lfo.s) - 1.0));
    }
    // Phase after n samples, against the exact 2π·hz·n/fs (mod 2π).
    const double want = std::fmod(2.0 * kPi * hz * static_cast<double>(n) / fs, 2.0 * kPi);
    double err = std::atan2(lfo.s, lfo.c) - want;
    while (err > kPi) err -= 2.0 * kPi;
    while (err < -kPi) err += 2.0 * kPi;
    const double totalPhase = 2.0 * kPi * hz * static_cast<double>(n) / fs;
    const double relFreqErr = std::fabs(err) / totalPhase;
    check(fmt("amplitude stays in 1 +- 1e-6 over 1e7 samples (worst %.3g)", worstAmp).c_str(),
          worstAmp <= 1e-6);
    check(fmt("relative frequency error %.3g (want < 1e-6)", relFreqErr).c_str(), relFreqErr < 1e-6);
  }

  // ---- G1.7 ScopedNoDenormals ---------------------------------------------
  std::cout << "\nGroup: ScopedNoDenormals (G1.7)\n";
  {
    // A feedback one-pole (double state) fed an impulse, then 10 s of
    // silence: its tail passes through the double subnormal range after
    // about a second. `volatile` keeps the compiler from folding the loop.
    auto countSubnormals = []() {
      OnePole lp;
      lp.setLowpass(100.0, 48000.0);
      int subnormals = 0;
      for (int i = 0; i < 48000 * 10; ++i) {
        volatile float out = lp.process(i == 0 ? 1.0f : 0.0f);
        (void)out;
        if (std::fpclassify(lp.y1) == FP_SUBNORMAL) ++subnormals;
      }
      return subnormals;
    };
    const int without = countSubnormals();
    if (!ScopedNoDenormals::kActive) {
      std::cout << "  [SKIP] skipped (no FTZ on this arch)\n";
    } else {
      int with = 0;
      {
        ScopedNoDenormals guard;
        with = countSubnormals();
      }
      // The test must be able to fail: without the guard the same loop DOES
      // reach the subnormal range, or "0 with the guard" proves nothing.
      check(fmt("without the guard the tail goes subnormal (%.0f samples), so the test can fail",
                without).c_str(),
            without > 0);
      check(fmt("with the guard: %.0f subnormal samples in 10 s (want 0)", with).c_str(), with == 0);
      const int after = countSubnormals();
      check("the guard restores the caller's mode on exit", after == without);
    }
  }

  // ---- Smoother (not a G1 AC; DESIGN.md §8's bit-exact cases rest on it) ---
  std::cout << "\nGroup: Smoother snaps onto its target exactly\n";
  {
    Smoother sm;
    sm.setTimeConstant(0.020, 48000.0);
    sm.value = 0.0;
    sm.setTarget(1.0);
    int steps = 0;
    while (!sm.settled() && steps < 48000) {
      sm.step();
      ++steps;
    }
    check(fmt("reaches exactly 1.0 (after %.0f steps of a 20 ms ramp)", steps).c_str(),
          sm.value == 1.0 && steps < 48000);
    const double t63 = [] {
      Smoother s;
      s.setTimeConstant(0.020, 48000.0);
      s.setTarget(1.0);
      for (int i = 0; i < 960; ++i) s.step();  // 20 ms
      return s.value;
    }();
    check(fmt("after one time constant it is at %.4f (want 1 - 1/e = 0.6321 +- 0.001)", t63).c_str(),
          std::fabs(t63 - (1.0 - std::exp(-1.0))) <= 0.001);
  }

  return testSummary();
}
