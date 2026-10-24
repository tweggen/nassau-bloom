#pragma once

// test_util.h — shared test harness helpers (docs/GATES.md "Shared measurement
// helpers"). G0 creates it with what G0 needs; G1 adds the measurement
// helpers (magDb, schroederDb, t30Seconds, ...) as the gates need them.
//
// Determinism (GATES R8): no rand()/time(); noise comes from the xorshift32
// below, with fixed seeds.

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
