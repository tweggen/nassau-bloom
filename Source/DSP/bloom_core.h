#pragma once

#include <atomic>

#include "bloom_dsp.h"
#include "bloom_er.h"
#include "bloom_spring.h"
#include "bloom_tank.h"

/**
 * BloomCore — the NassauBloom stereo reverb DSP core.
 *
 * G6 (docs/GATES.md): every stage is real. A switch of Algorithm or
 * Complexity crossfades the late output over 2048 samples (DESIGN.md §9.4).

 *
 * Design contract (docs/DESIGN.md, kept stable across gates):
 *   - Zero framework dependencies (GATES R2).
 *   - Lock-free parameters: every setter stores into a std::atomic; process()
 *     only loads. No mutex, allocation or I/O in the audio path (R3).
 *     Allocation happens in init() only.
 *   - Parameters are in USER UNITS (%, Hz, ms, m, s, dB), exactly what
 *     IParam::Value() returns; the core does every unit conversion. Enums are
 *     ints. Out-of-range values are clamped to DESIGN.md §10's ranges.
 *   - Block-based, stereo, float I/O. In-place safe (out may alias in).
 *     Any block size, including 1.
 *   - After init() or reset(), the FIRST process() starts every smoother AT
 *     its target (DESIGN.md §9.6), whatever was set in between. A host that
 *     pushes all parameters after init() therefore gets no start-up ramp.
 *   - Create once, never copy/move.
 */
class BloomCore {
public:
  static constexpr int kComplexityPlain = 0;
  static constexpr int kComplexityDense = 1;
  static constexpr int kAlgorithmTank = 0;
  static constexpr int kAlgorithmSpring = 1;

  /// Coefficients are recomputed once per sub-block (DESIGN.md §9.2).
  static constexpr int kSubBlock = 32;

  // ===== Lifecycle =====
  BloomCore();
  ~BloomCore();

  BloomCore(const BloomCore&) = delete;
  BloomCore& operator=(const BloomCore&) = delete;
  BloomCore(BloomCore&&) = delete;
  BloomCore& operator=(BloomCore&&) = delete;

  /// Size every buffer for `sampleRate` (8 kHz … 192 kHz) and reset all
  /// state. Allocates; call from OnReset(), never from process().
  void init(float sampleRate);

  /// Clear every delay line and filter, and make the next process() snap the
  /// smoothers to their targets (DESIGN.md §9.6). Parameters are untouched.
  /// Does not allocate.
  void reset();

  // ===== Audio Processing =====

  /// Process one block of stereo audio. out may alias in.
  void process(const float* inL, const float* inR,
               float* outL, float* outR,
               int numSamples);

  // ===== Parameter setters (DESIGN.md §10; user units) =====
  void setMix(float percent);           // 0 … 100
  void setInputHpf(float hz);           // 20 … 1000, bypassed at ≤ 20
  void setInputLpf(float hz);           // 1000 … 20000, bypassed at ≥ 20000
  void setPreDelayMs(float ms);         // 0 … 250
  void setSize(float metres);           // 2 … 40
  void setErLevel(float percent);       // 0 … 200
  void setLateLevel(float percent);     // 0 … 200
  void setDecay(float seconds);         // RT60, 0.2 … 20
  void setDampHigh(float hz);           // 1000 … 20000, bypassed at ≥ 20000
  void setDampLow(float hz);            // 20 … 1000, bypassed at ≤ 20
  void setDiffusion(float percent);     // 0 … 100
  void setModulation(float percent);    // 0 … 100
  void setWidth(float percent);         // 0 … 200
  void setComplexity(int complexity);   // kComplexityPlain / kComplexityDense
  void setAlgorithm(int algorithm);     // kAlgorithmTank / kAlgorithmSpring
  void setOutputDb(float db);           // −24 … +12

  float sampleRate() const { return sampleRate_; }

  /// Bytes of delay memory this instance allocated in init() (GATES G6.5).
  size_t memoryBytes() const;

  // ===== Test hook (GATES R6 exception, stated in G2.3) =====
  /// TEST ONLY. For the next process() call, replace the summed wet signal
  /// (what the reverb paths produce, before Width) with these buffers, which
  /// must hold at least that call's numSamples. Cleared by that call. Lets a
  /// test drive the width and mix stages with a wet signal of its choosing.
  void testInjectWet(const float* wetL, const float* wetR) {
    testWetL_ = wetL;
    testWetR_ = wetR;
  }

  /// TEST ONLY. While on, both reverb paths are replaced by a wire: the wet
  /// sum IS the filtered, pre-delayed input. Keeps G2's bit-exact checks of
  /// the routing meaningful once the paths are real reverbs.
  void testWireReverb(bool on) { testWire_ = on; }

