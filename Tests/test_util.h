#pragma once

// test_util.h — shared test harness helpers (docs/GATES.md "Shared measurement
// helpers"). G0 creates it with what G0 needs; G1 adds the measurement
// helpers (magDb, schroederDb, t30Seconds, ...) as the gates need them.
//
// Determinism (GATES R8): no rand()/time(); noise comes from the xorshift32
// below, with fixed seeds.

#include <algorithm>
#include <cmath>
#include <complex>
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
// The fixed set of GATES.md; each arrives with the first gate that uses it.
//   Xorshift  uniform noise, ±0.5
//   Impulse   1.0 at sample 0, then silence
//   Sine1k    1 kHz, amplitude 0.5
//   PluckBurst  nassau-zermatt's: a 220 Hz naive sawtooth under a 20 ms
//             exponential attack and a 50 ms decay, xorshift-dithered at
//             -90 dBFS (the dither runs for the whole signal)
enum class TestSignal { Xorshift, Impulse, Sine1k, PluckBurst };

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
    case TestSignal::Impulse:
      if (!v.empty()) v[0] = 1.0f;
      break;
    case TestSignal::PluckBurst: {
      Xorshift32 rng(0xB0B0B0B0u);
      const double dither = std::pow(10.0, -90.0 / 20.0);
      double phase = 0.0;
      for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        const double saw = 2.0 * phase - 1.0;
        phase += 220.0 / fs;
        if (phase >= 1.0) phase -= 1.0;
        const double env = t < 0.020 ? 1.0 - std::exp(-5.0 * t / 0.020) : std::exp(-(t - 0.020) / 0.050);
        v[i] = static_cast<float>(env * saw + dither * static_cast<double>(rng.nextBipolar()));
      }
      break;
    }
    case TestSignal::Sine1k:
      for (size_t i = 0; i < n; ++i)
        v[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 *
                                                 static_cast<double>(i) / fs));
      break;
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

// ---- Decay and density measurement (GATES "Shared measurement helpers") -----
// The methods the DESIGN.md simulations used, so the gates' numbers mean the
// same thing here as there.

// Energy, Σ x².
inline double energy(const std::vector<float>& v) {
  double e = 0.0;
  for (float x : v) e += static_cast<double>(x) * static_cast<double>(x);
  return e;
}

// RBJ biquad, Q 0.7071, run over a whole signal in double. Test-side copy
// (Tests must not reach into the DSP core to measure it).
inline std::vector<double> rbjFilter(const std::vector<double>& x, bool highpass, double fc, double fs) {
  const double w = 2.0 * 3.14159265358979323846 * fc / fs, cw = std::cos(w);
  const double alpha = std::sin(w) / (2.0 * 0.70710678118654752);
  const double a0 = 1.0 + alpha;
  const double b0 = (highpass ? (1.0 + cw) : (1.0 - cw)) / 2.0 / a0;
  const double b1 = (highpass ? -(1.0 + cw) : (1.0 - cw)) / a0;
  const double a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;
  std::vector<double> y(x.size());
  double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  for (size_t i = 0; i < x.size(); ++i) {
    const double v = b0 * x[i] + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x[i]; y2 = y1; y1 = v;
    y[i] = v;
  }
  return y;
}

// The IIR octave band around fc: two RBJ high-passes at fc/√2 and two RBJ
// low-passes at fc·√2, causal. FOR DECAY TIMES ONLY (GATES: band energies
// use FFT bins, an IIR band's skirts leak a strong neighbour into it).
inline std::vector<double> octaveBand(const std::vector<float>& ir, double fc, double fs) {
  std::vector<double> y(ir.begin(), ir.end());
  for (int k = 0; k < 2; ++k) {
    y = rbjFilter(y, true, fc / std::sqrt(2.0), fs);
    y = rbjFilter(y, false, fc * std::sqrt(2.0), fs);
  }
  return y;
}

// T30, extrapolated to 60 dB, from the Schroeder backward-integrated energy
// decay curve: a least-squares line through the part of the EDC between −5
// and −35 dB. Returns NaN if fewer than 10 samples fall in that range.
inline double t30Seconds(const std::vector<double>& ir, double fs) {
  const size_t n = ir.size();
  std::vector<double> edc(n);
  double acc = 0.0;
  for (size_t i = n; i-- > 0;) {
    acc += ir[i] * ir[i];
    edc[i] = acc;
  }
  const double e0 = edc[0];
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  long long m = 0;
  for (size_t i = 0; i < n; ++i) {
    const double db = 10.0 * std::log10(edc[i] / e0 + 1e-300);
    if (db <= -5.0 && db >= -35.0) {
      const double t = static_cast<double>(i) / fs;
      sx += t; sy += db; sxx += t * t; sxy += t * db;
      ++m;
    }
  }
  if (m < 10) return std::nan("");
  const double slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
  return -60.0 / slope;
}

