// NassauBloom core tests — Gate G0 (docs/GATES.md).
//
// G0.4: at Mix 0 and Output 0 dB the core passes its input through
// bit-exactly, for odd and even block sizes, in place and out of place.
// Later gates add their own test binaries; this one stays the G0 battery.

#include "bloom_core.h"
#include "test_util.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr double kFs = 48000.0;

// Run `inL/inR` through `core` in blocks of `block`, out of place or in place.
void run(BloomCore& core, const std::vector<float>& inL, const std::vector<float>& inR,
         std::vector<float>& outL, std::vector<float>& outR, int block, bool inPlace) {
  const int n = static_cast<int>(inL.size());
  outL = inL;
  outR = inR;
  std::vector<float> scratchL(inL), scratchR(inR);
  for (int pos = 0; pos < n; pos += block) {
    const int len = std::min(block, n - pos);
    if (inPlace) {
      core.process(&outL[pos], &outR[pos], &outL[pos], &outR[pos], len);
    } else {
      core.process(&scratchL[pos], &scratchR[pos], &outL[pos], &outR[pos], len);
    }
  }
}

}  // namespace

int main() {
  std::cout << "=== NassauBloom Core Tests (G0) ===\n\n";

  const auto inL = testSignal(TestSignal::Xorshift, kFs, 10.0, 0xB100Bu);
  const auto inR = testSignal(TestSignal::Xorshift, kFs, 10.0, 0x5EED5u);

  std::cout << "Group: Mix 0, Output 0 dB is bit-exact (G0.4)\n";
  for (int block : {1, 31, 32, 33, 512, 4096}) {
    for (bool inPlace : {false, true}) {
      BloomCore core;
      core.init(static_cast<float>(kFs));
      core.setMix(0.0f);
      core.setOutputDb(0.0f);
      std::vector<float> outL, outR;
      run(core, inL, inR, outL, outR, block, inPlace);
      const std::string name = "block " + std::to_string(block) +
                               (inPlace ? ", in place" : ", out of place") +
                               ": output == input";
      check(name.c_str(), outL == inL && outR == inR);
    }
  }

  std::cout << "\nGroup: setters clamp to DESIGN.md §10's ranges\n";
  {
    // At Mix -50 (clamped to 0) the core must still be bit-exact; at
    // Output +100 dB (clamped to +12) the gain must be +12 dB, not +100.
    BloomCore core;
    core.init(static_cast<float>(kFs));
    core.setMix(-50.0f);
    core.setOutputDb(0.0f);
    std::vector<float> outL, outR;
    run(core, inL, inR, outL, outR, 512, false);
    check("Mix below range clamps to 0 (bit-exact)", outL == inL && outR == inR);

    core.setMix(0.0f);
    core.setOutputDb(100.0f);
    run(core, inL, inR, outL, outR, 512, false);
    // Measured after the first second: since G2 the output gain is smoothed
    // (20 ms, DESIGN.md §9.1), so a change on a running core ramps in.
    const int n = static_cast<int>(inL.size());
    const int from = static_cast<int>(kFs);
    const double gainDb = 20.0 * std::log10(rms(outL, from, n) / rms(inL, from, n));
    check("Output above range clamps to +12 dB", std::fabs(gainDb - 12.0) < 0.01);
  }

  return testSummary();
}
