// NassauBloom preset tests — Gates G0 (G0.3, G0.5) and G7 (docs/GATES.md).
//
// The preset TABLE and the parameter RANGES both live in
// Source/Plugin/nassau_presets.h, the one copy the plugin also reads, so these
// checks are about the same numbers the plugin ships. No SDK link is needed.
//
// G7 measures what each preset claims (DESIGN.md §11), on the preset AS
// SHIPPED: its own damping, filters and pre-delay. Only Mix is moved, to
// 100 %, where an AC says "wet". (The "damping bypassed" convention of the
// GATES header is for checking Decay itself; a preset's damping is part of
// what the preset claims.)

#include "bloom_core.h"
#include "nassau_presets.h"
#include "test_util.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using nassau_presets::ParamArray;

constexpr double kFs = 48000.0;

std::string fmt(const char* f, double a = 0.0, double b = 0.0, double c = 0.0, double d = 0.0) {
  char buf[320];
  std::snprintf(buf, sizeof buf, f, a, b, c, d);
  return buf;
}

const ParamArray& presetValues(const char* name) {
  for (const auto& p : nassau_presets::Presets())
    if (std::strcmp(p.name, name) == 0) return p.values;
  std::printf("no preset named %s\n", name);
  std::abort();
}

ParamArray with(ParamArray v, int index, double value) {
  v[static_cast<size_t>(index)] = value;
  return v;
}

// Run a fresh core with `v` over (inL, inR) in 512-sample blocks.
void run(const ParamArray& v, const std::vector<float>& inL, const std::vector<float>& inR,
         std::vector<float>& outL, std::vector<float>& outR) {
  BloomCore core;
  core.init(static_cast<float>(kFs));
  nassau_presets::ApplyToCore(core, v);
  outL.assign(inL.size(), 0.0f);
  outR.assign(inR.size(), 0.0f);
  for (size_t pos = 0; pos < inL.size(); pos += 512) {
    const int len = static_cast<int>(std::min<size_t>(512, inL.size() - pos));
    core.process(&inL[pos], &inR[pos], &outL[pos], &outR[pos], len);
  }
}

// GATES G7's voice stand-in: Xorshift through one RBJ HPF at 300 Hz and one
// RBJ LPF at 4 kHz, Q 0.7071.
std::vector<float> voiceSignal(double seconds) {
  const auto x = testSignal(TestSignal::Xorshift, kFs, seconds);
  const auto y = rbjFilter(rbjFilter(std::vector<double>(x.begin(), x.end()), true, 300.0, kFs), false, 4000.0, kFs);
  std::vector<float> v(y.size());
  for (size_t i = 0; i < y.size(); ++i) v[i] = static_cast<float>(y[i]);
  return v;
}

std::vector<float> scaledToPeak(std::vector<float> x, double peak) {
  float m = 0.0f;
  for (float s : x) m = std::max(m, std::fabs(s));
  const float g = static_cast<float>(peak / m);
  for (float& s : x) s *= g;
  return x;
}

// The preset's wet impulse response (Mix 100 %), a unit impulse on both
// inputs, long enough for a T30 fit at its Decay.
void wetIr(const ParamArray& v, std::vector<float>& l, std::vector<float>& r) {
  const double decay = v[static_cast<size_t>(nassau_presets::kDecay)];
  const auto imp = testSignal(TestSignal::Impulse, kFs, std::max(3.0, 1.5 * decay + 2.0));
  run(with(v, nassau_presets::kMix, 100.0), imp, imp, l, r);
}

// How far a steady line at `hz` has been smeared: of the energy of x[from, to)
// within hz ± 20 Hz, the part OUTSIDE hz ± 1 Hz, in dB. Hann window,
// zero-padded to at least 8x. (A 2 s Hann window's own main lobe is ± 1 Hz,
// so an unmodulated line reads about −33 dB.)
double lineSpreadDb(const std::vector<float>& x, int from, int to, double hz) {
  const int n = to - from;
  size_t size = 1;
  while (size < static_cast<size_t>(8 * n)) size <<= 1;
  std::vector<std::complex<double>> a(size);
  for (int i = 0; i < n; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / (n - 1));
    a[static_cast<size_t>(i)] = w * x[static_cast<size_t>(from + i)];
  }
  fftInPlace(a);
  double inside = 0.0, all = 0.0;
  for (size_t k = 1; k < size / 2; ++k) {
    const double f = static_cast<double>(k) * kFs / static_cast<double>(size), e = std::norm(a[k]);
    if (std::fabs(f - hz) <= 20.0) all += e;
    if (std::fabs(f - hz) <= 1.0) inside += e;
  }
  return 10.0 * std::log10((all - inside) / all);
}

}  // namespace