  /// TEST ONLY (GATES G3.8). How many times the ER taps have been computed
  /// since init(), counting both channels of a set as one.
  int testErRecomputeCount() const { return erRecomputes_; }

  /// TEST ONLY (GATES G4.10). The current length of the Tank's delay 24–30.
  double testTankD1Length() const { return tank_[late_.tank].testD1Length(); }

  /// TEST ONLY (GATES G6). The late crossfade's length in samples for the
  /// next switches; 0 switches hard. Default 2048 (DESIGN.md §9.4).
  void testLateCrossfade(int samples) { lateFadeLen_ = samples < 0 ? 0 : samples; }

  /// TEST ONLY (GATES G6.3). How many late structures the last processed
  /// sample ran: 2 during a crossfade, 1 otherwise.
  int testLateStructuresRun() const { return lateRun_; }

  /// TEST ONLY. The L spring's current loop delay T, in samples.
  double testSpringLength() const { return spring_.testLength(); }

private:
  void beginSubBlock();

  float sampleRate_ = 48000.0f;
  double fs_ = 48000.0;
  bool snapPending_ = true;

  // ---- Parameters (user units) ----
  std::atomic<float> mix_{25.0f};
  std::atomic<float> inputHpf_{20.0f};
  std::atomic<float> inputLpf_{20000.0f};
  std::atomic<float> preDelayMs_{10.0f};
  std::atomic<float> size_{12.0f};
  std::atomic<float> erLevel_{100.0f};
  std::atomic<float> lateLevel_{100.0f};
  std::atomic<float> decay_{2.0f};
  std::atomic<float> dampHigh_{8000.0f};
  std::atomic<float> dampLow_{20.0f};
  std::atomic<float> diffusion_{70.0f};
  std::atomic<float> modulation_{25.0f};
  std::atomic<float> width_{100.0f};
  std::atomic<int> complexity_{kComplexityPlain};
  std::atomic<int> algorithm_{kAlgorithmTank};
  std::atomic<float> outputDb_{0.0f};

  // ---- Smoothers (DESIGN.md §9.1) ----
  // Per sample: the mix and output as LINEAR gains (§8, R5), the width, and
  // the pre-delay in samples.
  bloom::Smoother dryGain_, wetGain_, outGain_, width_s_, preDelay_;
  // Per sub-block: the input filter cutoffs in Hz.
  bloom::Smoother hpfHz_, lpfHz_;

  // ---- Input filters (DESIGN.md §2) ----
  bloom::Biquad hpf_[2], lpf_[2];
  bool hpfOn_ = false, lpfOn_ = false;

  // ---- Pre-delay (DESIGN.md §3) ----
  bloom::DelayLine pre_[2];
  double preMax_ = 0.0;  // samples

  // ---- Early reflections (DESIGN.md §4) ----
  void startErSet(int set, double size, int order);
  bloom::DelayLine er_[2];
  bloom::ErTaps erSet_[2][2];   // [set A/B][channel L/R]
  int erActive_ = 0;            // the set being heard
  int erFade_ = 0;              // samples of crossfade left, 0 = none
  double erSize_ = 0.0;         // Size the active set was computed for
  int erOrder_ = 0;             // reflection order it was computed for
  int erRecomputes_ = 0;
  bloom::OnePole erLp_[2], erHp_[2];
  bool erLpOn_ = false, erHpOn_ = false;

  // Per sample: the path levels as linear gains. Per sub-block: the wall
  // damping cutoffs, shared by the late network from G4 on (§5.4).
  bloom::Smoother erGain_, lateGain_;
  bloom::Smoother dampHighHz_, dampLowHz_;

  // ---- Late network: the Tank (§5), the Spring (§6), the switch (§9.4) ----
  // Two Tanks, so that a Plain/Dense switch can crossfade from the running
  // tank into a copy of it that continues in the other mode.
  struct LateSlot {
    int algorithm = kAlgorithmTank;
    int tank = 0;  // which of tank_[] (Tank only)
    bool dense = false;
    double out = 0.0;  // its output gain: §5.6's or §6.3's
  };
  void setUpLate(LateSlot& s, double sizeMetres);
  void processLate(const LateSlot& s, float xL, float xR, float& lL, float& lR);
  bloom::Tank tank_[2];
  bloom::Spring spring_;
  bloom::Smoother decay_s_, diffusion_s_, modulation_s_;  // per sub-block
  LateSlot late_;      // the structure being heard (crossfading in)
  LateSlot lateOld_;   // the one crossfading out
  int lateFade_ = 0;   // samples of crossfade left, 0 = none
  int lateFadeLen_ = 2048;
  int lateRun_ = 1;

  // ---- Test hooks ----
  const float* testWetL_ = nullptr;
  const float* testWetR_ = nullptr;
  bool testWire_ = false;
};
