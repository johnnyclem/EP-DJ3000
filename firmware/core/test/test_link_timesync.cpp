// TimeSync against a simulated radio: asymmetric exponential jitter, multi-millisecond retry spikes,
// two free-running crystals (+/-100 ppm each), arbitrary and wrapping 32-bit clock origins.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "epdj/link.h"
#include "test_harness.h"

using namespace epdj::link;

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
  double expo(double mean) { return -mean * std::log(uni()); }
};

// A free-running microsecond counter: reads true time T (seconds) through its own origin and rate.
struct Clock {
  double origin;  // counter value at T = 0, microseconds (may exceed 2^32: it wraps)
  double rate;    // 1 + ppm * 1e-6
  uint32_t read(double t) const {
    return static_cast<uint32_t>(static_cast<uint64_t>(std::floor(origin + t * 1e6 * rate)));
  }
};

struct Radio {
  double baseUs = 800.0;      // fixed one-way delay, equal both ways (unobservable by any algorithm)
  double jitterFwdUs = 150.0; // mean of the exponential extra delay requester -> responder
  double jitterRevUs = 400.0; // ... responder -> requester: the asymmetry
  double spikeProb = 0.05;    // a retry: 1..6 ms extra, in one direction
  double processUs = 30.0;    // responder holds the request this long
};

struct Session {
  Clock local, peer;
  Radio radio;
  Rng rng;
  TimeSync sync;
  double t = 0.0;  // true time of the last exchange start
  size_t exchanges = 0;

  Session(Clock l, Clock p, uint32_t seed) : local(l), peer(p), rng(seed) {}

  // One request/response; returns the true time at which the response arrived.
  double exchange() {
    double d1 = radio.baseUs + rng.expo(radio.jitterFwdUs);
    double d2 = radio.baseUs + rng.expo(radio.jitterRevUs);
    if (rng.uni() < radio.spikeProb) {
      const double spike = 1000.0 + rng.uni() * 5000.0;
      (rng.uni() < 0.5 ? d1 : d2) += spike;
    }
    const double t1 = t, t2 = t1 + d1 * 1e-6, t3 = t2 + radio.processUs * 1e-6, t4 = t3 + d2 * 1e-6;
    sync.addExchange(local.read(t1), peer.read(t2), peer.read(t3), local.read(t4));
    ++exchanges;
    return t4;
  }
  // The pacing Link uses: 100 ms for the first 32 exchanges, then 250 ms.
  double nextStart() const { return t + (exchanges < 32 ? 0.1 : 0.25); }

  // Estimated minus true peer clock at true time tt, microseconds.
  double error(double tt) const {
    return static_cast<double>(static_cast<int32_t>(sync.peerTime(local.read(tt)) - peer.read(tt)));
  }
};

constexpr double kTwo32 = 4294967296.0;

}  // namespace

EPDJ_TEST(timesync_converges_under_asymmetric_jitter_and_skew) {
  const double skews[5][2] = {{-100, 100}, {100, -100}, {100, 100}, {0, 0}, {-100, -100}};  // ppm: local, peer
  const double peerOffsets[3] = {0.0, kTwo32 - 5e5, 1.08e10};  // equal, about to wrap, 3 hours ahead
  double worst = 0.0, worstConverge = 0.0;
  int sessions = 0;
  for (const auto& sk : skews) {
    for (double off : peerOffsets) {
      for (uint32_t seed = 1; seed <= 4; ++seed) {
        // The local counter wraps 20 s into the run; the peer's origin is arbitrary.
        const Clock local{kTwo32 - 20e6, 1.0 + sk[0] * 1e-6};
        const Clock peer{kTwo32 - 20e6 + off, 1.0 + sk[1] * 1e-6};
        Session s(local, peer, seed * 7919u + static_cast<uint32_t>(sessions));
        double lastBad = 0.0, maxAfterWarmup = 0.0;
        while (s.t < 120.0) {
          const double tEnd = s.exchange();
          const double next = s.nextStart();
          for (double tt = tEnd; tt < next; tt += 0.0125) {
            if (!s.sync.valid()) continue;
            const double e = std::fabs(s.error(tt));
            if (e > 200.0) lastBad = tt;
            if (tt > 10.0 && e > maxAfterWarmup) maxAfterWarmup = e;
          }
          s.t = next;
        }
        if (maxAfterWarmup > worst) worst = maxAfterWarmup;
        if (lastBad > worstConverge) worstConverge = lastBad;
        CHECK(s.sync.locked());
        // The skew estimate is within 15 ppm of the truth (relative skew of the two crystals).
        CHECK_NEAR(s.sync.skewPpm(), sk[1] - sk[0], 15.0);
        ++sessions;
      }
    }
  }
  std::printf("    (%d sessions: worst error after 10 s %.0f us, last error >200 us at %.1f s)\n", sessions, worst,
              worstConverge);
  CHECK(worst < 200.0);
  CHECK(worstConverge < 10.0);  // locked within 10 s of the first exchange
}

