// FeedbackController: 10.14 encoding, control-law behaviour, and ten-minute simulations of a USB host whose
// frame clock differs from the I2S clock, with USB frame jitter, a DMA-block consumer and a host that only
// looks at the feedback endpoint every 8 ms.
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

struct SimParams {
  double hostPpm = 0.0;     // USB frame clock error
  double devicePpm = 0.0;   // I2S clock error
  uint32_t seed = 1;
  double seconds = 600.0;
  double jitterUs = 200.0;  // +/- uniform, on when the device sees each SOF / packet
  bool useFeedback = true;  // false: the host ignores the endpoint and sends nominal
  size_t prefill = kCap / 2;
  double stallAtS = -1.0;   // the host sends nothing for stallMs starting here
  double stallMs = 0.0;
  size_t dmaChunk = 64;     // the I2S consumer takes frames in blocks
};

struct SimResult {
  size_t minFill = kCap, maxFill = 0;
  uint32_t under = 0, over = 0;
  double meanFb = 0.0;         // mean feedback (samples/frame) over the last 60 s, as the host decodes it
  double expectedFb = 0.0;     // what a perfect controller would send
  double meanFillTail = 0.0;   // mean fill over the last 60 s
  double maxAbsFillTail = 0.0; // max |fill - target| over the last 60 s
  double driftPpmEst = 0.0;
};

SimResult simulate(const SimParams& p) {
  StaticSpscAudioRing<kCap> ring;
  FeedbackController::Config cc;
  cc.capacityFrames = kCap;
  FeedbackController ctrl(cc);
  Rng rng(p.seed);
  SimResult r;

  // prefill
  std::vector<int16_t> silence(2 * 64, 0);
  for (size_t left = p.prefill; left > 0;) {
    const size_t n = left < 64 ? left : 64;
    ring.write(silence.data(), n);
    left -= n;
  }

  const double hostRate = 1.0 + p.hostPpm * 1e-6;
  const double devRate = 1.0 + p.devicePpm * 1e-6;
  r.expectedFb = 48.0 * devRate / hostRate;
  const double framePeriod = 1e-3 / hostRate;          // seconds of true time per USB frame
  const double chunkPeriod = static_cast<double>(p.dmaChunk) / (48000.0 * devRate);

  double hostFb = 48.0;  // what the host currently believes
  double acc = 0.0;
  uint8_t usbBytes[3] = {0, 0, 0};
  FeedbackController::encodeUsb(ctrl.valueQ16(), usbBytes);
  double nextChunk = chunkPeriod;
  std::vector<int16_t> packet(2 * 64, 0), sink(2 * 256, 0);
  const uint64_t frames = static_cast<uint64_t>(p.seconds / framePeriod);
  const uint64_t tailStart = frames > 60000 ? frames - 60000 : 0;
  double fbSum = 0.0, fillSum = 0.0;
  uint64_t tailN = 0;
  auto observe = [&] {
    const size_t f = ring.fillFrames();
    if (f < r.minFill) r.minFill = f;
    if (f > r.maxFill) r.maxFill = f;
  };

  for (uint64_t k = 0; k < frames; ++k) {
    const double t = static_cast<double>(k) * framePeriod + p.jitterUs * 1e-6 * rng.sym() + 1e-3;
    while (nextChunk <= t) {  // the I2S DMA refills before this SOF
      ring.read(sink.data(), p.dmaChunk);
      nextChunk += chunkPeriod;
      observe();
    }
    const bool stalled = p.stallAtS >= 0.0 && t >= p.stallAtS && t < p.stallAtS + p.stallMs * 1e-3;
    if (!stalled) {
      if (k % 8 == 0 && p.useFeedback) hostFb = static_cast<double>(FeedbackController::decodeUsb(usbBytes)) / 65536.0;
      acc += hostFb;
      const size_t n = static_cast<size_t>(acc);
      acc -= static_cast<double>(n);
      ring.write(packet.data(), n);
    }
    observe();
    ctrl.update(static_cast<uint32_t>(ring.fillFrames()));
    FeedbackController::encodeUsb(ctrl.valueQ16(), usbBytes);
    if (k >= tailStart) {
      fbSum += static_cast<double>(FeedbackController::decodeUsb(usbBytes)) / 65536.0;
      const double f = static_cast<double>(ring.fillFrames());
      fillSum += f;
      const double dev = std::fabs(f - kCap / 2.0);
      if (dev > r.maxAbsFillTail) r.maxAbsFillTail = dev;
      ++tailN;
    }
  }
  r.under = ring.underrunFrames();
  r.over = ring.overrunFrames();
  r.meanFb = fbSum / static_cast<double>(tailN);
  r.meanFillTail = fillSum / static_cast<double>(tailN);
  r.driftPpmEst = ctrl.driftPpm();
  return r;
}

}  // namespace

