// NassauBloom preset tests — Gate G0 (docs/GATES.md G0.3, G0.5).
//
// The preset TABLE and the parameter RANGES both live in
// Source/Plugin/nassau_presets.h, the one copy the plugin also reads, so these
// checks are about the same numbers the plugin ships. No SDK link is needed.
//
// G7 extends this binary with the measured preset claims (RT60 per preset,
// mono plate L == R, the drum preset's dry first 12 ms, ...). Those need the
// reverb itself, which G0 does not have.

#include "bloom_core.h"
#include "nassau_presets.h"
#include "test_util.h"

#include <cmath>
#include <cstring>
#include <string>

int main() {
  using namespace nassau_presets;
  std::cout << "=== NassauBloom Preset Tests (G0) ===\n\n";

  std::cout << "Group: table shape (G0.3)\n";
  check("kParamCount == 16", kParamCount == 16);
  check("kPresetCount == 9", kPresetCount == 9);
  check("Params() has 16 entries", Params().size() == 16);
  check("Presets() has 9 entries", Presets().size() == 9);

  std::cout << "\nGroup: parameter specs are well-formed\n";
  {
    bool ok = true;
    for (const auto& p : Params()) {
      ok &= p.name != nullptr && std::strlen(p.name) > 0;
      ok &= p.minValue < p.maxValue;
      ok &= p.defaultValue >= p.minValue && p.defaultValue <= p.maxValue;
      if (p.kind == Kind::Enum)
        ok &= p.minValue == 0.0 && p.maxValue == 1.0 &&
              p.enumLabels[0] != nullptr && p.enumLabels[1] != nullptr;
    }
    check("every spec: named, min < max, default inside the range", ok);
  }

  std::cout << "\nGroup: every preset value is inside its range (G0.5)\n";
  for (const auto& preset : Presets()) {
    bool inRange = true;
    for (size_t i = 0; i < preset.values.size(); ++i) {
      const auto& p = Params()[i];
      const double v = preset.values[i];
      inRange &= std::isfinite(v) && v >= p.minValue && v <= p.maxValue;
      if (p.kind == Kind::Enum) inRange &= (v == std::floor(v));
    }
    const std::string name = std::string(preset.name) + ": all values in range";
    check(name.c_str(), inRange);
  }

  std::cout << "\nGroup: preset names are non-empty and unique (G0.5)\n";
  {
    bool allNonEmpty = true;
    bool allUnique = true;
    for (size_t i = 0; i < Presets().size(); ++i) {
      allNonEmpty &= Presets()[i].name != nullptr && std::strlen(Presets()[i].name) > 0;
      for (size_t j = i + 1; j < Presets().size(); ++j)
        allUnique &= std::strcmp(Presets()[i].name, Presets()[j].name) != 0;
    }
    check("names non-empty", allNonEmpty);
    check("names unique", allUnique);
  }

  std::cout << "\nGroup: preset 0 is the defaults\n";
  check("Presets()[0].values == Defaults()", Presets()[0].values == Defaults());

  std::cout << "\nGroup: every preset applies to a bare core and stays finite\n";
  for (const auto& preset : Presets()) {
    BloomCore core;
    core.init(48000.0f);
    ApplyToCore(core, preset.values);
    auto l = testSignal(TestSignal::Xorshift, 48000.0, 1.0, 0xB100Bu);
    auto r = testSignal(TestSignal::Xorshift, 48000.0, 1.0, 0x5EED5u);
    core.process(l.data(), r.data(), l.data(), r.data(), static_cast<int>(l.size()));
    bool finite = true;
    for (size_t i = 0; i < l.size(); ++i) finite &= std::isfinite(l[i]) && std::isfinite(r[i]);
    const std::string name = std::string(preset.name) + ": output finite";
    check(name.c_str(), finite);
  }

  return testSummary();
}
