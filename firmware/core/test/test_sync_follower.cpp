// Following an external MIDI clock: tempo estimate under jitter and dropped ticks, timeout, transport mirror.
#include <cmath>
#include <vector>

#include "epdj/midi.h"
#include "epdj/sync.h"
#include "test_harness.h"

using namespace epdj;
using proto::TransportState;
using Event = TransportFsm::Event;

namespace {

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  uint32_t below(uint32_t n) { return next() % n; }
  double uniform() { return static_cast<double>(next() >> 8) / 16777216.0; }  // [0,1)
  double sym() { return uniform() * 2.0 - 1.0; }                               // [-1,1)
};

double periodUs(uint16_t bpmX10) { return 25e6 / bpmX10; }

// Tick k arrives at base + k*P + jitter, jitter uniform within +/- jit*P.
uint64_t tickAt(uint64_t base, double p, uint32_t k, double jit, Rng& rng) {
  return base + static_cast<uint64_t>(std::llround(static_cast<double>(k) * p + rng.sym() * jit * p));
}

// Worst-case relative error of the estimate: timestamps' jitter telescopes, leaving 2*jit/(span ticks).
double bound(double jit, size_t ticksSpanned) { return 2.0 * jit / static_cast<double>(ticksSpanned); }

}  // namespace

EPDJ_TEST(follower_exact_on_a_clean_clock) {
  const uint16_t tempos[] = {200, 600, 1200, 1285, 2000, 2999, 3000};
  for (uint16_t bpmX10 : tempos) {
    ClockFollower f;
    const double p = periodUs(bpmX10);
    for (uint32_t k = 0; k < 23; ++k) {
      f.onClock(1000 + static_cast<uint64_t>(std::llround(k * p)));
      CHECK(!f.locked(1000 + static_cast<uint64_t>(std::llround(k * p))));  // < 24 ticks
    }
    const uint64_t t = 1000 + static_cast<uint64_t>(std::llround(23 * p));
    f.onClock(t);
    CHECK(f.locked(t));
    CHECK(std::abs(static_cast<int>(f.bpmX10(t)) - static_cast<int>(bpmX10)) <= 1);
  }
}

EPDJ_TEST(follower_not_locked_before_24_ticks_and_zero_when_unlocked) {
  ClockFollower f;
  CHECK(!f.locked(0));
  CHECK_EQ(f.bpmX10(0), 0);
  for (uint32_t k = 0; k < 23; ++k) f.onClock(k * 20833ull);
  CHECK_EQ(f.bpmX10(23 * 20833ull), 0);
  f.onClock(23 * 20833ull);
  CHECK(f.bpmX10(23 * 20833ull) != 0);
}

// Estimate error vs the worst case predicted by the telescoping argument, for a given timestamp jitter.
static void checkJitter(double jit, const uint32_t* counts, size_t nCounts) {
  const uint16_t tempos[] = {200, 600, 1200, 1285, 2000, 3000};
  for (uint16_t bpmX10 : tempos) {
    for (size_t ci = 0; ci < nCounts; ++ci) {
      const uint32_t n = counts[ci];
      for (uint32_t seed = 1; seed <= 25; ++seed) {
        Rng rng(seed * 7919u + n);
        ClockFollower f;
        const double p = periodUs(bpmX10);
        const uint64_t base = 0xFFFFFFFFull - 300000ull;  // the 32-bit timestamp wraps during the run
        uint64_t last = 0;
        for (uint32_t k = 0; k < n; ++k) {
          last = tickAt(base, p, k, jit, rng);
          f.onClock(last & 0xFFFFFFFFull);  // what a 32-bit timer would hand us
        }
        const size_t span = (n < ClockFollower::kWindow ? n : ClockFollower::kWindow) - 1;
        CHECK(f.locked(last & 0xFFFFFFFFull));
        const double err = std::fabs(f.bpmX10(last & 0xFFFFFFFFull) - static_cast<double>(bpmX10)) / bpmX10;
        // +1 count of rounding on the 0.1 BPM output grid, +timestamps rounded to whole microseconds.
        const double limit = bound(jit, span) + 1.0 / bpmX10 + 2.0 / p;
        if (!(err <= limit)) std::printf("      jit %.2f bpmX10 %u n %u seed %u: err %.4f limit %.4f\n", jit, bpmX10, n, seed, err, limit);
        CHECK(err <= limit);
      }
    }
  }
}

EPDJ_TEST(follower_tolerates_plus_minus_5_percent_jitter) {
  const uint32_t counts[] = {24, 30, 48, 100, 300};
  checkJitter(0.05, counts, 5);  // worst case ~0.43% at 24 ticks, ~0.21% (+ output grid) with a full window
}

EPDJ_TEST(follower_still_counts_ticks_correctly_with_plus_minus_20_percent_jitter) {
  // Each interval may be off by up to 40%, still less than half a tick, so no interval is miscounted.
  const uint32_t counts[] = {48, 100};
  checkJitter(0.20, counts, 2);
}

