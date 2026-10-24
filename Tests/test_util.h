#pragma once

// test_util.h — shared test harness helpers (docs/GATES.md "Shared measurement
// helpers"). G0 creates it with what G0 needs; G1 adds the measurement
// helpers (magDb, schroederDb, t30Seconds, ...) as the gates need them.
//
// Determinism (GATES R8): no rand()/time(); noise comes from the xorshift32
// below, with fixed seeds.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

// ---- Soft-check harness (the nassau-eq house pattern) ----------------------
// Run everything, tally, print, and let main() return nonzero on any failure.
inline int& testChecks() { static int n = 0; return n; }
inline int& testFailures() { static int n = 0; return n; }

inline void check(const char* name, bool cond) {
  ++testChecks();
  std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
  if (!cond) ++testFailures();
}

inline int testSummary() {
  std::cout << "\n" << (testChecks() - testFailures()) << "/" << testChecks()
            << " checks passed\n";
  return testFailures() == 0 ? 0 : 1;
}

// ---- Deterministic PRNG (xorshift32) ---------------------------------------
struct Xorshift32 {
  uint32_t s;
  explicit Xorshift32(uint32_t seed) : s(seed ? seed : 0x1234567u) {}
  uint32_t next() {
    uint32_t x = s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s = x;
    return x;
  }
  // Uniform in [-1, 1).
  float nextBipolar() {
    return (static_cast<float>(next() >> 8) / 8388608.0f) - 1.0f;  // 24-bit mantissa
  }
};

// ---- Test signals ------------------------------------------------------------
// The fixed set of GATES.md. G0 needs only Xorshift; the rest arrive with the
// gates that use them.
enum class TestSignal { Xorshift };

inline std::vector<float> testSignal(TestSignal kind, double fs, double seconds,
                                     uint32_t seed = 0xB100Bu) {
  const size_t n = static_cast<size_t>(fs * seconds);
  std::vector<float> v(n, 0.0f);
  switch (kind) {
    case TestSignal::Xorshift: {
      Xorshift32 rng(seed);
      for (auto& s : v) s = 0.5f * rng.nextBipolar();
      break;
    }
  }
  return v;
}

// ---- Tone measurement ----------------------------------------------------------
// Amplitude and phase of the `freq` component of v[from, to), by projection
// onto cos and sin. Leakage from the negative-frequency image is about
// 1/(4π · cycles) relative, so callers use ≥ 200 cycles (toneWindow below).
struct Tone {
  double amp;
  double phase;  // radians
};

inline Tone toneOf(const std::vector<float>& v, int from, int to, double freq, double fs) {
  const double w = 2.0 * 3.14159265358979323846 * freq / fs;
  double re = 0.0, im = 0.0;
  for (int i = from; i < to; ++i) {
    const double s = static_cast<double>(v[static_cast<size_t>(i)]);
    re += s * std::cos(w * i);
    im -= s * std::sin(w * i);
  }
  const double n = static_cast<double>(to - from);
  return Tone{2.0 * std::sqrt(re * re + im * im) / n, std::atan2(im, re)};
}

// Measurement length for a steady sine: a WHOLE number of cycles (at least
// 200, and at least 1 s), rounded to the nearest sample. A whole number of
// cycles is what cancels toneOf's leakage term; 200 cycles alone left up to
// 0.009 dB of error at 30 Hz, on a filter whose true gain is exactly 0 dB.
inline int toneWindow(double freq, double fs) {
  const double cycles = std::max(200.0, std::ceil(freq));
  return static_cast<int>(std::lround(cycles * fs / freq));
}

// Steady-state gain in dB of `processSample` (a callable float(float)) at
// `freq`: run a sine through it, discard a warm-up, compare amplitudes.
template <class ProcessSampleFn>
inline double magDb(ProcessSampleFn&& processSample, double freq, double fs,
                    double amp = 0.25, int warm = 16384) {
  const int meas = toneWindow(freq, fs);
  const int n = warm + meas;
  std::vector<float> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    in[static_cast<size_t>(i)] =
        static_cast<float>(amp * std::sin(2.0 * 3.14159265358979323846 * freq * i / fs));
    out[static_cast<size_t>(i)] = processSample(in[static_cast<size_t>(i)]);
  }
  const Tone ti = toneOf(in, warm, n, freq, fs);
  const Tone to = toneOf(out, warm, n, freq, fs);
  return 20.0 * std::log10(to.amp / ti.amp);
}

// ---- rms ---------------------------------------------------------------------
// RMS of v[from, to).
inline double rms(const std::vector<float>& v, int from, int to) {
  double acc = 0.0;
  for (int i = from; i < to; ++i) {
    const double s = static_cast<double>(v[static_cast<size_t>(i)]);
    acc += s * s;
  }
  const int n = to - from;
  return n > 0 ? std::sqrt(acc / static_cast<double>(n)) : 0.0;
}