int main() {
  using namespace nassau_presets;
  std::cout << "=== NassauBloom Preset Tests (G0, G7) ===\n\n";

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

  // ==========================================================================
  // G7 — the presets do their job
  // ==========================================================================

  // ---- G7.1 -----------------------------------------------------------------
  // Every preset on every one of the three source signals, not only "its
  // own": a superset of the AC. The voice is 6 s of steady noise (the worst
  // case for the tail's build-up); each burst is followed by silence.
  std::cout << "\nGroup: finite, and no output above 0 dBFS for a -6 dBFS input (G7.1)\n";
  {
    const double kPeak = std::pow(10.0, -6.0 / 20.0);
    const struct { const char* name; std::vector<float> x; } inputs[] = {
        {"PluckBurst", scaledToPeak(testSignal(TestSignal::PluckBurst, kFs, 6.0), kPeak)},
        {"SnareBurst", scaledToPeak(testSignal(TestSignal::SnareBurst, kFs, 6.0), kPeak)},
        {"voice", scaledToPeak(voiceSignal(6.0), kPeak)},
    };
    for (const auto& preset : Presets()) {
      bool finite = true;
      double worst = 0.0;
      const char* worstIn = "";
      for (const auto& in : inputs) {
        std::vector<float> l, r;
        run(preset.values, in.x, in.x, l, r);
        for (size_t i = 0; i < l.size(); ++i) {
          finite &= std::isfinite(l[i]) && std::isfinite(r[i]);
          const double m = std::max(std::fabs(l[i]), std::fabs(r[i]));
          if (m > worst) {
            worst = m;
            worstIn = in.name;
          }
        }
      }
      check((std::string(preset.name) + fmt(": finite, peak %.2f dBFS", 20.0 * std::log10(worst)) + " (on " +
             worstIn + "; want <= 0)")
                .c_str(),
            finite && worst <= 1.0);
    }
  }

  // ---- G7.2, G7.3, G7.4: decay times of the wet impulse response -----------
  std::cout << "\nGroup: decay times, T30 by octave band (G7.2, G7.3, G7.4)\n";
  {
    struct Bands {
      double t125[2], t1k[2], t8k[2];
    };
    auto bands = [](const char* name) {
      std::vector<float> l, r;
      wetIr(presetValues(name), l, r);
      Bands b{};
      const std::vector<float>* ch[2] = {&l, &r};
      for (int c = 0; c < 2; ++c) {
        b.t125[c] = t30Band(*ch[c], 125.0, kFs);
        b.t1k[c] = t30Band(*ch[c], 1000.0, kFs);
        b.t8k[c] = t30Band(*ch[c], 8000.0, kFs);
      }
      std::cout << "  [INFO] " << name
                << fmt(": T30 125 Hz %.3f / %.3f s, 1 kHz %.3f / %.3f s, ", b.t125[0], b.t125[1], b.t1k[0], b.t1k[1])
                << fmt("8 kHz %.3f / %.3f s (L / R)\n", b.t8k[0], b.t8k[1]);
      return b;
    };
    const char* lr[2] = {"L", "R"};

    const Bands booth = bands("Vocal Booth");
    const Bands amp = bands("Guitar Amp Ambience");
    for (int c = 0; c < 2; ++c) {
      check((std::string("Vocal Booth ") + lr[c] + fmt(": wet T30 at 1 kHz %.3f s (want < 0.5)", booth.t1k[c])).c_str(),
            booth.t1k[c] < 0.5);
      check((std::string("Guitar Amp Ambience ") + lr[c] + fmt(": wet T30 at 1 kHz %.3f s (want < 0.8)", amp.t1k[c]))
                .c_str(),
            amp.t1k[c] < 0.8);
    }

    const Bands ballad = bands("Vocal Ballad Long");
    for (int c = 0; c < 2; ++c) {
      check((std::string("Vocal Ballad Long ") + lr[c] +
             fmt(": T30 at 1 kHz %.3f s (want 3.8 +- 15 %%: 3.23 ... 4.37)", ballad.t1k[c]))
                .c_str(),
            std::fabs(ballad.t1k[c] / 3.8 - 1.0) <= 0.15);
      const double warm = ballad.t8k[c] / ballad.t1k[c];
      check((std::string("Vocal Ballad Long ") + lr[c] +
             fmt(": T30 8 kHz / 1 kHz %.1f %% (want <= 85, warm; simulated 73 / 74)", 100.0 * warm))
                .c_str(),
            warm <= 0.85);
    }

    const Bands hall = bands("80s Bright Ballad Hall");
    for (int c = 0; c < 2; ++c) {
      const double bright = hall.t8k[c] / hall.t1k[c], mud = hall.t125[c] / hall.t1k[c];
      check((std::string("80s Bright Ballad Hall ") + lr[c] +
             fmt(": T30 8 kHz / 1 kHz %.1f %% (want >= 60, bright)", 100.0 * bright))
                .c_str(),
            bright >= 0.60);
      check((std::string("80s Bright Ballad Hall ") + lr[c] +
             fmt(": T30 125 Hz / 1 kHz %.1f %% (want <= 70, no mud)", 100.0 * mud))
                .c_str(),
            mud <= 0.70);
    }
  }

  // ---- G7.5 -----------------------------------------------------------------
  // Two DIFFERENT input channels: Width 0 must make the wet mono whatever the
  // input, and at Mix 100 % there is no dry to differ.
  std::cout << "\nGroup: 60s Mono Plate is mono, and has no early reflections (G7.5)\n";
  {
    const auto plate = with(presetValues("60s Mono Plate"), kMix, 100.0);
    const auto inL = testSignal(TestSignal::Xorshift, kFs, 3.0, 0xB100Bu);
    const auto inR = testSignal(TestSignal::SnareBurst, kFs, 3.0);
    std::vector<float> l, r;
    run(plate, inL, inR, l, r);
    size_t differ = 0;
    for (size_t i = 0; i < l.size(); ++i) differ += l[i] != r[i];
    const double level = rms(l, 0, static_cast<int>(l.size()));
    check(fmt("Mix 100 %%: L == R bit-exactly (%.0f of %.0f samples differ; output rms %.3g)",
              static_cast<double>(differ), static_cast<double>(l.size()), level)
              .c_str(),
          differ == 0 && level > 0.0);
    run(with(plate, kLateLevel, 0.0), inL, inR, l, r);
    size_t nonzero = 0;
    for (size_t i = 0; i < l.size(); ++i) nonzero += (l[i] != 0.0f) + (r[i] != 0.0f);
    check(fmt("Mix 100 %%, Late Level 0: every output sample == 0.0f (%.0f are not)", static_cast<double>(nonzero))
              .c_str(),
          nonzero == 0);
  }

  // ---- G7.6 -----------------------------------------------------------------
  // The dispersion cascade alone (GATES R6's stated exception, as G5.1), with
  // the preset's Diffusion, for BOTH springs (R's chirp is 0.02 lower,
  // DESIGN.md §6.1) and at four rates, since K = round(fs / 9000) changes.
  std::cout << "\nGroup: Spring Tank's dispersion (G7.6)\n";
  {
    const double diffusion = presetValues("Spring Tank")[static_cast<size_t>(kDiffusion)] / 100.0;
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
      for (int c = 0; c < 2; ++c) {
        bloom::SpringDispersion disp;
        disp.init(fs);
        disp.setChirp(bloom::springChirp(diffusion) - (c == 1 ? 0.02 : 0.0));
        std::vector<float> h(static_cast<size_t>(1) << (fs > 100000.0 ? 17 : 16));
        for (size_t i = 0; i < h.size(); ++i) h[i] = disp.process(i == 0 ? 1.0f : 0.0f);
        bool monotonic = true;
        double prev = -1e9;
        int points = 0;
        for (int k = 0;; ++k) {
          const double f = std::min(4500.0, 200.0 * std::pow(2.0, k / 6.0));
          const double tau = groupDelaySamples(h, f, fs);
          monotonic &= tau > prev;
          prev = tau;
          ++points;
          if (f >= 4500.0) break;
        }
        const double spread = (groupDelaySamples(h, 4000.0, fs) - groupDelaySamples(h, 200.0, fs)) / fs;
        check((fmt("%.1f kHz, ", fs / 1000.0) + (c == 0 ? "L" : "R") +
               fmt(" (K %.0f, a %.4f): group delay monotonic over %.0f points, 4 kHz - 200 Hz %.2f ms (want >= 12)",
                   disp.k, disp.a, points, 1000.0 * spread))
                  .c_str(),
              monotonic && spread >= 0.012);
      }
    }
  }

  // ---- G7.7 -----------------------------------------------------------------
  // The wet's contribution is the preset's output minus the same preset with
  // both reverb paths at 0 (ER Level 0, Late Level 0), which is the dry alone
  // at the preset's Mix.
  std::cout << "\nGroup: Pop Drum Verb keeps the hit dry and in front (G7.7)\n";
  {
    const auto& drum = presetValues("Pop Drum Verb");
    const auto x = testSignal(TestSignal::SnareBurst, kFs, 1.0);
    std::vector<float> l, r, dl, dr;
    run(drum, x, x, l, r);
    run(with(with(drum, kErLevel, 0.0), kLateLevel, 0.0), x, x, dl, dr);
    const size_t n11 = static_cast<size_t>(0.011 * kFs);
    double eWet = 0.0, eDry = 0.0;
    for (size_t i = 0; i < n11; ++i) {
      const double wl = static_cast<double>(l[i]) - dl[i], wr = static_cast<double>(r[i]) - dr[i];
      eWet += wl * wl + wr * wr;
      eDry += static_cast<double>(dl[i]) * dl[i] + static_cast<double>(dr[i]) * dr[i];
    }
    const double db = eWet > 0.0 ? 10.0 * std::log10(eWet / eDry) : -999.0;
    // Where the wet does start: a check that there is one at all.
    int first = -1;
    for (size_t i = 0; i < l.size() && first < 0; ++i)
      if (std::fabs(l[i] - dl[i]) > 1e-6f || std::fabs(r[i] - dr[i]) > 1e-6f) first = static_cast<int>(i);
    check(fmt("wet in 0-11 ms %.1f dB re the dry (want < -40; -999 = exactly none); the wet starts at %.2f ms", db,
              1000.0 * first / kFs)
              .c_str(),
          db < -40.0 && first > 0);
  }

  // ---- G7.8 -----------------------------------------------------------------
  // Amended in G7 (as written, the AC asked for the wet's spectral PEAK on
  // PluckBurst at 220 ± 1 Hz, which cannot measure detuning: see GATES). A
  // delay-line LFO does not move a line, it spreads it, so this measures the
  // spread: a steady 220 Hz sine, the wet over 1–3 s (past the build-up).
  // Modulation 0 and 100 % are printed beside it, for the measure's range.
  std::cout << "\nGroup: Piano on Stage does not detune (G7.8)\n";
  {
    const auto piano = with(presetValues("Piano on Stage"), kMix, 100.0);
    std::vector<float> x(static_cast<size_t>(3.0 * kFs));
    for (size_t i = 0; i < x.size(); ++i)
      x[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * 220.0 * static_cast<double>(i) / kFs));
    const int sec = static_cast<int>(kFs);
    auto spread = [&](const ParamArray& v, double out[2]) {
      std::vector<float> l, r;
      run(v, x, x, l, r);
      out[0] = lineSpreadDb(l, sec, 3 * sec, 220.0);
      out[1] = lineSpreadDb(r, sec, 3 * sec, 220.0);
    };
    double ref0[2], ref100[2], s[2];
    spread(with(piano, kModulation, 0.0), ref0);
    spread(with(piano, kModulation, 100.0), ref100);
    spread(piano, s);
    std::cout << "  [INFO] the same at Modulation 0: " << fmt("%.1f / %.1f dB; at 100 %%: %.1f / %.1f dB (L / R)\n",
                                                               ref0[0], ref0[1], ref100[0], ref100[1]);
    for (int c = 0; c < 2; ++c)
      check((std::string(c == 0 ? "L" : "R") +
             fmt(": a 220 Hz sine's wet energy outside 220 +- 1 Hz (of +- 20 Hz) %.1f dB (want < -20)", s[c]))
                .c_str(),
            s[c] < -20.0);
  }

  return testSummary();
}
