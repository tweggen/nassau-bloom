// NassauBloom benchmark — Gate G6.5 and G6.3's timing half (docs/GATES.md).
// NOT a ctest: it reads a steady clock, and a busy machine moves its numbers.
//
//   * CPU per factory preset at 48 kHz in 512-sample blocks, as the fraction
//     of real time one instance takes (best of 5 runs of 10 s of noise).
//   * Per-instance delay memory at 192 kHz (BloomCore::memoryBytes()).
//   * G6.3: the Default preset (a Tank) after a switch to the Spring and back
//     costs within 5 % of the same settings on a fresh instance, i.e. the
//     structure switched away from is no longer processed.

#include "bloom_core.h"
#include "nassau_presets.h"
#include "test_util.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 512;
constexpr double kSeconds = 10.0;

// Seconds of CPU per second of audio for `core`, best of `runs`.
double cpuFraction(BloomCore& core, const std::vector<float>& x, int runs = 5) {
  std::vector<float> l(x.size()), r(x.size());
  double best = 1e30;
  for (int k = 0; k < runs; ++k) {
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < x.size(); pos += kBlock) {
      const int len = static_cast<int>(std::min<size_t>(kBlock, x.size() - pos));
      core.process(&x[pos], &x[pos], &l[pos], &r[pos], len);
    }
    const auto t1 = std::chrono::steady_clock::now();
    best = std::min(best, std::chrono::duration<double>(t1 - t0).count());
  }
  return best / (static_cast<double>(x.size()) / kFs);
}

void settle(BloomCore& core, int samples) {
  std::vector<float> z(static_cast<size_t>(kBlock), 0.0f), l(z.size()), r(z.size());
  for (int n = 0; n < samples; n += kBlock) core.process(z.data(), z.data(), l.data(), r.data(), kBlock);
}

}  // namespace

int main() {
  const auto x = testSignal(TestSignal::Xorshift, kFs, kSeconds);
  std::printf("NassauBloom bench: 48 kHz, %d-sample blocks, best of 5 x %.0f s of noise\n\n", kBlock, kSeconds);
  std::printf("  %-26s %10s %14s\n", "preset", "CPU", "ns / sample");
  for (const auto& p : nassau_presets::Presets()) {
    BloomCore core;
    core.init(static_cast<float>(kFs));
    nassau_presets::ApplyToCore(core, p.values);
    const double f = cpuFraction(core, x);
    std::printf("  %-26s %9.2f%% %14.1f\n", p.name, 100.0 * f, 1e9 * f / kFs);
  }

  for (double fs : {48000.0, 96000.0, 192000.0}) {
    BloomCore core;
    core.init(static_cast<float>(fs));
    std::printf("\nMemory at %.0f kHz: %.2f MB per instance (%zu bytes of delay lines)", fs / 1000.0,
                static_cast<double>(core.memoryBytes()) / (1024.0 * 1024.0), core.memoryBytes());
  }
  std::printf("\n");

  // ---- G6.3, timing half ----
  {
    const auto& def = nassau_presets::Presets()[0].values;  // Default: a Tank
    BloomCore fresh, switched;
    for (BloomCore* c : {&fresh, &switched}) {
      c->init(static_cast<float>(kFs));
      nassau_presets::ApplyToCore(*c, def);
    }
    settle(switched, 8192);
    switched.setAlgorithm(BloomCore::kAlgorithmSpring);
    settle(switched, 8192);  // the 2048-sample crossfade is long over
    switched.setAlgorithm(BloomCore::kAlgorithmTank);
    settle(switched, 8192);
    settle(fresh, 3 * 8192);
    // Interleave the two so a clock-speed change hits both.
    double a = 1e30, b = 1e30;
    for (int k = 0; k < 3; ++k) {
      a = std::min(a, cpuFraction(fresh, x, 2));
      b = std::min(b, cpuFraction(switched, x, 2));
    }
    const double d = b / a - 1.0;
    std::printf("\nG6.3: Default after Tank -> Spring -> Tank %.2f%%, fresh %.2f%%: %+.1f %% (want within +-5 %%) %s\n",
                100.0 * b, 100.0 * a, 100.0 * d, std::fabs(d) <= 0.05 ? "PASS" : "FAIL");
    return std::fabs(d) <= 0.05 ? 0 : 1;
  }
}
