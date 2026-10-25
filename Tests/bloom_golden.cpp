// NassauBloom golden-reference harness — Gate G6.4 (docs/GATES.md), the
// nassau-eq pattern.
//
//   bloom_golden generate [path]  runs the CURRENT BloomCore over the battery
//                                 and writes Tests/fixtures/golden.bin.
//   bloom_golden verify [path]    re-runs the same battery and compares with
//                                 the fixture: max abs error <= 1e-5, or a
//                                 nonzero exit. This is the ctest.
//
// The battery: every factory preset x {PluckBurst, SnareBurst}, 4 s at 48 kHz,
// the signal on both inputs (a mono source), processed in 512-sample blocks.
//
// The fixture keeps EVERY 16TH OUTPUT SAMPLE, not all of them: the full
// battery is 27.6 MB of float, the decimated one 1.7 MB (nassau-eq's is
// 2.1 MB). A reverb spreads any change in its DSP over the whole tail, so
// every 16th sample sees it as surely as every sample does.
//
// Format, little-endian: "BLGD", u32 version (1), u32 decimation, u32 case
// count; per case: u32 name length, the name, u32 frames, then that many
// float L then that many float R.

#include "bloom_core.h"
#include "nassau_presets.h"
#include "test_util.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef GOLDEN_FIXTURE_PATH
#define GOLDEN_FIXTURE_PATH "fixtures/golden.bin"
#endif

namespace {

constexpr double kFs = 48000.0;
constexpr double kSeconds = 4.0;
constexpr uint32_t kVersion = 1;
constexpr uint32_t kDecimation = 16;
constexpr double kTolerance = 1e-5;

struct CaseOut {
  std::string name;
  std::vector<float> l, r;  // decimated
};

std::vector<CaseOut> runBattery() {
  std::vector<CaseOut> out;
  const struct { const char* name; TestSignal sig; } inputs[] = {
      {"PluckBurst", TestSignal::PluckBurst}, {"SnareBurst", TestSignal::SnareBurst}};
  for (const auto& preset : nassau_presets::Presets()) {
    for (const auto& in : inputs) {
      const auto x = testSignal(in.sig, kFs, kSeconds);
      BloomCore core;
      core.init(static_cast<float>(kFs));
      nassau_presets::ApplyToCore(core, preset.values);
      std::vector<float> l(x.size()), r(x.size());
      for (size_t pos = 0; pos < x.size(); pos += 512) {
        const int len = static_cast<int>(std::min<size_t>(512, x.size() - pos));
        core.process(&x[pos], &x[pos], &l[pos], &r[pos], len);
      }
      CaseOut c;
      c.name = std::string(preset.name) + " / " + in.name;
      for (size_t i = 0; i < x.size(); i += kDecimation) {
        c.l.push_back(l[i]);
        c.r.push_back(r[i]);
      }
      out.push_back(std::move(c));
    }
  }
  return out;
}

void putU32(std::FILE* f, uint32_t v) {
  const unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                              static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24)};
  std::fwrite(b, 1, 4, f);
}

bool getU32(std::FILE* f, uint32_t& v) {
  unsigned char b[4];
  if (std::fread(b, 1, 4, f) != 4) return false;
  v = static_cast<uint32_t>(b[0]) | static_cast<uint32_t>(b[1]) << 8 | static_cast<uint32_t>(b[2]) << 16 |
      static_cast<uint32_t>(b[3]) << 24;
  return true;
}

int generate(const char* path) {
  const auto cases = runBattery();
  std::FILE* f = std::fopen(path, "wb");
  if (!f) { std::printf("cannot write %s\n", path); return 2; }
  std::fwrite("BLGD", 1, 4, f);
  putU32(f, kVersion);
  putU32(f, kDecimation);
  putU32(f, static_cast<uint32_t>(cases.size()));
  for (const auto& c : cases) {
    putU32(f, static_cast<uint32_t>(c.name.size()));
    std::fwrite(c.name.data(), 1, c.name.size(), f);
    putU32(f, static_cast<uint32_t>(c.l.size()));
    std::fwrite(c.l.data(), sizeof(float), c.l.size(), f);  // little-endian targets only
    std::fwrite(c.r.data(), sizeof(float), c.r.size(), f);
  }
  std::fclose(f);
  std::printf("wrote %zu cases to %s\n", cases.size(), path);
  return 0;
}

int verify(const char* path) {
  std::FILE* f = std::fopen(path, "rb");
  if (!f) { std::printf("[FAIL] cannot read %s\n", path); return 1; }
  char magic[4];
  uint32_t version = 0, decimation = 0, count = 0;
  if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "BLGD", 4) != 0 || !getU32(f, version) ||
      version != kVersion || !getU32(f, decimation) || decimation != kDecimation || !getU32(f, count)) {
    std::printf("[FAIL] %s: not a version-%u golden fixture\n", path, kVersion);
    std::fclose(f);
    return 1;
  }
  const auto cases = runBattery();
  bool ok = count == cases.size(), readable = ok;
  if (!ok) std::printf("[FAIL] fixture has %u cases, the battery %zu\n", count, cases.size());
  double worstAll = 0.0;
  for (uint32_t k = 0; readable && k < count; ++k) {
    uint32_t nameLen = 0, frames = 0;
    std::string name;
    if (!getU32(f, nameLen)) { ok = readable = false; break; }
    name.resize(nameLen);
    if (std::fread(&name[0], 1, nameLen, f) != nameLen || !getU32(f, frames)) { ok = readable = false; break; }
    std::vector<float> l(frames), r(frames);
    if (std::fread(l.data(), sizeof(float), frames, f) != frames ||
        std::fread(r.data(), sizeof(float), frames, f) != frames) { ok = readable = false; break; }
    const CaseOut& c = cases[k];
    if (name != c.name || frames != c.l.size()) {
      std::printf("[FAIL] case %u: fixture '%s' (%u frames), battery '%s' (%zu)\n", k, name.c_str(), frames,
                  c.name.c_str(), c.l.size());
      ok = readable = false;
      break;
    }
    double worst = 0.0;
    for (uint32_t i = 0; i < frames; ++i) {
      worst = std::max(worst, static_cast<double>(std::fabs(l[i] - c.l[i])));
      worst = std::max(worst, static_cast<double>(std::fabs(r[i] - c.r[i])));
    }
    worstAll = std::max(worstAll, worst);
    const bool pass = worst <= kTolerance;
    std::printf("  [%s] %-40s max abs error %.3g\n", pass ? "PASS" : "FAIL", c.name.c_str(), worst);
    ok &= pass;
  }
  std::fclose(f);
  std::printf("%s: max abs error %.3g over %u cases (tolerance %.0e)\n", ok ? "PASS" : "FAIL", worstAll, count,
              kTolerance);
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  const char* mode = argc > 1 ? argv[1] : "verify";
  const char* path = argc > 2 ? argv[2] : GOLDEN_FIXTURE_PATH;
  if (std::strcmp(mode, "generate") == 0) return generate(path);
  if (std::strcmp(mode, "verify") == 0) return verify(path);
  std::printf("usage: bloom_golden generate|verify [fixture]\n");
  return 2;
}
