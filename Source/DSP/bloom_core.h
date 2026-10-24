#pragma once

#include <atomic>

/**
 * BloomCore — the NassauBloom stereo reverb DSP core.
 *
 * G0 (docs/GATES.md): the FULL public API, with a body that computes only the
 * dry side of the mix law (out = dry · cos θ · outputGain). The wet path does
 * not exist yet; G2 wires it, G3–G5 fill it. The wrapper, the parameter
 * plumbing and the tests use this interface unchanged from here on.
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
 *   - Create once, never copy/move.
 */
class BloomCore {
public:
  static constexpr int kComplexityPlain = 0;
  static constexpr int kComplexityDense = 1;
  static constexpr int kAlgorithmTank = 0;
  static constexpr int kAlgorithmSpring = 1;

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

  /// Clear every delay line, filter and LFO, end any crossfade and snap the
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

private:
  float sampleRate_ = 48000.0f;

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
};
