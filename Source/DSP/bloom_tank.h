#pragma once

// bloom_tank.h — the late network's Tank: Dattorro's figure-of-eight
// (docs/DESIGN.md §5, GATES G4), with NassauBloom's deviations: one input
// diffusion chain per channel (§5.2), unity cross-feed (§5.3), the Dense
// extras (§5.3), and Size as a gliding delay scale (§9.3).
//
// Header-only, allocation in init() only (GATES R3), no transcendental per
// sample (R5): setBlock() does the pow()s once per sub-block, and
// processSample() only multiplies, adds and reads delay lines.
//
// HOW SIZE GLIDES. Every Size-scaled length (the five in-loop elements per
// branch and the fourteen output taps) lands on an INTEGER at rest, because
// at rest the tank reads with integer taps (exact, and time-invariant at
// Mod 0, G4.8). During a glide each length moves linearly from where it was
// to its new rounded target, all of them in step with the smoothed,
// slew-limited scale. So a glide ends exactly on the integers the settled
// tank will read, and the switch back to integer reads is seamless. (Scaling
// each length by the moving S directly leaves n·S up to half a sample off the
// rounded length at the start and end of a glide. That jump was below the
// linear-interpolation artefacts of the glide itself when measured (G4.10,
// -50 dB either way), but this way there is no jump to measure.)

#include <algorithm>
#include <cmath>

#include "bloom_dsp.h"

namespace bloom {

constexpr double kDattorroRate = 29761.0;  // the paper's sample rate
constexpr double kTankScaleMin = 0.25;     // DESIGN.md §5.3
constexpr double kTankScaleMax = 1.4;      // DESIGN.md §5.3
constexpr double kTankExcursion = 12.0;    // samples at 29761 Hz, DESIGN.md §5.7
constexpr double kTankSlew = 0.25;         // samples per sample on the longest line, §9.3

// The tank scale S for a Size in metres (DESIGN.md §5.3).
inline double tankScale(double sizeMetres) {
  return std::min(kTankScaleMax, std::max(kTankScaleMin, sizeMetres / 12.0));
}

class Tank {
public:
  // Everything the tank reads per sub-block. Units: seconds, Hz, samples.
  struct Block {
    double rt60 = 2.0;
    double diffusion = 0.7;     // D, 0 … 1 (§5.2)
    double excursion = 0.0;     // modulation depth in samples at fs (§5.7)
    bool dense = false;
    bool dampHighOn = false;    // in-loop LPF (§5.4)
    bool dampLowOn = false;     // in-loop HPF
    double dampHighHz = 20000.0;
    double dampLowHz = 20.0;
  };

  void init(double fs) {
    fs_ = fs;
    unit_ = fs / kDattorroRate;
    const double maxScale = unit_ * kTankScaleMax;
    const int excursionMargin = static_cast<int>(std::ceil(kTankExcursion * unit_)) + 4;
    auto sized = [&](double base) { return static_cast<int>(std::ceil(base * maxScale)) + excursionMargin; };
    for (int c = 0; c < 2; ++c) {
      // Input diffusion is not Size-scaled (§5.2): fixed integer lengths.
      for (int k = 0; k < 4; ++k) {
        inDiffLen_[c][k] = rounded(kInDiffLen[c][k] * unit_);
        inDiff_[c][k].init(inDiffLen_[c][k] + 2);
      }
      ap1_[c].init(sized(kBase[c * 5 + kAp1]));
      ap3_[c].init(sized(kBase[c * 5 + kAp3]));
      d1_[c].init(sized(kBase[c * 5 + kD1]));
      ap2_[c].init(sized(kBase[c * 5 + kAp2]));
      d2_[c].init(sized(kBase[c * 5 + kD2]));
    }
    // The slew limit, in S per sample: the longest line (24–30, 4453 at
    // 29761 Hz) may change its length by at most kTankSlew samples per sample.
    maxScaleStep_ = kTankSlew / (kBase[kD1] * unit_);
    scale_.setTimeConstant(0.150, fs);  // DESIGN.md §9.1
    lfo_[0].setFrequency(0.73, fs);
    lfo_[1].setFrequency(0.89, fs);
    lfo_[2].setFrequency(1.07, fs);
    lfo_[3].setFrequency(0.61, fs);
    reset();
  }