EPDJ_TEST(feedback_usb_10_14_encoding) {
  uint8_t b[3];
  FeedbackController::encodeUsb(48u << 16, b);  // 48.0 samples/frame = 48 << 14 = 0x0C0000
  CHECK_EQ(b[0], 0x00);
  CHECK_EQ(b[1], 0x00);
  CHECK_EQ(b[2], 0x0C);
  FeedbackController::encodeUsb((48u << 16) + (1u << 15), b);  // 48.5 = 0x0C2000
  CHECK_EQ(b[0], 0x00);
  CHECK_EQ(b[1], 0x20);
  CHECK_EQ(b[2], 0x0C);
  FeedbackController::encodeUsb(static_cast<uint32_t>(0x0B0666) << 2, b);  // 44.1 samples/frame = 0x0B0666 / 2^14
  CHECK_EQ(b[0], 0x66);
  CHECK_EQ(b[1], 0x06);
  CHECK_EQ(b[2], 0x0B);
  CHECK_EQ(FeedbackController::decodeUsb(b), static_cast<uint32_t>(0x0B0666) << 2);

  // Round trip loses only the two lowest Q16.16 bits.
  Rng r(3);
  for (int i = 0; i < 2000; ++i) {
    const uint32_t q = static_cast<uint32_t>(40.0 * 65536.0 + r.uni() * 20.0 * 65536.0);
    FeedbackController::encodeUsb(q, b);
    const uint32_t back = FeedbackController::decodeUsb(b);
    CHECK(back + 2 >= q && back <= q + 2);
  }
  // Rounding, not truncation: 0.5 LSB of 10.14 rounds up.
  CHECK_EQ(FeedbackController::toUsb10_14(1), 0);
  CHECK_EQ(FeedbackController::toUsb10_14(2), 1);
  CHECK_EQ(FeedbackController::toUsb10_14(5), 1);
  CHECK_EQ(FeedbackController::toUsb10_14(6), 2);
  // 24-bit saturation instead of wrapping into a tiny value
  CHECK_EQ(FeedbackController::toUsb10_14(0xFFFFFFFFu), 0xFFFFFF);
  FeedbackController::encodeUsb(0xFFFFFFFFu, b);
  CHECK(b[0] == 0xFF && b[1] == 0xFF && b[2] == 0xFF);
}

EPDJ_TEST(feedback_starts_at_nominal_and_stays_there_when_balanced) {
  FeedbackController c;
  CHECK_EQ(c.nominalQ16(), 48u << 16);
  CHECK_EQ(c.valueQ16(), 48u << 16);
  CHECK_EQ(c.minQ16(), 3114271);  // -1%
  CHECK_EQ(c.maxQ16(), 3177185);  // +1%
  for (int i = 0; i < 100000; ++i) c.update(kCap / 2);
  CHECK_EQ(c.valueQ16(), 48u << 16);
  CHECK_NEAR(c.driftPpm(), 0.0, 0.01);
  FeedbackController c44(FeedbackController::Config{44100, 2048, 6.3e-4f, 9.9e-8f, 131});
  CHECK_EQ(c44.nominalQ16(), 2890138);  // 44.1 x 65536 rounded
}

EPDJ_TEST(feedback_direction_bounds_and_rate_limit) {
  FeedbackController c;
  // Ring too full: ask for fewer samples; too empty: more.
  FeedbackController hi, lo;
  for (int i = 0; i < 2000; ++i) {
    hi.update(1800);
    lo.update(300);
  }
  CHECK(hi.valueQ16() < hi.nominalQ16());
  CHECK(lo.valueQ16() > lo.nominalQ16());
  // However wrong the fill is, the request stays within +/-1%.
  for (int i = 0; i < 200000; ++i) {
    hi.update(2048);
    lo.update(0);
    CHECK(hi.valueQ16() >= hi.minQ16() && hi.valueQ16() <= hi.maxQ16());
    CHECK(lo.valueQ16() >= lo.minQ16() && lo.valueQ16() <= lo.maxQ16());
  }
  CHECK_EQ(hi.valueQ16(), hi.minQ16());
  CHECK_EQ(lo.valueQ16(), lo.maxQ16());
  // The output never moves by more than maxStepQ16 per update, whatever the fill does.
  FeedbackController r;
  Rng rng(1);
  uint32_t prev = r.valueQ16();
  for (int i = 0; i < 50000; ++i) {
    const uint32_t v = r.update(static_cast<uint32_t>(rng.uni() * 2048));
    const int64_t d = static_cast<int64_t>(v) - static_cast<int64_t>(prev);
    CHECK(d <= 131 && d >= -131);
    prev = v;
  }
  // reset() forgets the integrator
  hi.reset();
  CHECK_EQ(hi.valueQ16(), hi.nominalQ16());
  CHECK_NEAR(hi.driftPpm(), 0.0, 1e-9);
  (void)c;
}

