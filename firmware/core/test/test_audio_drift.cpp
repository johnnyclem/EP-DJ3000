// DriftCompensator: exact slip arithmetic, hysteresis and the 0.1% cap, then ten minutes of a host that
// ignores the feedback endpoint against an I2S clock that is 150 / 300 ppm off.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "epdj/audio_rate.h"
#include "test_harness.h"

using namespace epdj::audio;

namespace {

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  double uni() { return (static_cast<double>(next()) + 0.5) / 4294967296.0; }
  double sym() { return 2.0 * uni() - 1.0; }
};

constexpr size_t kCap = 2048;
constexpr double kPi = 3.14159265358979323846;

// Frames with distinct, small values so averages are exact: L = base + 10 i, R = -L.
void fill(SpscAudioRing& r, size_t n, int base, size_t first = 0) {
  for (size_t i = 0; i < n; ++i) {
    const int16_t l = static_cast<int16_t>(base + 10 * static_cast<int>(first + i));
    const int16_t f[2] = {l, static_cast<int16_t>(-l)};
    r.write(f, 1);
  }
}

}  // namespace

EPDJ_TEST(drift_passes_audio_through_untouched_inside_the_band) {
  StaticSpscAudioRing<kCap> ring;
  DriftCompensator comp;
  fill(ring, kCap / 2 + 255, 0);  // just inside the engage distance (256)
  int16_t out[2 * 100];
  size_t total = 0;
  for (int i = 0; i < 5; ++i) {
    CHECK_EQ(comp.render(ring, out, 100), 100);
    for (size_t k = 0; k < 100; ++k) {
      CHECK_EQ(out[2 * k], (total + k) * 10);
      CHECK_EQ(out[2 * k + 1], -static_cast<int>((total + k) * 10));
    }
    total += 100;
  }
  CHECK_EQ(comp.droppedFrames(), 0);
  CHECK_EQ(comp.duplicatedFrames(), 0);
}

EPDJ_TEST(drift_drops_a_frame_by_averaging_two_when_far_too_full) {
  StaticSpscAudioRing<kCap> ring;
  DriftCompensator comp;
  fill(ring, kCap / 2 + 300, 100);
  int16_t out[2 * 4];
  CHECK_EQ(comp.render(ring, out, 4), 4);
  // in0 = 100, in1 = 110 -> one frame of 105; then 120, 130, 140 untouched
  CHECK_EQ(out[0], 105);
  CHECK_EQ(out[1], -105);
  CHECK_EQ(out[2], 120);
  CHECK_EQ(out[4], 130);
  CHECK_EQ(out[6], 140);
  CHECK_EQ(out[7], -140);
  CHECK_EQ(comp.droppedFrames(), 1);
  CHECK_EQ(ring.fillFrames(), kCap / 2 + 300 - 5);  // 4 outputs consumed 5 inputs
}

EPDJ_TEST(drift_inserts_an_interpolated_frame_when_far_too_empty) {
  StaticSpscAudioRing<kCap> ring;
  DriftCompensator comp;
  fill(ring, kCap / 2 - 300, 100);
  int16_t out[2 * 4];
  CHECK_EQ(comp.render(ring, out, 4), 4);
  // previous output (silence at start) = 0, next input = 100 -> 50, then 100 itself, then 110, 120
  CHECK_EQ(out[0], 50);
  CHECK_EQ(out[1], -50);
  CHECK_EQ(out[2], 100);
  CHECK_EQ(out[4], 110);
  CHECK_EQ(out[6], 120);
  CHECK_EQ(comp.duplicatedFrames(), 1);
  CHECK_EQ(ring.fillFrames(), kCap / 2 - 300 - 3);  // 4 outputs from 3 inputs

  // A duplicate needs room for two output frames; with one left it waits for the next call.
  StaticSpscAudioRing<kCap> r2;
  DriftCompensator c2;
  fill(r2, kCap / 2 - 300, 100);
  int16_t one[2];
  CHECK_EQ(c2.render(r2, one, 1), 1);
  CHECK_EQ(one[0], 100);
  CHECK_EQ(c2.duplicatedFrames(), 0);
}

