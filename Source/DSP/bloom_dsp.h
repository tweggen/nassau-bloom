#pragma once

// bloom_dsp.h — the DSP primitives every NassauBloom stage is built from
// (docs/GATES.md G1). Header-only, each struct separately testable
// (Tests/dsp_tests.cpp).
//
// GATES R2: only <algorithm> <cmath> <cstddef> <cstdint> <vector> here, plus
// the arch-intrinsic header ScopedNoDenormals needs. <vector> is for
// init()-time allocation only (GATES R3): nothing below allocates outside a
// method named init().
//
// Conventions shared by every struct:
//   * Coefficient math in double. Filter state in double (Biquad, OnePole);
//     delay-line contents in float (they are large, DESIGN.md §9.5).
//   * setXxx() computes coefficients and may call transcendentals; it is
//     meant to run per sub-block (DESIGN.md §9.2), never per sample (R5).
//     process()/tick() never call one.
//   * reset() clears state and never allocates.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <xmmintrin.h>
#endif

namespace bloom {

constexpr double kPi = 3.14159265358979323846;

// ============================================================================
// Biquad — RBJ "Audio EQ Cookbook" low-pass / high-pass, Direct Form I.
// ============================================================================
struct Biquad {
  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
  double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

  void setLowpass(double fc, double fs, double q = 0.70710678118654752) {
    const double w = 2.0 * kPi * fc / fs;
    const double cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = (1.0 - cw) / 2.0 / a0;
    b1 = (1.0 - cw) / a0;
    b2 = b0;
    a1 = -2.0 * cw / a0;
    a2 = (1.0 - alpha) / a0;
  }

  void setHighpass(double fc, double fs, double q = 0.70710678118654752) {
    const double w = 2.0 * kPi * fc / fs;
    const double cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = (1.0 + cw) / 2.0 / a0;
    b1 = -(1.0 + cw) / a0;
    b2 = b0;
    a1 = -2.0 * cw / a0;
    a2 = (1.0 - alpha) / a0;
  }

  void reset() { x1 = x2 = y1 = y2 = 0.0; }

  float process(float in) {
    const double x = static_cast<double>(in);
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x;
    y2 = y1; y1 = y;
    return static_cast<float>(y);
  }
};

// ============================================================================
// OnePole — first-order low-pass / high-pass by the bilinear transform, with
// the cutoff prewarped: exactly −3.01 dB at fc, exactly unity at DC (LP) or
// Nyquist (HP), and a zero at the other end. Unity passband gain is what lets
// it sit inside a feedback loop (DESIGN.md §5.4, §5.8).
// ============================================================================
struct OnePole {
  double b0 = 1.0, b1 = 0.0, a1 = 0.0;
  double x1 = 0.0, y1 = 0.0;

  void setLowpass(double fc, double fs) {
    const double k = std::tan(kPi * fc / fs);
    b0 = k / (1.0 + k);
    b1 = b0;
    a1 = (k - 1.0) / (k + 1.0);
  }

  void setHighpass(double fc, double fs) {
    const double k = std::tan(kPi * fc / fs);
    b0 = 1.0 / (1.0 + k);
    b1 = -b0;
    a1 = (k - 1.0) / (k + 1.0);
  }

  void reset() { x1 = y1 = 0.0; }

  float process(float in) {
    const double x = static_cast<double>(in);
    const double y = b0 * x + b1 * x1 - a1 * y1;
    x1 = x;
    y1 = y;
    return static_cast<float>(y);
  }
};

// ============================================================================
// DelayLine — a power-of-two circular buffer of float.
//
// tap(d) is the sample written d writes ago: tap(1) is the most recent one.
// So a D-sample delay (D ≥ 1) is `y = tap(D); write(x);`. A zero delay has no
// tap; the caller passes x through (DESIGN.md §3's exact 0 ms case).
// ============================================================================
struct DelayLine {
  std::vector<float> buf;
  uint32_t mask = 0;
  uint32_t pos = 0;  // index the NEXT write goes to