  void reset() {
    for (int c = 0; c < 2; ++c) {
      for (auto& a : inDiff_[c]) a.reset();
      ap1_[c].reset();
      ap3_[c].reset();
      d1_[c].reset();
      ap2_[c].reset();
      d2_[c].reset();
      lp_[c].reset();
      hp_[c].reset();
      cross_[c] = 0.0f;
    }
    lpOn_ = hpOn_ = false;
    for (int k = 0; k < 4; ++k) lfo_[k].setPhase(0.25 * k);  // §5.7: 0, ¼, ½, ¾
    snapPending_ = true;
  }

  void setScaleTarget(double s) {
    s = std::min(kTankScaleMax, std::max(kTankScaleMin, s));
    scale_.setTarget(s);
    if (snapPending_ || s == glideTo_) return;
    // A new target: re-anchor every length's glide at where it is now.
    glideFrom_ = scale_.value;
    glideTo_ = s;
    for (int i = 0; i < kNumLengths; ++i) {
      fromLen_[i] = curLen_[i];
      toLen_[i] = static_cast<double>(rounded(kBase[i] * unit_ * s));
    }
  }

  // Once per sub-block (DESIGN.md §9.2).
  void setBlock(const Block& b) {
    if (snapPending_) {
      scale_.snap();
      glideFrom_ = glideTo_ = scale_.value;
      for (int i = 0; i < kNumLengths; ++i)
        curLen_[i] = fromLen_[i] = toLen_[i] = static_cast<double>(rounded(kBase[i] * unit_ * scale_.value));
      snapPending_ = false;
    }
    blk_ = b;
    for (int c = 0; c < 2; ++c) {
      // Decay gain from the branch's loop-segment length (DESIGN.md §5.5),
      // applied ONCE per branch; the cross-feed is unity.
      const double lb = curLen_[c * 5 + kAp1] + (b.dense ? curLen_[c * 5 + kAp3] : 0.0) +
                        curLen_[c * 5 + kD1] + curLen_[c * 5 + kAp2] + curLen_[c * 5 + kD2];
      gain_[c] = std::pow(10.0, -3.0 * lb / (b.rt60 * fs_));
      ap1_[c].g = -0.70;                                            // decayDiff1, §5.1
      ap2_[c].g = std::min(0.50, std::max(0.25, gain_[c] + 0.15));  // decayDiff2
      ap3_[c].g = 0.65;                                             // Dense, §5.3
      for (int k = 0; k < 4; ++k) inDiff_[c][k].g = (k < 2 ? 0.75 : 0.625) * b.diffusion;
    }
    const bool lpOn = b.dampHighOn, hpOn = b.dampLowOn;
    if (lpOn && !lpOn_) for (auto& f : lp_) f.reset();
    if (hpOn && !hpOn_) for (auto& f : hp_) f.reset();
    lpOn_ = lpOn;
    hpOn_ = hpOn;
    if (lpOn_) for (auto& f : lp_) f.setLowpass(std::min(b.dampHighHz, 0.45 * fs_), fs_);
    if (hpOn_) for (auto& f : hp_) f.setHighpass(b.dampLowHz, fs_);
    for (auto& l : lfo_) l.renormalize();
  }

  double scale() const { return scale_.value; }

