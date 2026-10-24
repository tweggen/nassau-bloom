#pragma once
// NassauBloom parameter table and factory presets (docs/DESIGN.md §10, §11).
//
// DEPENDENCY-FREE (no iPlug2 includes), the nassau-zermatt boundary: the
// parameter RANGES, the preset TABLE and its effect on BloomCore can all be
// exercised from a plain test binary (Tests/preset_tests.cpp) with no SDK.
//
// SINGLE SOURCE OF TRUTH, twice over:
//   * kParams below is the one copy of every parameter's name, range, default
//     and kind. NassauBloomPlugin.cpp builds its Init* calls FROM it, so a
//     range can't differ between the plugin and the test that checks every
//     preset lies inside it (GATES G0.5).
//   * Each preset is a FULL 16-value array in enum order: the exact
//     positional layout of a factory preset chunk. The plugin builds its bank
//     with MakePresetFromChunk from this table, and the tests apply the same
//     table to a bare BloomCore (ApplyPresetToCore below).
//
// The index constants MUST match NassauBloomPlugin.h's EParams value for
// value; NassauBloomPlugin.cpp static_asserts each one.

#include "bloom_core.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <utility>

namespace nassau_presets {

// ===== Param indices (DESIGN.md §10) =====
constexpr int kMix = 0;
constexpr int kInputHpf = 1;
constexpr int kInputLpf = 2;
constexpr int kPreDelay = 3;
constexpr int kSize = 4;
constexpr int kErLevel = 5;
constexpr int kLateLevel = 6;
constexpr int kDecay = 7;
constexpr int kDampHigh = 8;
constexpr int kDampLow = 9;
constexpr int kDiffusion = 10;
constexpr int kModulation = 11;
constexpr int kWidth = 12;
constexpr int kComplexity = 13;
constexpr int kAlgorithm = 14;
constexpr int kOutputGain = 15;

constexpr int kParamCount = 16;
constexpr int kPresetCount = 9;

// ===== Parameter specs =====
enum class Kind { Double, Frequency, Enum };

struct ParamSpec {
  const char* name;
  Kind kind;
  double minValue;
  double maxValue;
  double defaultValue;
  double step;        // display/automation step (Double/Frequency)
  const char* unit;
  double shape;       // ShapePowCurve exponent; 1.0 = linear
  const char* enumLabels[2];  // Enum only (both enums have exactly two values)
};

inline const std::array<ParamSpec, static_cast<size_t>(kParamCount)>& Params() {
  static const std::array<ParamSpec, static_cast<size_t>(kParamCount)> kTable = {{
      {"Mix",         Kind::Double,    0.0,    100.0,   25.0,   0.1,  "%",  1.0, {nullptr, nullptr}},
      {"Input HPF",   Kind::Frequency, 20.0,   1000.0,  20.0,   0.1,  "Hz", 1.0, {nullptr, nullptr}},
      {"Input LPF",   Kind::Frequency, 1000.0, 20000.0, 20000.0, 0.1, "Hz", 1.0, {nullptr, nullptr}},
      {"Pre-Delay",   Kind::Double,    0.0,    250.0,   10.0,   0.1,  "ms", 1.0, {nullptr, nullptr}},
      {"Size",        Kind::Double,    2.0,    40.0,    12.0,   0.1,  "m",  1.0, {nullptr, nullptr}},
      {"ER Level",    Kind::Double,    0.0,    200.0,   100.0,  0.1,  "%",  1.0, {nullptr, nullptr}},
      {"Late Level",  Kind::Double,    0.0,    200.0,   100.0,  0.1,  "%",  1.0, {nullptr, nullptr}},
      {"Decay",       Kind::Double,    0.2,    20.0,    2.0,    0.01, "s",  3.0, {nullptr, nullptr}},
      {"Damp High",   Kind::Frequency, 1000.0, 20000.0, 8000.0, 0.1,  "Hz", 1.0, {nullptr, nullptr}},
      {"Damp Low",    Kind::Frequency, 20.0,   1000.0,  20.0,   0.1,  "Hz", 1.0, {nullptr, nullptr}},
      {"Diffusion",   Kind::Double,    0.0,    100.0,   70.0,   0.1,  "%",  1.0, {nullptr, nullptr}},
      {"Modulation",  Kind::Double,    0.0,    100.0,   25.0,   0.1,  "%",  1.0, {nullptr, nullptr}},
      {"Width",       Kind::Double,    0.0,    200.0,   100.0,  0.1,  "%",  1.0, {nullptr, nullptr}},
      {"Complexity",  Kind::Enum,      0.0,    1.0,     0.0,    1.0,  "",   1.0, {"Plain", "Dense"}},
      {"Algorithm",   Kind::Enum,      0.0,    1.0,     0.0,    1.0,  "",   1.0, {"Tank", "Spring"}},
      {"Output",      Kind::Double,    -24.0,  12.0,    0.0,    0.1,  "dB", 1.0, {nullptr, nullptr}},
  }};
  return kTable;
}

using ParamArray = std::array<double, static_cast<size_t>(kParamCount)>;

inline ParamArray Defaults() {
  ParamArray v{};
  for (size_t i = 0; i < v.size(); ++i) v[i] = Params()[i].defaultValue;
  return v;
}

struct PresetSpec {
  const char* name;
  ParamArray values;
};

// A full row, in enum order. Written out in full rather than as overrides of
// the defaults, because every preset sets nearly every knob, and the table in
// DESIGN.md §11 is a full table too: the two can be compared row by row.
inline ParamArray Row(double mix, double hpf, double lpf, double pre, double size,
                      double er, double late, double decay, double dampHi,
                      double dampLo, double diff, double mod, double width,
                      double complexity, double algorithm, double out) {
  return ParamArray{{mix, hpf, lpf, pre, size, er, late, decay, dampHi, dampLo,
                     diff, mod, width, complexity, algorithm, out}};
}

constexpr double kPlain = 0.0, kDense = 1.0;
constexpr double kTank = 0.0, kSpring = 1.0;

// ===== The 9 factory presets (DESIGN.md §11; the why is there) =====
inline const std::array<PresetSpec, static_cast<size_t>(kPresetCount)>& Presets() {
  static const std::array<PresetSpec, static_cast<size_t>(kPresetCount)> kTable = {{
      //                          Mix  HPF   LPF    Pre  Size  ER   Late  Decay DHi    DLo  Diff Mod Width Cx      Algo     Out
      {"Default",             Row(25,  20,   20000, 10,  12,   100, 100,  2.0,  8000,  20,  70,  25, 100,  kPlain, kTank,   0)},
      {"Guitar Amp Ambience", Row(18,  120,  9000,  3,   5,    140, 45,   0.6,  6000,  100, 60,  10, 110,  kPlain, kTank,   0)},
      {"Vocal Booth",         Row(12,  150,  12000, 0,   3,    120, 25,   0.35, 7000,  150, 50,  0,  80,   kPlain, kTank,   0)},
      {"Vocal Ballad Long",   Row(20,  200,  10000, 60,  25,   40,  100,  3.8,  6500,  180, 80,  40, 130,  kDense, kTank,   0)},
      {"Piano on Stage",      Row(22,  60,   14000, 20,  18,   110, 90,   1.9,  7500,  60,  70,  15, 120,  kDense, kTank,   0)},
      {"80s Bright Ballad Hall", Row(25, 300, 20000, 80, 30,   30,  120,  4.5,  16000, 500, 85,  50, 150,  kDense, kTank,   0)},
      {"60s Mono Plate",      Row(25,  80,   10000, 0,   8,    0,   100,  2.4,  9000,  20,  90,  0,  0,    kPlain, kTank,   0)},
      {"Spring Tank",         Row(30,  150,  5000,  0,   10,   0,   100,  2.2,  4500,  80,  65,  0,  60,   kPlain, kSpring, 0)},
      {"Pop Drum Verb",       Row(20,  150,  12000, 12,  10,   100, 100,  1.3,  9000,  200, 95,  20, 140,  kDense, kTank,   0)},
  }};
  return kTable;
}

// Apply a full parameter array to a bare core, the way the plugin's
// OnParamChange does one parameter at a time. Used by the preset tests.
inline void ApplyToCore(BloomCore& core, const ParamArray& v) {
  auto at = [&v](int i) { return v[static_cast<size_t>(i)]; };
  core.setMix(static_cast<float>(at(kMix)));
  core.setInputHpf(static_cast<float>(at(kInputHpf)));
  core.setInputLpf(static_cast<float>(at(kInputLpf)));
  core.setPreDelayMs(static_cast<float>(at(kPreDelay)));
  core.setSize(static_cast<float>(at(kSize)));
  core.setErLevel(static_cast<float>(at(kErLevel)));
  core.setLateLevel(static_cast<float>(at(kLateLevel)));
  core.setDecay(static_cast<float>(at(kDecay)));
  core.setDampHigh(static_cast<float>(at(kDampHigh)));
  core.setDampLow(static_cast<float>(at(kDampLow)));
  core.setDiffusion(static_cast<float>(at(kDiffusion)));
  core.setModulation(static_cast<float>(at(kModulation)));
  core.setWidth(static_cast<float>(at(kWidth)));
  core.setComplexity(static_cast<int>(at(kComplexity) + 0.5));
  core.setAlgorithm(static_cast<int>(at(kAlgorithm) + 0.5));
  core.setOutputDb(static_cast<float>(at(kOutputGain)));
}

}  // namespace nassau_presets