  // Allocates. Holds at least `maxDelay` samples of history: tap(maxDelay)
  // and tapLinear(d) for d ≤ maxDelay − 1 are valid.
  void init(int maxDelay) {
    uint32_t n = 1;
    while (n < static_cast<uint32_t>(std::max(maxDelay, 1)) + 1u) n <<= 1;
    buf.assign(n, 0.0f);
    mask = n - 1;
    pos = 0;
  }

  int capacity() const { return static_cast<int>(mask); }

  void reset() {
    std::fill(buf.begin(), buf.end(), 0.0f);
    pos = 0;
  }

  void write(float x) {
    buf[pos] = x;
    pos = (pos + 1) & mask;
  }

  float tap(int d) const { return buf[(pos - static_cast<uint32_t>(d)) & mask]; }

  // Linear interpolation between tap(i) and tap(i + 1), d = i + f, d ≥ 1.
  // At f == 0 this returns tap(i) exactly (no interpolation arithmetic on
  // the sample), which keeps an integer delay bit-exact.
  float tapLinear(double d) const {
    const double fl = std::floor(d);
    const int i = static_cast<int>(fl);
    const float f = static_cast<float>(d - fl);
    const float a = tap(i);
    if (f == 0.0f) return a;
    return a + f * (tap(i + 1) - a);
  }
};

// ============================================================================
// Allpass — the Schroeder lattice of DESIGN.md §5.1:
//     v[n] = x[n] + g · v[n − M]
//     y[n] = −g · v[n] + v[n − M]
// H(z) = (−g + z^−M) / (1 − g z^−M), |H| = 1 for |g| < 1. The delay may be
// fractional and may change per sample (the modulated allpasses, §5.7);
// line is public so the Tank can read its interior for output taps (§5.6).
// ============================================================================
struct Allpass {
  DelayLine line;
  double g = 0.5;

  void init(int maxDelay) { line.init(maxDelay); }
  void reset() { line.reset(); }

  // Fixed integer delay M ≥ 1.
  float process(float x, int m) {
    const float vd = line.tap(m);
    const float v = x + static_cast<float>(g) * vd;
    line.write(v);
    return vd - static_cast<float>(g) * v;
  }

  // Fractional delay d ≥ 1 (linear interpolation, DESIGN.md §5.7).
  float processFrac(float x, double d) {
    const float vd = line.tapLinear(d);
    const float v = x + static_cast<float>(g) * vd;
    line.write(v);
    return vd - static_cast<float>(g) * v;
  }
};

// ============================================================================
// StretchedAllpass — DESIGN.md §6.1's dispersion section,
//     H(z) = (a + z^−K) / (1 + a z^−K),
// which is the §5.1 Allpass with g = −a and M = K. For a > 0 its group delay
// rises from K(1−a)/(1+a) at DC to K(1+a)/(1−a) at fs/(2K).
// ============================================================================
struct StretchedAllpass {
  Allpass ap;
  int k = 1;

  void init(int maxK) { ap.init(maxK); }
  void reset() { ap.reset(); }
  void set(int stretch, double a) {
    k = std::max(1, stretch);
    ap.g = -a;
  }
  float process(float x) { return ap.process(x, k); }
};

// Closed-form group delay, in samples, of one StretchedAllpass at angular
// frequency w (radians per sample):
//     τ(w) = K (1 − a²) / (1 + 2a cos(K w) + a²)
// DESIGN.md §6.1 uses it for the spring's loop length. Not for per-sample use.
inline double stretchedAllpassGroupDelay(int k, double a, double w) {
  return k * (1.0 - a * a) / (1.0 + 2.0 * a * std::cos(k * w) + a * a);
}

// ============================================================================
// RotLfo — a sine/cosine oscillator by recursive rotation: one 2×2 rotation of
// the (cos, sin) pair per sample, no transcendental. Rounding makes the
// amplitude drift; renormalize() (once per sub-block) pulls it back with the
// first-order correction r ≈ (3 − |p|²)/2, which needs no square root.
// ============================================================================
struct RotLfo {
  double c = 1.0, s = 0.0;    // current (cos φ, sin φ)
  double dc = 1.0, ds = 0.0;  // per-sample rotation (cos Δ, sin Δ)