namespace {
// Worst |error| after 20 s over four seeds, 30 ppm / -30 ppm crystals, for a given radio.
double worstError(const Radio& radio) {
  double worst = 0.0;
  for (uint32_t seed = 1; seed <= 4; ++seed) {
    Session s(Clock{5e6, 1.0 + 70e-6}, Clock{9e6, 1.0 - 60e-6}, seed);
    s.radio = radio;
    while (s.t < 90.0) {
      const double tEnd = s.exchange();
      const double next = s.nextStart();
      for (double tt = tEnd; tt < next; tt += 0.0125) {
        if (tt > 20.0) worst = std::max(worst, std::fabs(s.error(tt)));
      }
      s.t = next;
    }
  }
  return worst;
}
}  // namespace

// The error is set by the radio, roughly a third of the mean one-way jitter: documented figures in docs/LINK.md.
EPDJ_TEST(timesync_error_scales_with_radio_jitter) {
  Radio quiet;
  quiet.jitterFwdUs = 40;
  quiet.jitterRevUs = 80;
  quiet.spikeProb = 0.0;
  Radio typical;  // the default: 150 / 400 us means, 5% retries
  Radio rough;
  rough.jitterFwdUs = 300;
  rough.jitterRevUs = 800;
  const double q = worstError(quiet), t = worstError(typical), r = worstError(rough);
  std::printf("    (worst error: quiet link %.0f us, typical %.0f us, rough (2x jitter) %.0f us)\n", q, t, r);
  CHECK(q < 60.0);
  CHECK(t < 200.0);
  CHECK(r < 450.0);
  CHECK(q < t && t < r);
}

EPDJ_TEST(timesync_applied_clock_never_steps_once_running) {
  Session s(Clock{1e6, 1.0 + 100e-6}, Clock{7e6, 1.0 - 100e-6}, 42);
  double maxStepUs = 0.0;
  bool havePrev = false;
  uint32_t prevL = 0, prevP = 0;
  size_t samples = 0;
  while (s.t < 60.0) {
    const double tEnd = s.exchange();
    const double next = s.nextStart();
    for (double tt = tEnd; tt < next; tt += 0.001) {
      if (!s.sync.valid() || tt < 15.0) {
        havePrev = false;
        continue;
      }
      const uint32_t l = s.local.read(tt);
      const uint32_t p = s.sync.peerTime(l);
      if (havePrev) {
        // How far did (estimated peer time - local time) move during this millisecond?
        const double d = static_cast<double>(static_cast<int32_t>((p - prevP) - (l - prevL)));
        maxStepUs = std::max(maxStepUs, std::fabs(d));
        ++samples;
      }
      prevL = l;
      prevP = p;
      havePrev = true;
    }
    s.t = next;
  }
  std::printf("    (largest change of the offset in 1 ms: %.0f us over %zu steps)\n", maxStepUs, samples);
  CHECK(samples > 30000);
  // Budget: estimated relative skew 0.2 us/ms + slew limit 0.5 us/ms + integer rounding of the two
  // offset terms (+/-2) + the sub-microsecond mismatch between old and new model at a refit: < 5.
  // A refit that moved the estimate by tens of microseconds in one go would show up as a spike.
  CHECK(maxStepUs <= 5.0);
}

EPDJ_TEST(timesync_peer_and_local_time_are_inverse) {
  Session s(Clock{2e6, 1.0 + 90e-6}, Clock{kTwo32 - 1e6, 1.0 - 80e-6}, 5);
  while (s.t < 30.0) {
    s.exchange();
    s.t = s.nextStart();
  }
  CHECK(s.sync.valid());
  Rng r(8);
  for (int i = 0; i < 2000; ++i) {
    const uint32_t local = s.local.read(25.0 + r.uni() * 5.0);
    const uint32_t peer = s.sync.peerTime(local);
    const uint32_t back = s.sync.localTime(peer);
    CHECK(std::abs(static_cast<int32_t>(back - local)) <= 2);
    CHECK_EQ(s.sync.offsetUs(local), static_cast<int32_t>(peer - local));
  }
}

