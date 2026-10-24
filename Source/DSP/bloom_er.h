#pragma once

// bloom_er.h — the early-reflection taps: an image-source model of a shoebox
// room (docs/DESIGN.md §4, GATES G3).
//
// computeErTaps() is a pure function, tested directly (GATES R6's stated
// exception). It allocates nothing and takes no lock, because BloomCore calls
// it on the audio thread at a sub-block boundary when Size changes (§4.5):
// fixed arrays, an insertion sort over at most 62 taps, and a square root and
// a division per image.

#include <algorithm>
#include <cmath>

namespace bloom {

constexpr int kMaxErTaps = 62;           // order 3: 6 + 18 + 38 (DESIGN.md §4.2)
constexpr double kSpeedOfSound = 343.0;  // m/s
constexpr double kErBeta = 0.85;         // amplitude reflection coefficient
constexpr double kErEnergy = 0.25;       // Σ gain² per channel, "as designed" (§4.4)

enum class ErMic { Left, Right };

struct ErTaps {
  int rawCount = 0;  // images enumerated, before merging (§4.2's 24 / 62)
  int count = 0;     // taps after merging coincident delays
  int delay[kMaxErTaps] = {};   // samples, ≥ 1, ascending
  float gain[kMaxErTaps] = {};  // signed, normalised
};

// The room of DESIGN.md §4.1, in metres, for a Size (longest dimension).
struct ErRoom {
  double w, d, h;
  double sx, sy, sz;  // source
  double mx, my, mz;  // the mic for one channel
};

inline ErRoom erRoom(double size, ErMic mic) {
  ErRoom r{};
  r.w = size;
  r.d = 0.72 * size;
  r.h = std::min(12.0, std::max(2.4, 0.38 * size));
  r.sx = 0.42 * r.w;
  r.sy = 0.28 * r.d;
  r.sz = 0.40 * r.h;
  const double s = std::min(1.2, std::max(0.3, 0.06 * size));
  r.mx = 0.5 * r.w + (mic == ErMic::Left ? -s : s);
  r.my = 0.68 * r.d;
  r.mz = 0.45 * r.h;
  return r;
}

// The image of source coordinate `s` in a room of length `len` along one
// axis, for image index n: |n| reflections, alternating between the walls at
// 0 and len. n = 1 is the reflection off the far wall (2·len − s), n = −1
// off the near wall (−s).
inline double erImageCoord(int n, double s, double len) {
  return (n % 2 == 0) ? n * len + s : (n + 1) * len - s;
}

// The longest delay any tap can have, in samples, with margin: the ER delay
// line's length (DESIGN.md §4.5).
inline int erMaxDelaySamples(double fs) { return static_cast<int>(std::ceil(0.40 * fs)); }

// Taps for one channel's mic, reflection orders 1 … `order` (2 = Plain,
// 3 = Dense). DESIGN.md §4.2–§4.4, in its order: enumerate, round, merge,
// sign by delay-sorted rank, normalise to Σ gain² = kErEnergy.
inline void computeErTaps(double size, int order, double fs, ErMic mic, ErTaps& out) {
  order = std::min(3, std::max(1, order));
  const ErRoom r = erRoom(size, mic);
  const double r0 = std::sqrt((r.sx - r.mx) * (r.sx - r.mx) + (r.sy - r.my) * (r.sy - r.my) +
                              (r.sz - r.mz) * (r.sz - r.mz));

  // Enumerate and round.
  int n = 0;
  for (int nx = -order; nx <= order; ++nx) {
    for (int ny = -order; ny <= order; ++ny) {
      for (int nz = -order; nz <= order; ++nz) {
        const int o = std::abs(nx) + std::abs(ny) + std::abs(nz);
        if (o < 1 || o > order) continue;
        const double ix = erImageCoord(nx, r.sx, r.w) - r.mx;
        const double iy = erImageCoord(ny, r.sy, r.d) - r.my;
        const double iz = erImageCoord(nz, r.sz, r.h) - r.mz;
        const double dist = std::sqrt(ix * ix + iy * iy + iz * iz);
        const int del = std::max(1, static_cast<int>(std::lround((dist - r0) / kSpeedOfSound * fs)));
        out.delay[n] = del;
        out.gain[n] = static_cast<float>(r0 / dist * std::pow(kErBeta, o));
        ++n;
      }
    }
  }
  out.rawCount = n;

  // Sort by delay (insertion sort: ≤ 62 items, no allocation).
  for (int i = 1; i < n; ++i) {
    const int d = out.delay[i];
    const float g = out.gain[i];
    int j = i - 1;
    while (j >= 0 && out.delay[j] > d) {
      out.delay[j + 1] = out.delay[j];
      out.gain[j + 1] = out.gain[j];
      --j;
    }
    out.delay[j + 1] = d;
    out.gain[j + 1] = g;
  }

  // Merge taps that rounded onto the same sample (their gains add).
  int m = 0;
  for (int i = 0; i < n; ++i) {
    if (m > 0 && out.delay[m - 1] == out.delay[i]) {
      out.gain[m - 1] += out.gain[i];
    } else {
      out.delay[m] = out.delay[i];
      out.gain[m] = out.gain[i];
      ++m;
    }
  }
  out.count = m;

  // Sign by delay-sorted rank, then normalise.
  double energy = 0.0;
  for (int i = 0; i < m; ++i) {
    if (i % 2 == 1) out.gain[i] = -out.gain[i];
    energy += static_cast<double>(out.gain[i]) * static_cast<double>(out.gain[i]);
  }
  const float k = static_cast<float>(std::sqrt(kErEnergy / energy));
  for (int i = 0; i < m; ++i) out.gain[i] *= k;
}

}  // namespace bloom