EPDJ_TEST(drift_slips_at_most_once_per_1000_frames_and_stops_near_the_target) {
  StaticSpscAudioRing<kCap> ring;
  DriftCompensator comp;
  fill(ring, kCap / 2 + 300, 0);  // too full by more than the engage distance (256)
  std::vector<int16_t> out(2 * 100);
  uint32_t slips = 0, lastTotal = 0;
  size_t frames = 0, lastSlipAt = 0;
  bool first = true;
  size_t lastSlipFrames = 0;
  for (int i = 0; i < 4000; ++i) {  // 400000 frames; the producer keeps up exactly (100 frames per render)
    comp.render(ring, out.data(), 100);
    if (comp.droppedFrames() != lastTotal) {
      if (!first) CHECK(frames - lastSlipAt >= 1000 - 100);  // render granularity is 100
      first = false;
      lastSlipAt = frames;
      lastSlipFrames = frames;
      slips += comp.droppedFrames() - lastTotal;
      lastTotal = comp.droppedFrames();
    }
    frames += 100;
    const int16_t zeros[2 * 100] = {};
    ring.write(zeros, 100);
  }
  CHECK(static_cast<double>(slips) <= static_cast<double>(frames) / 1000.0 + 1.0);  // <= 0.1%
  CHECK(slips >= 250 && slips <= 300);  // 300 -> 32 frames from the target, one frame per slip
  // It stopped once the fill came within the release distance of half full, and stays there.
  CHECK(ring.fillFrames() >= kCap / 2 + 28 && ring.fillFrames() <= kCap / 2 + 36);
  CHECK(lastSlipFrames < 330000);
  CHECK_EQ(comp.duplicatedFrames(), 0);
}

EPDJ_TEST(drift_underrun_is_zero_filled_and_reported) {
  StaticSpscAudioRing<kCap> ring;
  DriftCompensator comp;
  std::vector<int16_t> out(2 * 64, 7);
  CHECK_EQ(comp.render(ring, out.data(), 64), 0);
  for (int16_t v : out) CHECK_EQ(v, 0);
  CHECK_EQ(ring.underrunFrames(), 64);
  // A nearly empty ring is "too empty": one interpolated frame is inserted in front (silence -> 1000 = 500).
  fill(ring, 10, 1000);
  std::fill(out.begin(), out.end(), static_cast<int16_t>(7));
  CHECK_EQ(comp.render(ring, out.data(), 64), 11);
  CHECK_EQ(out[0], 500);
  CHECK_EQ(out[2], 1000);
  CHECK_EQ(out[2 * 10], 1090);
  CHECK_EQ(out[2 * 11], 0);
  CHECK_EQ(out[2 * 63 + 1], 0);
}