  // Become a copy of `o`'s running state (DESIGN.md §9.4: a Plain/Dense switch
  // starts the new structure from the current tank). Both tanks must have
  // been init()ed at the same fs; no allocation. With `zeroDenseExtras` the
  // Dense-only allpasses start at zero instead, which is what a switch INTO
  // Dense wants: whatever they hold is left over from some earlier use.
  void copyStateFrom(const Tank& o, bool zeroDenseExtras) {
    for (int c = 0; c < 2; ++c) {
      for (int k = 0; k < 4; ++k) inDiff_[c][k].copyFrom(o.inDiff_[c][k]);
      ap1_[c].copyFrom(o.ap1_[c]);
      ap3_[c].copyFrom(o.ap3_[c]);
      if (zeroDenseExtras) ap3_[c].reset();
      ap2_[c].copyFrom(o.ap2_[c]);
      d1_[c].copyFrom(o.d1_[c]);
      d2_[c].copyFrom(o.d2_[c]);
      lp_[c] = o.lp_[c];
      hp_[c] = o.hp_[c];
      cross_[c] = o.cross_[c];
      gain_[c] = o.gain_[c];
    }
    lpOn_ = o.lpOn_;
    hpOn_ = o.hpOn_;
    for (int k = 0; k < 4; ++k) lfo_[k] = o.lfo_[k];
    scale_ = o.scale_;
    glideFrom_ = o.glideFrom_;
    glideTo_ = o.glideTo_;
    for (int i = 0; i < kNumLengths; ++i) {
      curLen_[i] = o.curLen_[i];
      fromLen_[i] = o.fromLen_[i];
      toLen_[i] = o.toLen_[i];
    }
    snapPending_ = o.snapPending_;
    blk_ = o.blk_;
  }

  // Bytes of delay memory (GATES G6.5).
  size_t memoryBytes() const {
    size_t n = 0;
    for (int c = 0; c < 2; ++c) {
      for (const auto& a : inDiff_[c]) n += a.line.bytes();
      n += ap1_[c].line.bytes() + ap3_[c].line.bytes() + ap2_[c].line.bytes() + d1_[c].bytes() + d2_[c].bytes();
    }
    return n;
  }

  // TEST ONLY (GATES G4.10): the current length, in samples, of delay 24–30.
  double testD1Length() const { return curLen_[kD1]; }