inline double t30Band(const std::vector<float>& ir, double fc, double fs) {
  return t30Seconds(octaveBand(ir, fc, fs), fs);
}

// Pearson correlation of a and b over [from, to).
inline double correlation(const std::vector<float>& a, const std::vector<float>& b, int from, int to) {
  double ma = 0, mb = 0;
  for (int i = from; i < to; ++i) { ma += a[static_cast<size_t>(i)]; mb += b[static_cast<size_t>(i)]; }
  const double n = static_cast<double>(to - from);
  ma /= n; mb /= n;
  double sab = 0, saa = 0, sbb = 0;
  for (int i = from; i < to; ++i) {
    const double x = a[static_cast<size_t>(i)] - ma, y = b[static_cast<size_t>(i)] - mb;
    sab += x * y; saa += x * x; sbb += y * y;
  }
  return sab / std::sqrt(saa * sbb);
}

// Abel–Huang normalised echo density, averaged over [t0, t1) seconds: in a
// 20 ms window stepped by a quarter window, the fraction of samples more than
// one standard deviation from zero, divided by erfc(1/√2) = 0.3173 (what
// Gaussian noise gives, so dense diffuse reverb reads ~1).
inline double echoDensity(const std::vector<float>& ir, double fs, double t0, double t1) {
  const int w = static_cast<int>(0.020 * fs);
  double sum = 0.0;
  int count = 0;
  for (int c = static_cast<int>(t0 * fs); c < static_cast<int>(t1 * fs); c += w / 4) {
    const int from = c - w / 2, to = c + w / 2;
    if (from < 0 || to > static_cast<int>(ir.size())) continue;
    double mean = 0.0;
    for (int i = from; i < to; ++i) mean += ir[static_cast<size_t>(i)];
    mean /= w;
    double var = 0.0;
    for (int i = from; i < to; ++i) {
      const double d = ir[static_cast<size_t>(i)] - mean;
      var += d * d;
    }
    const double sd = std::sqrt(var / w);
    int outside = 0;
    for (int i = from; i < to; ++i) outside += std::fabs(ir[static_cast<size_t>(i)]) > sd ? 1 : 0;
    sum += static_cast<double>(outside) / w / 0.3173;
    ++count;
  }
  return count ? sum / count : 0.0;
}

// ---- Spectra (GATES "Two band methods") --------------------------------------

// In-place iterative radix-2 FFT; the size must be a power of two.
inline void fftInPlace(std::vector<std::complex<double>>& a) {
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2.0 * 3.14159265358979323846 / static_cast<double>(len);
    const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0, 0.0);
      for (size_t k = 0; k < len / 2; ++k) {
        const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

// Band energy with brick-wall edges: Σ|X[k]|² over the FFT bins whose
// frequency lies in [fc/√2, fc·√2), the signal zero-padded to a power of two.
// FOR ENERGIES AND ENERGY RATIOS (an IIR band's skirts leak, see above).
inline double octaveEnergyFft(const std::vector<float>& x, double fc, double fs) {
  size_t n = 1;
  while (n < x.size()) n <<= 1;
  std::vector<std::complex<double>> a(n);
  for (size_t i = 0; i < x.size(); ++i) a[i] = x[i];
  fftInPlace(a);
  const double lo = fc / std::sqrt(2.0), hi = fc * std::sqrt(2.0);
  double e = 0.0;
  for (size_t k = 0; k <= n / 2; ++k) {
    const double f = static_cast<double>(k) * fs / static_cast<double>(n);
    if (f >= lo && f < hi) e += std::norm(a[k]);
  }
  return e;
}

// Group delay, in samples, of an impulse response h at `hz`:
// τ = Re( Σ n·h[n]·e^{−jωn} / Σ h[n]·e^{−jωn} ), the derivative of the phase
// without unwrapping it. Exact for a response that has died out within h.
inline double groupDelaySamples(const std::vector<float>& h, double hz, double fs) {
  const double w = 2.0 * 3.14159265358979323846 * hz / fs;
  std::complex<double> num(0.0, 0.0), den(0.0, 0.0);
  for (size_t i = 0; i < h.size(); ++i) {
    const std::complex<double> e = std::polar(static_cast<double>(h[i]), -w * static_cast<double>(i));
    den += e;
    num += static_cast<double>(i) * e;
  }
  return (num / den).real();
}