EPDJ_TEST(follower_estimate_is_the_same_across_the_32_bit_wrap) {
  Rng a(5), b(5);
  ClockFollower fa, fb;
  const double p = periodUs(1337);
  uint64_t lastA = 0, lastB = 0;
  for (uint32_t k = 0; k < 60; ++k) {
    lastA = tickAt(1000, p, k, 0.04, a);
    lastB = tickAt(0xFFFFFFFFull - 500000ull, p, k, 0.04, b);
    fa.onClock(lastA);
    fb.onClock(lastB);  // 64-bit time running over 2^32
  }
  CHECK_EQ(fa.bpmX10(lastA), fb.bpmX10(lastB));
  CHECK(fa.bpmX10(lastA) != 0);
}

EPDJ_TEST(follower_treats_dropped_ticks_as_missing_not_as_a_tempo_change) {
  const double drops[] = {0.10, 0.30};
  for (double dropP : drops) {
    for (uint32_t seed = 1; seed <= 30; ++seed) {
      Rng rng(seed * 104729u);
      ClockFollower f;
      const uint16_t bpmX10 = 1285;
      const double p = periodUs(bpmX10);
      uint64_t last = 0;
      size_t received = 0, dropped = 0;
      for (uint32_t k = 0; received < 80; ++k) {
        if (k > 0 && rng.uniform() < dropP) { ++dropped; continue; }
        last = tickAt(1000, p, k, 0.03, rng);
        f.onClock(last);
        ++received;
      }
      CHECK(dropped > 0);
      CHECK(f.locked(last));
      const double err = std::fabs(f.bpmX10(last) - static_cast<double>(bpmX10)) / bpmX10;
      CHECK(err <= bound(0.03, ClockFollower::kWindow - 1) + 1.0 / bpmX10 + 2.0 / p);
    }
  }
}

EPDJ_TEST(follower_times_out_after_500ms_and_restarts_cleanly_after_a_gap) {
  ClockFollower f;
  double p = periodUs(1200);
  uint64_t t = 0;
  for (uint32_t k = 0; k < 40; ++k) { t = 100000 + static_cast<uint64_t>(std::llround(k * p)); f.onClock(t); }
  CHECK(f.locked(t));
  CHECK_EQ(f.bpmX10(t), 1200);
  CHECK(f.locked(t + 500000));
  CHECK_EQ(f.bpmX10(t + 500000), 1200);
  CHECK(!f.locked(t + 500001));
  CHECK_EQ(f.bpmX10(t + 500001), 0);
  CHECK_EQ(f.bpmX10(t + 100000000), 0);

  // The master comes back at another tempo after a pause: no blending with the old window.
  p = periodUs(1500);
  const uint64_t resume = t + 700000;
  for (uint32_t k = 0; k < 23; ++k) {
    const uint64_t tk = resume + static_cast<uint64_t>(std::llround(k * p));
    f.onClock(tk);
    CHECK(!f.locked(tk));
  }
  const uint64_t tl = resume + static_cast<uint64_t>(std::llround(23 * p));
  f.onClock(tl);
  CHECK(f.locked(tl));
  CHECK(std::abs(static_cast<int>(f.bpmX10(tl)) - 1500) <= 1);
}

EPDJ_TEST(follower_converges_after_a_tempo_change_without_a_gap) {
  Rng rng(3);
  ClockFollower f;
  double p = periodUs(1200);
  uint64_t t = 0;
  double acc = 0;
  for (uint32_t k = 0; k < 100; ++k) { acc += p; t = static_cast<uint64_t>(std::llround(acc + rng.sym() * 0.03 * p)); f.onClock(t); }
  CHECK(std::abs(static_cast<int>(f.bpmX10(t)) - 1200) <= 4);
  p = periodUs(1500);
  for (uint32_t k = 0; k < ClockFollower::kWindow + 2; ++k) { acc += p; t = static_cast<uint64_t>(std::llround(acc + rng.sym() * 0.03 * p)); f.onClock(t); }
  CHECK(std::abs(static_cast<int>(f.bpmX10(t)) - 1500) <= 6);
  // Midway the estimate is somewhere between, never outside the two tempos.
  ClockFollower g;
  acc = 0;
  p = periodUs(1200);
  for (uint32_t k = 0; k < 60; ++k) { acc += p; g.onClock(static_cast<uint64_t>(std::llround(acc))); }
  p = periodUs(1500);
  for (uint32_t k = 0; k < 30; ++k) {
    acc += p;
    const uint64_t tk = static_cast<uint64_t>(std::llround(acc));
    g.onClock(tk);
    const int v = g.bpmX10(tk);
    CHECK(v >= 1190 && v <= 1510);
  }
}