  // One stereo sample in (the pre-delayed wet input), the raw tap sums out.
  void processSample(float inL, float inR, float& outL, float& outR) {
    // ---- Size: a per-sample glide, slew-limited (DESIGN.md §9.3) ----
    if (!scale_.settled()) {
      const double prev = scale_.value;
      double s = scale_.step();
      s = std::min(prev + maxScaleStep_, std::max(prev - maxScaleStep_, s));
      scale_.value = s;
      // Each length moves in step with S from its anchor to its rounded
      // target, and IS the target once S has arrived.
      const double span = glideTo_ - glideFrom_;
      const double q = scale_.settled() || span == 0.0
                           ? 1.0
                           : std::min(1.0, std::max(0.0, (s - glideFrom_) / span));
      for (int i = 0; i < kNumLengths; ++i) curLen_[i] = fromLen_[i] + q * (toLen_[i] - fromLen_[i]);
    }
    // Integer reads when the tank is time-invariant: Size settled and no
    // modulation (§5.7). Otherwise fractional, linearly interpolated. At an
    // integer length the two reads are the same value (G1.5), so switching
    // between them is seamless.
    const bool exact = scale_.settled() && blk_.excursion == 0.0;

    const bool mod = blk_.excursion > 0.0;
    const bool modAp2 = blk_.dense && mod;
    const double mod1[2] = {mod ? blk_.excursion * lfo_[0].tick() : 0.0,
                            mod ? blk_.excursion * lfo_[1].tick() : 0.0};
    const double mod2[2] = {modAp2 ? blk_.excursion * lfo_[2].tick() : 0.0,
                            modAp2 ? blk_.excursion * lfo_[3].tick() : 0.0};

    // ---- Input diffusion, one chain per channel (§5.2) ----
    float x[2] = {inL, inR};
    for (int c = 0; c < 2; ++c)
      for (int k = 0; k < 4; ++k) x[c] = inDiff_[c][k].process(x[c], inDiffLen_[c][k]);

    // ---- The two branches; each reads the OTHER's output of the previous
    //      sample (the figure-of-eight, unity cross-feed, §5.3) ----
    const float crossIn[2] = {cross_[1], cross_[0]};
    for (int c = 0; c < 2; ++c) {
      const double* L = &curLen_[c * 5];
      float v = x[c] + crossIn[c];
      v = exact ? ap1_[c].process(v, asInt(L[kAp1])) : ap1_[c].processFrac(v, L[kAp1] + mod1[c]);
      if (blk_.dense) v = exact ? ap3_[c].process(v, asInt(L[kAp3])) : ap3_[c].processFrac(v, L[kAp3]);
      v = delay(d1_[c], v, L[kD1], exact);
      if (lpOn_) v = lp_[c].process(v);
      if (hpOn_) v = hp_[c].process(v);
      v *= static_cast<float>(gain_[c]);
      v = (exact && !modAp2) ? ap2_[c].process(v, asInt(L[kAp2])) : ap2_[c].processFrac(v, L[kAp2] + mod2[c]);
      v = delay(d2_[c], v, L[kD2], exact);
      cross_[c] = v;
    }

    // ---- Output taps (Dattorro Table 2, DESIGN.md §5.6) ----
    // tap(1) is the last write itself, so "k behind the last write" is k + 1.
    auto t = [&](const DelayLine& l, int tapIndex) {
      const double k = curLen_[kFirstTap + tapIndex];
      return exact ? l.tap(asInt(k) + 1) : l.tapLinear(k + 1.0);
    };
    outL = t(d1_[1], 0) + t(d1_[1], 1) - t(ap2_[1].line, 2) + t(d2_[1], 3) - t(d1_[0], 4) -
           t(ap2_[0].line, 5) - t(d2_[0], 6);
    outR = t(d1_[0], 7) + t(d1_[0], 8) - t(ap2_[0].line, 9) + t(d2_[0], 10) - t(d1_[1], 11) -
           t(ap2_[1].line, 12) - t(d2_[1], 13);
  }

private:
  // Per-branch element slots in the length tables; branch c's element k is
  // at c * 5 + k.
  static constexpr int kAp1 = 0, kAp3 = 1, kD1 = 2, kAp2 = 3, kD2 = 4;
  static constexpr int kFirstTap = 10;
  static constexpr int kNumLengths = 24;
  // Dattorro's lengths at 29761 Hz (DESIGN.md §5.3, §5.6): the L branch's
  // 23–24, Dense extra, 24–30, 31–33, 33–39; the R branch's 46–48, Dense
  // extra, 48–54, 55–59, 59–63; then yL's seven taps and yR's seven.
  static constexpr double kBase[kNumLengths] = {
      672, 331, 4453, 1800, 3720,
      908, 397, 4217, 2656, 3163,
      266, 2974, 1913, 1996, 1990, 187, 1066,
      353, 3627, 1228, 2673, 2111, 335, 121};
  static constexpr double kInDiffLen[2][4] = {{142, 107, 379, 277}, {151, 113, 401, 293}};

  static int rounded(double v) { return std::max(1, static_cast<int>(std::lround(v))); }
  static int asInt(double v) { return static_cast<int>(v); }  // exact only: v is integral

  static float delay(DelayLine& l, float x, double d, bool exact) {
    const float y = exact ? l.tap(asInt(d)) : l.tapLinear(std::max(1.0, d));
    l.write(x);
    return y;
  }

  double fs_ = 48000.0, unit_ = 48000.0 / kDattorroRate;
  Allpass inDiff_[2][4];
  int inDiffLen_[2][4] = {};
  Allpass ap1_[2], ap3_[2], ap2_[2];
  DelayLine d1_[2], d2_[2];
  OnePole lp_[2], hp_[2];
  bool lpOn_ = false, hpOn_ = false;
  float cross_[2] = {0.0f, 0.0f};
  double gain_[2] = {0.0, 0.0};
  RotLfo lfo_[4];
  Smoother scale_;
  double maxScaleStep_ = 0.0;
  double glideFrom_ = 1.0, glideTo_ = 1.0;
  double curLen_[kNumLengths] = {};
  double fromLen_[kNumLengths] = {};
  double toLen_[kNumLengths] = {};
  bool snapPending_ = true;
  Block blk_;
};

}  // namespace bloom
