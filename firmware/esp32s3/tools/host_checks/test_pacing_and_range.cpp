// tick_pacing.h and epdj_hal/touch_range.h: the arithmetic behind the engine task's catch-up rule and the touch
// sensor's measurement-length fit. Neither needs FreeRTOS or a chip.
#include <cstdint>

#include "epdj_hal/touch_range.h"
#include "test_harness.h"
#include "tick_pacing.h"

using namespace epdj;
using epdj::hal::touch_range::Probe;
using epdj::hal::touch_range::cyclesForScan;

EPDJ_TEST(small_lag_is_caught_up_large_lag_is_dropped) {
  using app::shouldDropLostTicks;
  CHECK(!shouldDropLostTicks(1000, 1000));  // on time
  CHECK(!shouldDropLostTicks(1001, 1000));
  CHECK(!shouldDropLostTicks(1002, 1000));
  CHECK(shouldDropLostTicks(1003, 1000));
  CHECK(shouldDropLostTicks(5000, 1000));
}

EPDJ_TEST(tick_counter_wrap_does_not_look_like_a_huge_lag) {
  using app::shouldDropLostTicks;
  CHECK(!shouldDropLostTicks(0u, 0xFFFFFFFFu));         // one tick behind across the wrap
  CHECK(!shouldDropLostTicks(1u, 0xFFFFFFFFu));         // two behind
  CHECK(shouldDropLostTicks(2u, 0xFFFFFFFFu));          // three behind
  CHECK(shouldDropLostTicks(1u, 0xFFFFFFFEu));
}

namespace {
// scan(c) = fixed + perCycle * c, the model touch_range.h fits.
Probe probe(uint32_t cycles, double fixedUs, double perCycleUs) {
  return Probe{cycles, static_cast<uint32_t>(fixedUs + perCycleUs * cycles + 0.5)};
}
}  // namespace

EPDJ_TEST(range_fit_hits_the_target_on_an_exact_model) {
  // Eight pads of ~1.2 ms each at 500 cycles plus 300 us of fixed cost.
  const Probe hi = probe(500, 300, 19.4);
  const Probe lo = probe(125, 300, 19.4);
  bool clamped = true;
  const uint32_t c = cyclesForScan(lo, hi, 800, 8, 500, &clamped);
  CHECK(!clamped);
  CHECK(c >= 25 && c <= 26);  // (800 - 300) / 19.4 = 25.8
  const double scan = 300 + 19.4 * c;
  CHECK(scan <= 800.0 + 1.0);
  CHECK(scan >= 800.0 - 20.0);
}

EPDJ_TEST(range_fit_with_no_fixed_cost_scales_proportionally) {
  bool clamped = true;
  const uint32_t c = cyclesForScan(Probe{125, 2500}, Probe{500, 10000}, 800, 8, 500, &clamped);
  CHECK(!clamped);
  CHECK_EQ(c, 40);  // 20 us per cycle
}

EPDJ_TEST(range_fit_stays_within_limits_and_says_so) {
  bool clamped = false;
  // Fixed cost alone is above the target: the shortest allowed measurement, flagged.
  CHECK_EQ(cyclesForScan(probe(125, 1500, 19.4), probe(500, 1500, 19.4), 800, 8, 500, &clamped), 8);
  CHECK(clamped);
  // Tiny pads: even the cap scans faster than the target; the cap is used.
  clamped = false;
  CHECK_EQ(cyclesForScan(Probe{125, 150}, Probe{500, 600}, 800, 8, 500, &clamped), 500);
  CHECK(clamped);
}

EPDJ_TEST(range_fit_treats_a_negative_fixed_cost_as_zero) {
  // Measurement noise: the line through the probes crosses below zero at 0 cycles.
  bool clamped = true;
  const uint32_t c = cyclesForScan(Probe{125, 2400}, Probe{500, 10000}, 800, 8, 500, &clamped);
  CHECK(!clamped);
  CHECK(c > 0 && c <= 500);
  CHECK_EQ(c, 800ull * 375 / 7600);  // fixed = 0, slope = 7600 / 375
}

EPDJ_TEST(range_fit_rejects_unusable_probes) {
  bool clamped = false;
  CHECK_EQ(cyclesForScan(Probe{125, 0}, Probe{500, 10000}, 800, 8, 500, &clamped), 0);      // not measured
  CHECK_EQ(cyclesForScan(Probe{125, 2500}, Probe{500, 0}, 800, 8, 500, &clamped), 0);
  CHECK_EQ(cyclesForScan(Probe{500, 10000}, Probe{125, 2500}, 800, 8, 500, &clamped), 0);   // swapped
  CHECK_EQ(cyclesForScan(Probe{125, 9000}, Probe{500, 9000}, 800, 8, 500, &clamped), 0);    // scan did not grow
  CHECK_EQ(cyclesForScan(Probe{125, 2500}, Probe{500, 10000}, 800, 600, 500, &clamped), 0); // min > max
}
