#pragma once

// bloom_spring.h — the late network's Spring (docs/DESIGN.md §6, GATES G5): two
// dispersive spring loops after Välimäki, Parker & Abel, "Parametric Spring
// Reverberation Effect" (JAES 58(7/8), 2010), the "low-chirp" path only.
//
// One spring (§6.1):
//
//   x ─► (+) ─► Dispersion (80 stretched allpasses) ─► Delay T ─► LPF pair ─┬─► out
//         ▲                                                                 │
//         └──────────────────────── × gS ◄── HPF ◄─────────────────────────┘
//
// Header-only, allocation in init() only (GATES R3), no transcendental per
// sample (R5): setBlock() does the pow()s and the filter coefficients once
// per sub-block, and processSample() only multiplies, adds and reads delay
// lines.
//
// SIZE is the spring length: T = 25 ms + Size · 1 ms. T glides like the
// Tank's lengths (DESIGN.md §9.3): a one-pole smoother towards the ROUNDED
// target, slew-limited to 0.25 samples per sample, read fractionally while it
// moves and with an integer tap once it has arrived.

#include <algorithm>
#include <cmath>

#include "bloom_dsp.h"

namespace bloom {

constexpr int kSpringSections = 80;          // M, DESIGN.md §6.1
constexpr double kSpringTransitionHz = 4500.0;  // fc, the spring's transition frequency
constexpr double kSpringBaseSeconds = 0.025;    // T = 25 ms + Size · 1 ms
constexpr double kSpringSecondsPerMetre = 0.001;
constexpr double kSpringRightLength = 1.071;    // R spring's T = L's × 1.071 (§6.1)
constexpr double kSpringRightChirp = 0.02;      // R spring's a = L's − 0.02
constexpr double kSpringMaxSizeMetres = 40.0;   // the Size range's top (DESIGN.md §10)
constexpr double kSpringSlew = 0.25;            // samples per sample, as §9.3

// The stretch K of every dispersion section at fs: round(fs / (2 fc)), ≥ 1.
inline int springStretch(double fs) {
  return std::max(1, static_cast<int>(std::lround(fs / (2.0 * kSpringTransitionHz))));
}

// The chirp amount a for a Diffusion D in 0 … 1 (DESIGN.md §6.1), L spring.
inline double springChirp(double diffusion) { return 0.30 + 0.45 * diffusion; }

// The dispersion cascade alone: M stretched first-order allpasses, all with
// the same K and a. Public so G5.1 can measure it on its own.
struct SpringDispersion {
  StretchedAllpass sec[kSpringSections];
  int k = 1;
  double a = 0.0;

  void init(double fs) {
    k = springStretch(fs);
    for (auto& s : sec) s.init(k + 1);
  }
  void reset() {
    for (auto& s : sec) s.reset();
  }
  void setChirp(double chirp) {
    a = chirp;
    for (auto& s : sec) s.set(k, chirp);
  }
  float process(float x) {
    for (auto& s : sec) x = s.process(x);
    return x;
  }
  // The cascade's closed-form group delay at `hz`, in samples (§6.1's Tloop).
  double groupDelay(double hz, double fs) const {
    return kSpringSections * stretchedAllpassGroupDelay(k, a, 2.0 * kPi * hz / fs);
  }
};

class Spring {
public:
  // Everything the springs read per sub-block. Units: seconds, Hz.
  struct Block {
    double rt60 = 2.0;
    double diffusion = 0.65;  // D, 0 … 1: the chirp amount (§6.1)
    double dampHighHz = 20000.0;  // the LPF pair sits at min(this, fc), always in
    bool dampLowOn = false;       // the HPF, as in the Tank
    double dampLowHz = 20.0;
  };

  void init(double fs) {
    fs_ = fs;
    const int maxT = static_cast<int>(std::ceil(
        (kSpringBaseSeconds + kSpringMaxSizeMetres * kSpringSecondsPerMetre) * kSpringRightLength * fs));
    for (int c = 0; c < 2; ++c) {
      disp_[c].init(fs);
      delay_[c].init(maxT + 4);
      len_[c].setTimeConstant(0.150, fs);  // DESIGN.md §9.1, as the Tank's scale
    }
    reset();
  }

  void reset() {
    for (int c = 0; c < 2; ++c) {
      disp_[c].reset();
      delay_[c].reset();
      for (auto& f : lp_[c]) f.reset();
      hp_[c].reset();
      fb_[c] = 0.0f;
    }
    hpOn_ = false;
    snapPending_ = true;
  }

