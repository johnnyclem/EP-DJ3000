// epdj_hal/encoder_retry.h: when an absent or failing AS5600 is asked again, and the cost bound. Pure policy: the tests
// play the engine task (one call per millisecond) against a chip that comes and goes.
#include <cstdint>

#include "epdj_hal/encoder_retry.h"
#include "test_harness.h"

using epdj::hal::EncoderRetry;
using Change = EncoderRetry::Change;

namespace {

constexpr uint64_t kMs = 1000;
constexpr uint64_t kBoot = 2000000;

// The engine task's loop, 1 kHz, against a chip that answers only during [upFrom, upTo). Returns the number of bus reads made.
struct Sim {
  EncoderRetry r;
  uint64_t now = kBoot;
  unsigned reads = 0, losses = 0, recoveries = 0, failedReads = 0;
  uint64_t firstRecoveryAt = 0;

  void tick(bool chipUp) {
    if (r.mayRead(now)) {
      ++reads;
      if (!chipUp) ++failedReads;
      const Change c = r.report(chipUp, now);
      if (c == Change::Lost) ++losses;
      if (c == Change::Recovered) {
        ++recoveries;
        if (firstRecoveryAt == 0) firstRecoveryAt = now;
      }
    }
    now += kMs;
  }
};

}  // namespace

EPDJ_TEST(a_chip_absent_at_boot_is_asked_after_500_ms_and_then_every_500_ms) {
  EncoderRetry r;
  r.start(false, kBoot);
  CHECK(!r.answering());
  CHECK(!r.mayRead(kBoot));
  CHECK(!r.mayRead(kBoot + 499 * kMs));
  CHECK(r.mayRead(kBoot + 500 * kMs));  // exactly one interval after boot
  CHECK(r.report(false, kBoot + 500 * kMs) == Change::None);
  CHECK(!r.mayRead(kBoot + 500 * kMs + 499 * kMs));  // a failed trial starts a new wait, measured from the trial
  CHECK(r.mayRead(kBoot + 1000 * kMs));
  CHECK(r.report(true, kBoot + 1000 * kMs) == Change::Recovered);
  CHECK(r.answering());
  CHECK(r.mayRead(kBoot + 1000 * kMs));  // from now on every read is made
  CHECK(r.mayRead(kBoot + 1001 * kMs));
}

EPDJ_TEST(a_chip_that_answered_at_boot_is_read_every_time) {
  EncoderRetry r;
  r.start(true, kBoot);
  CHECK(r.answering());
  for (uint64_t t = kBoot; t < kBoot + 5 * kMs; t += kMs) {
    CHECK(r.mayRead(t));
    CHECK(r.report(true, t) == Change::None);
  }
}

EPDJ_TEST(one_failed_read_is_a_glitch_the_second_in_a_row_is_a_loss) {
  EncoderRetry r;
  r.start(true, kBoot);
  CHECK(r.report(false, kBoot) == Change::None);
  CHECK(r.answering() && r.mayRead(kBoot + kMs));      // still read every tick
  CHECK(r.report(true, kBoot + kMs) == Change::None);  // the streak is reset by a good read
  CHECK(r.report(false, kBoot + 2 * kMs) == Change::None);
  CHECK(r.answering());
  CHECK(r.report(false, kBoot + 3 * kMs) == Change::Lost);
  CHECK(!r.answering());
  CHECK(!r.mayRead(kBoot + 3 * kMs + 499 * kMs));
  CHECK(r.mayRead(kBoot + 3 * kMs + 500 * kMs));
  CHECK(r.report(true, kBoot + 3 * kMs + 500 * kMs) == Change::Recovered);
}

EPDJ_TEST(a_dead_chip_costs_two_reads_when_it_dies_and_one_every_500_ms_after_that) {
  Sim s;
  s.r.start(true, s.now);
  for (int i = 0; i < 100; ++i) s.tick(true);
  const unsigned healthy = s.reads;
  CHECK_EQ(healthy, 100u);
  for (int i = 0; i < 60000; ++i) s.tick(false);  // a minute with the cable out
  CHECK_EQ(s.losses, 1u);
  // 2 failed reads, then trials at +500 ms, +1000 ms, ... : at most 1 + 60000/500 = 121 more, never one per tick
  CHECK(s.failedReads <= 2u + 120u);
  CHECK(s.failedReads >= 2u + 118u);
  // worst case blocking time at 2 ms per failed read: 244 ms of 60 s
  CHECK(static_cast<uint64_t>(s.failedReads) * 2 * kMs < 60000 * kMs / 200);  // under 0.5 %
}

EPDJ_TEST(a_chip_absent_since_boot_costs_one_read_per_500_ms) {
  Sim s;
  s.r.start(false, s.now);
  for (int i = 0; i < 10000; ++i) s.tick(false);
  CHECK_EQ(s.reads, 19u);  // t = 500, 1000, ..., 9500 ms of the 10 s (0..9999 ms)
  CHECK_EQ(s.losses, 0u);  // it never answered, so it was never "lost"
}

EPDJ_TEST(a_chip_that_appears_is_found_within_one_interval_and_normal_reads_resume) {
  Sim s;
  s.r.start(false, s.now);
  const uint64_t appears = s.now + 2300 * kMs;
  while (s.now < appears) s.tick(false);
  const uint64_t cameUp = s.now;
  for (int i = 0; i < 1000; ++i) s.tick(true);
  CHECK_EQ(s.recoveries, 1u);
  CHECK(s.firstRecoveryAt >= cameUp && s.firstRecoveryAt - cameUp <= EncoderRetry::kRetryIntervalUs);
  // after that, every millisecond is a read again
  const unsigned before = s.reads;
  for (int i = 0; i < 100; ++i) s.tick(true);
  CHECK_EQ(s.reads - before, 100u);
}

EPDJ_TEST(a_chip_that_comes_and_goes_is_lost_and_recovered_each_time) {
  Sim s;
  s.r.start(true, s.now);
  for (int cycle = 0; cycle < 5; ++cycle) {
    for (int i = 0; i < 700; ++i) s.tick(true);
    for (int i = 0; i < 1300; ++i) s.tick(false);
  }
  for (int i = 0; i < 700; ++i) s.tick(true);
  CHECK_EQ(s.losses, 5u);
  CHECK_EQ(s.recoveries, 5u);
  CHECK(s.r.answering());
}