EPDJ_TEST(timesync_recovers_from_a_clock_step) {
  Session s(Clock{1e6, 1.0}, Clock{3e6, 1.0}, 77);
  const double stepAt = 40.0;
  bool stepped = false;
  double recovered = -1.0, lastBadAfter = -1.0;
  while (s.t < 90.0) {
    if (!stepped && s.t >= stepAt) {
      s.peer.origin += 50000.0;  // the peer's counter jumps 50 ms
      stepped = true;
    }
    const double tEnd = s.exchange();
    const double next = s.nextStart();
    for (double tt = tEnd; tt < next; tt += 0.0125) {
      if (!stepped || !s.sync.valid()) continue;
      if (std::fabs(s.error(tt)) > 200.0) lastBadAfter = tt - stepAt;
    }
    s.t = next;
  }
  recovered = lastBadAfter;
  std::printf("    (error above 200 us until %.1f s after a 50 ms clock step)\n", recovered);
  CHECK(recovered >= 0.0);
  CHECK(recovered < 12.0);
  CHECK(std::fabs(s.error(89.9)) < 200.0);
}

EPDJ_TEST(timesync_rejects_impossible_exchanges) {
  TimeSync ts;
  // reply "sent" before it was received
  CHECK(ts.addExchange(1000, 5000, 4000, 3000) == TimeSync::Result::Invalid);
  // response arrives before the request left
  CHECK(ts.addExchange(1000, 5000, 5001, 900) == TimeSync::Result::Invalid);
  // the responder claims to have held it longer than the whole exchange
  CHECK(ts.addExchange(1000, 5000, 9000, 3000) == TimeSync::Result::Invalid);
  // round trip beyond kMaxRttUs
  CHECK(ts.addExchange(0, 100000, 100010, TimeSync::kMaxRttUs + 100) == TimeSync::Result::Invalid);
  CHECK(!ts.valid());
  CHECK_EQ(ts.exchanges(), 0);
  // a sane one
  CHECK(ts.addExchange(1000, 5000, 5030, 3030) == TimeSync::Result::Used);
  CHECK(ts.valid());
  CHECK_EQ(ts.rttUs(), 2000);
  // theta = (t3 - t4) + rtt/2 = 2000 + 1000: the peer is ~3000 us ahead
  CHECK_EQ(ts.offsetUs(2000), 3000);
  CHECK_EQ(ts.peerTime(2000), 5000);
  CHECK_EQ(ts.localTime(5000), 2000);
  ts.reset();
  CHECK(!ts.valid());
  CHECK_EQ(ts.offsetUs(5), 0);
}

EPDJ_TEST(timesync_a_slow_round_trip_barely_moves_the_estimate) {
  Session s(Clock{1e6, 1.0}, Clock{4e6, 1.0}, 3);
  s.radio.spikeProb = 0.0;
  while (s.t < 20.0) {
    s.exchange();
    s.t = s.nextStart();
  }
  const double before = s.error(20.0);
  // 6 ms extra in one direction only: an unfiltered offset sample would be 3 ms off.
  const uint32_t t1 = s.local.read(20.0), t2 = s.peer.read(20.0 + 0.0008), t3 = s.peer.read(20.0 + 0.00083);
  const uint32_t t4 = s.local.read(20.0 + 0.00083 + 0.0068);
  CHECK(s.sync.addExchange(t1, t2, t3, t4) == TimeSync::Result::Slow);
  CHECK(std::fabs(s.error(20.01) - before) < 25.0);
}

EPDJ_TEST(timesync_wraps_cleanly_when_the_offset_exceeds_half_the_counter) {
  // Peer counter ~40 minutes ahead (more than 2^31 us): only modular differences are meaningful.
  Session s(Clock{1e6, 1.0}, Clock{1e6 + 2.4e9, 1.0}, 9);
  while (s.t < 15.0) {
    s.exchange();
    s.t = s.nextStart();
  }
  for (double tt = 14.0; tt < 15.0; tt += 0.01) CHECK(std::fabs(s.error(tt)) < 200.0);
}