  void setFrequency(double hz, double fs) {
    const double d = 2.0 * kPi * hz / fs;
    dc = std::cos(d);
    ds = std::sin(d);
  }

  // Phase as a fraction of a cycle, 0 … 1.
  void setPhase(double cycles) {
    c = std::cos(2.0 * kPi * cycles);
    s = std::sin(2.0 * kPi * cycles);
  }

  // Advance one sample; returns the new sin φ.
  double tick() {
    const double nc = c * dc - s * ds;
    const double ns = s * dc + c * ds;
    c = nc;
    s = ns;
    return s;
  }

  void renormalize() {
    const double r = 0.5 * (3.0 - (c * c + s * s));
    c *= r;
    s *= r;
  }
};

// ============================================================================
// Smoother — one-pole ramp towards a target, with a snap: once within 1e−9 of
// the target it IS the target, exactly. That is what lets a smoothed gain land
// on exactly 1.0 or 0.0 and keep DESIGN.md §8's bit-exact cases bit-exact.
// ============================================================================
struct Smoother {
  double value = 0.0;
  double target = 0.0;
  double k = 1.0;  // per-step coefficient

  // Time constant `seconds` at `stepsPerSecond` steps (fs for per-sample
  // smoothing, fs / 32 for per-sub-block, DESIGN.md §9.1).
  void setTimeConstant(double seconds, double stepsPerSecond) {
    k = seconds > 0.0 ? 1.0 - std::exp(-1.0 / (seconds * stepsPerSecond)) : 1.0;
  }

  void setTarget(double t) { target = t; }
  void snap() { value = target; }

  double step() {
    const double d = target - value;
    if (std::fabs(d) < 1e-9) {
      value = target;
    } else {
      value += k * d;
    }
    return value;
  }

  bool settled() const { return value == target; }
};

// ============================================================================
// ScopedNoDenormals — RAII flush-to-zero / denormals-are-zero for the scope
// (DESIGN.md §5.8). Ported from nassau-zermatt's amp_dsp.h, same bit patterns:
// MXCSR FTZ (bit 15) | DAZ (bit 6) on x86/x64, FPCR.FZ (bit 24) on arm64.
// MSVC has no x64 inline asm, so it gets the <xmmintrin.h> intrinsics.
// kActive says whether this build has a real guard or the no-op fallback.
// ============================================================================
struct ScopedNoDenormals {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  static constexpr bool kActive = true;
  unsigned int saved;
  ScopedNoDenormals() {
    saved = _mm_getcsr();
    _mm_setcsr(saved | 0x8040u);
  }
  ~ScopedNoDenormals() { _mm_setcsr(saved); }
#elif defined(__aarch64__)
  static constexpr bool kActive = true;
  unsigned long long saved;
  ScopedNoDenormals() {
    unsigned long long fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    saved = fpcr;
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr | (1ull << 24)));
  }
  ~ScopedNoDenormals() { __asm__ __volatile__("msr fpcr, %0" : : "r"(saved)); }
#elif defined(__x86_64__) || defined(__i386__)
  static constexpr bool kActive = true;
  unsigned int saved;
  ScopedNoDenormals() {
    unsigned int csr;
    __asm__ __volatile__("stmxcsr %0" : "=m"(csr));
    saved = csr;
    unsigned int modified = csr | 0x8040u;
    __asm__ __volatile__("ldmxcsr %0" : : "m"(modified));
  }
  ~ScopedNoDenormals() { __asm__ __volatile__("ldmxcsr %0" : : "m"(saved)); }
#else
  static constexpr bool kActive = false;
  ScopedNoDenormals() {}
#endif
  ScopedNoDenormals(const ScopedNoDenormals&) = delete;
  ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;
};

}  // namespace bloom