EPDJ_TEST(feedback_ten_minutes_never_underruns_and_converges_for_every_clock_error) {
  struct Case {
    double host, dev;
  } cases[] = {{0, +150}, {0, -150}, {+150, 0}, {-150, 0}, {+150, -150}, {-150, +150}, {0, 0}};
  uint32_t seed = 1;
  for (const Case& c : cases) {
    SimParams p;
    p.hostPpm = c.host;
    p.devicePpm = c.dev;
    p.seed = seed++;
    const SimResult r = simulate(p);
    const double expectedPpm = (r.expectedFb / 48.0 - 1.0) * 1e6;
    const double gotPpm = (r.meanFb / 48.0 - 1.0) * 1e6;
    std::printf("    (host %+4.0f dev %+4.0f ppm: fill %zu..%zu of %zu, feedback %+.1f ppm vs ideal %+.1f, tail |fill-target| <= %.0f)\n",
                c.host, c.dev, r.minFill, r.maxFill, kCap, gotPpm, expectedPpm, r.maxAbsFillTail);
    CHECK_EQ(r.under, 0);
    CHECK_EQ(r.over, 0);
    CHECK(r.minFill >= kCap / 5);       // never below 20%
    CHECK(r.maxFill <= kCap * 4 / 5);   // never above 80%
    CHECK_NEAR(gotPpm, expectedPpm, 5.0);  // converged on the true rate ratio
    CHECK(r.maxAbsFillTail < 100.0);       // and sits near half full: jitter and DMA blocks only
    CHECK(std::fabs(r.meanFillTail - kCap / 2.0) < 15.0);
  }
}

EPDJ_TEST(feedback_recovers_from_a_bad_start_and_from_a_host_stall) {
  // Streaming starts with the ring 30% full (platform started the DMA too early).
  SimParams a;
  a.hostPpm = 0;
  a.devicePpm = 100;
  a.prefill = kCap * 3 / 10;
  a.seconds = 120;
  SimResult r = simulate(a);
  CHECK_EQ(r.under, 0);
  CHECK(r.minFill >= kCap / 5);
  CHECK(r.maxAbsFillTail < 100.0);

  // ... or 70% full.
  a.prefill = kCap * 7 / 10;
  r = simulate(a);
  CHECK_EQ(r.over, 0);
  CHECK(r.maxFill <= kCap * 4 / 5);
  CHECK(r.maxAbsFillTail < 100.0);

  // The host's scheduler hiccups: 8 ms with no packets (384 frames drained) in the middle of playback.
  SimParams s;
  s.devicePpm = -120;
  s.seconds = 180;
  s.stallAtS = 90.0;
  s.stallMs = 8.0;
  r = simulate(s);
  CHECK_EQ(r.under, 0);
  CHECK_EQ(r.over, 0);
  CHECK(r.minFill >= kCap / 5);
  CHECK(r.maxAbsFillTail < 100.0);
  CHECK_NEAR((r.meanFb / 48.0 - 1.0) * 1e6, (r.expectedFb / 48.0 - 1.0) * 1e6, 5.0);
}

// The premise of the whole exercise: without feedback the same clocks DO fail within minutes.
EPDJ_TEST(without_feedback_the_same_clocks_over_or_underrun) {
  SimParams p;
  p.devicePpm = +150;
  p.useFeedback = false;
  p.seconds = 300;
  SimResult r = simulate(p);
  CHECK(r.under > 0);  // device clock fast: the ring drains
  p.devicePpm = -150;
  r = simulate(p);
  CHECK(r.over > 0);  // device clock slow: the ring fills up
}