  // The loop length T, in samples, for a Size in metres: the L spring's, and
  // the R spring's × 1.071. Rounded: at rest the read is an integer tap.
  void setSizeTarget(double sizeMetres) {
    sizeMetres = std::min(kSpringMaxSizeMetres, std::max(0.0, sizeMetres));
    const double tl = (kSpringBaseSeconds + sizeMetres * kSpringSecondsPerMetre) * fs_;
    len_[0].setTarget(static_cast<double>(rounded(tl)));
    len_[1].setTarget(static_cast<double>(rounded(tl * kSpringRightLength)));
  }

  // Once per sub-block (DESIGN.md §9.2).
  void setBlock(const Block& b) {
    if (snapPending_) {
      for (auto& l : len_) l.snap();
      snapPending_ = false;
    }
    const double lpHz = std::min(std::min(b.dampHighHz, kSpringTransitionHz), 0.45 * fs_);
    for (int c = 0; c < 2; ++c) {
      disp_[c].setChirp(springChirp(b.diffusion) - (c == 1 ? kSpringRightChirp : 0.0));
      // gS from the whole loop: T plus the cascade's group delay at 1 kHz,
      // so Decay is RT60 at 1 kHz (§6.1).
      const double loop = len_[c].value + disp_[c].groupDelay(1000.0, fs_);
      gain_[c] = std::pow(10.0, -3.0 * loop / (b.rt60 * fs_));
      for (auto& f : lp_[c]) f.setLowpass(lpHz, fs_);
    }
    if (b.dampLowOn && !hpOn_) for (auto& f : hp_) f.reset();
    hpOn_ = b.dampLowOn;
    if (hpOn_) for (auto& f : hp_) f.setHighpass(b.dampLowHz, fs_);
  }

  // One stereo sample in (the pre-delayed wet input), the two springs out.
  void processSample(float inL, float inR, float& outL, float& outR) {
    const float in[2] = {inL, inR};
    float out[2];
    for (int c = 0; c < 2; ++c) {
      // ---- T: a per-sample glide, slew-limited ----
      Smoother& l = len_[c];
      if (!l.settled()) {
        const double prev = l.value;
        const double t = l.step();
        l.value = std::min(prev + kSpringSlew, std::max(prev - kSpringSlew, t));
        // At T ≈ 2640 one step, k·d with k ≈ 1.4e−4, falls below a double's
        // resolution (4.5e−13) while d is still ~3e−9; Smoother snaps there
        // now, but only some 3 s into the glide. A millionth of a sample is
        // inaudible, so snap sooner, here.
        if (std::fabs(l.target - l.value) < 1e-6) l.value = l.target;
      }
      float v = in[c] + fb_[c];
      v = disp_[c].process(v);
      // Read before the write: a d-sample delay. The feedback adds the one
      // sample of fb_, so d = T − 1 makes the loop T plus the dispersion.
      const double d = l.value - 1.0;
      const float y0 = l.settled() ? delay_[c].tap(static_cast<int>(d)) : delay_[c].tapLinear(std::max(1.0, d));
      delay_[c].write(v);
      float y = lp_[c][1].process(lp_[c][0].process(y0));
      out[c] = y;
      if (hpOn_) y = hp_[c].process(y);
      fb_[c] = static_cast<float>(gain_[c]) * y;
    }
    outL = out[0];
    outR = out[1];
  }

  // Bytes of delay memory (GATES G6.5).
  size_t memoryBytes() const {
    size_t n = 0;
    for (int c = 0; c < 2; ++c) {
      n += delay_[c].bytes();
      for (const auto& s : disp_[c].sec) n += s.ap.line.bytes();
    }
    return n;
  }

  // TEST ONLY: the L spring's current loop delay T, in samples.
  double testLength() const { return len_[0].value; }

private:
  static int rounded(double v) { return std::max(2, static_cast<int>(std::lround(v))); }

  double fs_ = 48000.0;
  SpringDispersion disp_[2];
  DelayLine delay_[2];
  Biquad lp_[2][2];
  OnePole hp_[2];
  Smoother len_[2];
  double gain_[2] = {0.0, 0.0};
  float fb_[2] = {0.0f, 0.0f};
  bool hpOn_ = false;
  bool snapPending_ = true;
};

}  // namespace bloom
