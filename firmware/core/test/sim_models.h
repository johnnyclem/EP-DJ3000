// Sensor and radio models shared by the engine tests (test_engine_sim.h) and the end-to-end simulator
// (firmware/core/tools/engine_sim.cpp): a deterministic RNG, a synthetic capacitive touch ring with a
// finger on it, and a lossy broadcast radio between units. Header-only, no dependency on the test
// harness, no clocks: time is always passed in.
#pragma once
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace esim {

using Bytes = std::vector<uint8_t>;

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed = 1) : s(seed ? seed : 1u) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  double uni() { return (static_cast<double>(next()) + 0.5) / 4294967296.0; }
  double sym() { return 2.0 * uni() - 1.0; }
  uint32_t below(uint32_t n) { return next() % n; }
  int range(int lo, int hi) { return lo + static_cast<int>(below(static_cast<uint32_t>(hi - lo + 1))); }
};

// Synthetic ring: a finger is an arc of `widthSectors` sectors with soft edges (same model as the
// sensor tests), readings carry uniform noise and are quantised to counts.
struct RingSim {
  unsigned n;
  double baseline[8];
  double peakPermille = 40.0, widthSectors = 1.5, noisePermille = 3.0;

  explicit RingSim(unsigned sectors = 8) : n(sectors) {
    for (unsigned i = 0; i < 8; ++i) baseline[i] = 30000.0 + 900.0 * i;
  }
  static double wrapped(double d) { return d - std::floor(d + 0.5); }
  double coverage(unsigned sector, double fingerRev) const {
    const double pitch = 1.0 / n, w = widthSectors * 0.5 * pitch, edge = 0.15 * pitch;
    double sum = 0.0;
    for (int k = 0; k < 32; ++k) {
      const double phi = sector * pitch + ((k + 0.5) / 32 - 0.5) * pitch;
      const double d = std::fabs(wrapped(phi - fingerRev));
      double p;
      if (d <= w - edge) p = 1.0;
      else if (d >= w + edge) p = 0.0;
      else {
        const double u = (w + edge - d) / (2.0 * edge);
        p = u * u * (3.0 - 2.0 * u);
      }
      sum += p;
    }
    return sum / 32;
  }
  void read(double fingerRev, double amplitude, Rng& rng, uint16_t* out) const {
    for (unsigned i = 0; i < n; ++i) {
      double rise = noisePermille * rng.sym();
      if (amplitude > 0.0) rise += amplitude * peakPermille * coverage(i, fingerRev);
      out[i] = static_cast<uint16_t>(std::lround(baseline[i] * (1.0 + rise / 1000.0)));
    }
  }
};

// Broadcast radio between decks: per-packet loss, duplication, delay with jitter (so packets also
// reorder). Packets are taken from each deck's Link OutEvents and delivered through onLinkPacket.
struct Air {
  Rng rng;
  double loss = 0.0, dup = 0.0, baseUs = 800.0, jitterUs = 150.0;
  struct Delivery {
    size_t to;
    Bytes bytes;
    uint64_t sentTrueUs;
  };
  std::multimap<uint64_t, Delivery> flight;
  size_t sent = 0, lost = 0;
  bool partitioned = false;

  explicit Air(uint32_t seed) : rng(seed) {}

  void broadcast(size_t from, size_t deckCount, const Bytes& pkt, uint64_t nowTrue) {
    ++sent;
    if (partitioned) return;
    for (size_t to = 0; to < deckCount; ++to) {
      if (to == from) continue;
      const int copies = rng.uni() < dup ? 2 : 1;
      for (int c = 0; c < copies; ++c) {
        if (rng.uni() < loss) {
          ++lost;
          continue;
        }
        const double delay = baseUs - jitterUs * std::log(rng.uni());
        flight.emplace(nowTrue + static_cast<uint64_t>(delay), Delivery{to, pkt, nowTrue});
      }
    }
  }
};

}  // namespace esim