namespace {

struct DriftResult {
  size_t minFill = kCap, maxFill = 0;
  uint32_t under = 0, over = 0, dropped = 0, duplicated = 0;
  double maxStep = 0.0;  // largest sample-to-sample change in the output
  uint64_t outFrames = 0;
  size_t closestSlipsChunks = 1000000;
};

// A host that sends 48 frames every USB frame of a 1 kHz sine (48 samples per period), no feedback, and an
// I2S consumer that pulls 64 frames per DMA block through the compensator.
DriftResult runDrift(double hostPpm, double devicePpm, double seconds, uint32_t seed) {
  StaticSpscAudioRing<kCap> ring;
  DriftCompensator comp;
  Rng rng(seed);
  DriftResult r;
  int16_t wave[48];
  for (int i = 0; i < 48; ++i) wave[i] = static_cast<int16_t>(std::lround(16000.0 * std::sin(2.0 * kPi * i / 48.0)));
  uint64_t hostIndex = 0;
  auto send = [&] {
    int16_t pkt[2 * 48];
    for (int i = 0; i < 48; ++i) {
      pkt[2 * i] = pkt[2 * i + 1] = wave[(hostIndex + static_cast<uint64_t>(i)) % 48];
    }
    hostIndex += 48;
    ring.write(pkt, 48);
  };
  for (size_t i = 0; i < kCap / 2 / 48; ++i) send();

  const double framePeriod = 1e-3 / (1.0 + hostPpm * 1e-6);
  const double chunkPeriod = 64.0 / (48000.0 * (1.0 + devicePpm * 1e-6));
  double nextChunk = chunkPeriod;
  const uint64_t frames = static_cast<uint64_t>(seconds / framePeriod);
  int16_t out[2 * 64];
  double prev = 0.0;
  bool havePrev = false;
  uint64_t chunkNo = 0, lastSlipChunk = 0;
  bool haveSlip = false;
  for (uint64_t k = 0; k < frames; ++k) {
    const double t = static_cast<double>(k) * framePeriod + 200e-6 * rng.sym() + 1e-3;
    while (nextChunk <= t) {
      const uint32_t slipsBefore = comp.droppedFrames() + comp.duplicatedFrames();
      comp.render(ring, out, 64);
      ++chunkNo;
      if (comp.droppedFrames() + comp.duplicatedFrames() != slipsBefore) {
        if (haveSlip && chunkNo - lastSlipChunk < r.closestSlipsChunks) r.closestSlipsChunks = chunkNo - lastSlipChunk;
        lastSlipChunk = chunkNo;
        haveSlip = true;
      }
      for (int i = 0; i < 64; ++i) {
        const double v = out[2 * i];
        if (havePrev && std::fabs(v - prev) > r.maxStep) r.maxStep = std::fabs(v - prev);
        prev = v;
        havePrev = true;
      }
      r.outFrames += 64;
      nextChunk += chunkPeriod;
      const size_t f = ring.fillFrames();
      if (f < r.minFill) r.minFill = f;
      if (f > r.maxFill) r.maxFill = f;
    }
    send();
    const size_t f = ring.fillFrames();
    if (f < r.minFill) r.minFill = f;
    if (f > r.maxFill) r.maxFill = f;
  }
  r.under = ring.underrunFrames();
  r.over = ring.overrunFrames();
  r.dropped = comp.droppedFrames();
  r.duplicated = comp.duplicatedFrames();
  return r;
}

}  // namespace

EPDJ_TEST(drift_ten_minutes_without_feedback_stays_in_range_and_below_0_1_percent) {
  struct Case {
    double host, dev;
  } cases[] = {{0, +150}, {0, -150}, {+150, -150}, {-150, +150}};
  uint32_t seed = 1;
  // 1 kHz at amplitude 16000: the largest legitimate sample-to-sample step
  const double normalMaxStep = 16000.0 * 2.0 * kPi / 48.0;
  for (const Case& c : cases) {
    const DriftResult r = runDrift(c.host, c.dev, 600.0, seed++);
    const double slipRate = static_cast<double>(r.dropped + r.duplicated) / static_cast<double>(r.outFrames);
    std::printf("    (host %+4.0f dev %+4.0f ppm: fill %zu..%zu, %u dropped %u duplicated = %.4f%% of frames, max step %.0f vs %.0f)\n",
                c.host, c.dev, r.minFill, r.maxFill, r.dropped, r.duplicated, slipRate * 100.0, r.maxStep, normalMaxStep);
    CHECK_EQ(r.under, 0);
    CHECK_EQ(r.over, 0);
    CHECK(r.minFill >= kCap / 5);
    CHECK(r.maxFill <= kCap * 4 / 5);
    CHECK(slipRate <= 0.001);
    CHECK(r.dropped + r.duplicated > 1000);  // it really had work to do
    CHECK(r.closestSlipsChunks >= 15);       // 15 chunks x 64 = 960 + a partial chunk: spacing honoured
    // A slip replaces two samples by their mean (or adds one): at most 1.5x the steepest legitimate step.
    CHECK(r.maxStep <= 1.55 * normalMaxStep);
    // the right direction was chosen
    if (c.dev > c.host) {  // I2S consumes faster than the host delivers: the ring drains, frames are inserted
      CHECK(r.duplicated > r.dropped * 10);
    } else {
      CHECK(r.dropped > r.duplicated * 10);
    }
  }
}

EPDJ_TEST(drift_with_matched_clocks_never_slips) {
  const DriftResult r = runDrift(0.0, 0.0, 120.0, 5);
  CHECK_EQ(r.under, 0);
  CHECK_EQ(r.over, 0);
  CHECK_EQ(r.dropped, 0);
  CHECK_EQ(r.duplicated, 0);
}