EPDJ_TEST(follower_clamps_to_the_contract_range) {
  ClockFollower fast;
  uint64_t t = 0;
  for (uint32_t k = 0; k < 40; ++k) { t = 1000 + k * 4167ull; fast.onClock(t); }  // ~600 BPM
  CHECK_EQ(fast.bpmX10(t), 3000);
  ClockFollower slow;
  for (uint32_t k = 0; k < 40; ++k) { t = 1000 + k * 250000ull; slow.onClock(t); }  // 10 BPM, ticks 250 ms apart
  CHECK(slow.locked(t));
  CHECK_EQ(slow.bpmX10(t), 200);
}

EPDJ_TEST(follower_survives_bursts_and_garbage_timestamps) {
  ClockFollower f;
  for (int i = 0; i < 100; ++i) f.onClock(5000);  // everything in the same microsecond
  CHECK_EQ(f.bpmX10(5000), 0);
  CHECK(!f.locked(5000));

  Rng rng(11);
  uint64_t t = 0;
  for (int i = 0; i < 50000; ++i) {
    // Random walk with occasional jumps backwards and huge gaps.
    t += rng.below(40000);
    if (rng.below(50) == 0) t -= rng.below(100000) < t ? rng.below(100000) : 0;
    f.onClock(t);
    const uint16_t v = f.bpmX10(t);
    CHECK(v == 0 || (v >= 200 && v <= 3000));
  }
}

EPDJ_TEST(follower_transport_mirror_table) {
  ClockFollower f;
  CHECK(f.transport() == TransportState::Stopped);
  f.onStop();
  CHECK(f.transport() == TransportState::Stopped);  // nothing to stop
  f.onStart();
  CHECK(f.transport() == TransportState::Playing);
  f.onSongPosition(0);
  CHECK(f.transport() == TransportState::Playing);  // SPP while running is not a stop
  f.onStop();
  CHECK(f.transport() == TransportState::Paused);
  f.onSongPosition(16);
  CHECK(f.transport() == TransportState::Paused);
  f.onContinue();
  CHECK(f.transport() == TransportState::Playing);
  f.onStop();
  f.onSongPosition(0);  // stop + rewind
  CHECK(f.transport() == TransportState::Stopped);
  f.onContinue();
  CHECK(f.transport() == TransportState::Playing);
  f.onStop();
  f.onStart();
  CHECK(f.transport() == TransportState::Playing);
  f.reset();
  CHECK(f.transport() == TransportState::Stopped);
}

EPDJ_TEST(follower_dispatches_realtime_messages_only) {
  ClockFollower f;
  CHECK(f.onMessage(midi::start(), 0));
  CHECK(f.transport() == TransportState::Playing);
  CHECK(f.onMessage(midi::stop(), 0));
  CHECK(f.transport() == TransportState::Paused);
  CHECK(f.onMessage(midi::cont(), 0));
  CHECK(f.transport() == TransportState::Playing);
  CHECK(f.onMessage(midi::stop(), 0));
  CHECK(f.onMessage(midi::songPosition(0), 0));
  CHECK(f.transport() == TransportState::Stopped);
  CHECK(f.onMessage(midi::clock(), 123));
  CHECK(!f.onMessage(midi::noteOn(0, 60, 100), 0));
  CHECK(!f.onMessage(midi::mtcQuarterFrame(1), 0));
  CHECK(!f.onMessage(midi::Message(), 0));
  CHECK(!f.onMessage(midi::make(0xF2, 1, 0, 2), 0));  // malformed SPP (2 bytes) is not consumed
}

EPDJ_TEST(follower_mirrors_a_master_fsm_through_the_wire_messages) {
  Rng rng(21);
  TransportFsm master;
  ClockFollower f;
  for (int i = 0; i < 20000; ++i) {
    const uint32_t r = rng.below(8);
    if (r < 4) { master.onClockTick(); continue; }
    const auto o = master.handle(static_cast<Event>(r - 4));
    for (uint8_t k = 0; k < o.count; ++k) CHECK(f.onMessage(o.msg[k], 0));
    TransportState expect = master.state();
    // Pause at beat 0 puts Stop + SPP 0 on the wire, which is what a stop+rewind looks like too.
    if (expect == TransportState::Paused && master.songPositionBeats() == 0) expect = TransportState::Stopped;
    CHECK(f.transport() == expect);
  }
}

EPDJ_TEST(follower_locks_to_a_clock_generator_polled_at_1khz) {
  // The real path: ClockGenerator on the master side, ticks timestamped when the follower's engine
  // loop sees them (up to 1 ms late each).
  ClockGenerator gen;
  ClockFollower f;
  gen.start(0, 1285);
  uint64_t t = 0;
  size_t ticks = 0;
  for (; ticks < 100; ++t) {
    for (uint32_t n = gen.advance(t); n; --n) { f.onClock(t); ++ticks; }
  }
  const double err = std::fabs(f.bpmX10(t) - 1285.0) / 1285.0;
  CHECK(err < 0.0025);
  CHECK(f.bpmX10(t) != 0);
}
