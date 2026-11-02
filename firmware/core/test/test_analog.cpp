#include <cmath>
#include <vector>

#include "epdj/analog.h"
#include "test_harness.h"

using namespace epdj;
using proto::FaderCurve;

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
  double uni() { return static_cast<double>(next()) * (1.0 / 4294967296.0); }  // [0,1)
  // Integer noise uniform in [-amp, amp].
  int noise(int amp) { return static_cast<int>(next() % static_cast<uint32_t>(2 * amp + 1)) - amp; }
};

uint16_t adc(int v) { return static_cast<uint16_t>(v < 0 ? 0 : (v > 4095 ? 4095 : v)); }

AnalogControl::Config cfg(uint16_t lo = 0, uint16_t hi = 4095, FaderCurve c = FaderCurve::Linear,
                          uint16_t dlo = 0, uint16_t dhi = 0, bool inv = false) {
  AnalogControl::Config k;
  k.rawMin = lo;
  k.rawMax = hi;
  k.curve = c;
  k.deadLoPermille = dlo;
  k.deadHiPermille = dhi;
  k.invert = inv;
  return k;
}

// Holds `raw` for `ms` ticks (1 kHz) and returns the final value.
uint16_t settle(AnalogControl& a, uint64_t& t, uint16_t raw, int ms) {
  uint16_t v = a.value();
  for (int i = 0; i < ms; ++i) {
    v = a.update(raw, t).value;
    t += 1000;
  }
  return v;
}

// Steady-state output for a raw value on a fresh control (first update = direct mapping).
uint16_t steady(const AnalogControl::Config& k, int raw) {
  AnalogControl a;
  a.configure(k);
  return a.update(adc(raw), 0).value;
}

}  // namespace

EPDJ_TEST(step_response_within_two_ms) {
  const int sizes[] = {8, 12, 20, 50, 300, 1500, 4000};
  for (int dir : {1, -1}) {
    for (int size : sizes) {
      AnalogControl a;
      a.configure(cfg());
      uint64_t t = 0;
      const int start = dir > 0 ? 100 : 4000;
      settle(a, t, adc(start), 200);
      const int target = start + dir * size;
      if (target < 0 || target > 4095) continue;
      int reportedAtMs = -1;
      uint16_t v = 0;
      for (int ms = 0; ms < 5; ++ms) {
        const auto r = a.update(adc(target), t);
        t += 1000;
        if (r.changed && reportedAtMs < 0) {
          reportedAtMs = ms;
          v = r.value;
        }
      }
      CHECK(reportedAtMs >= 0 && reportedAtMs <= 2);
      // Reported value is already the final mapping (zero filter lag on a real step).
      CHECK_EQ(v, steady(cfg(), target));
    }
  }
}

EPDJ_TEST(fast_ramps_are_reported_within_two_ms_of_starting) {
  for (double countsPerMs : {4.0, 6.0, 10.0, 40.0}) {
    AnalogControl a;
    a.configure(cfg());
    uint64_t t = 0;
    settle(a, t, 1000, 300);
    int firstChangeMs = -1;
    for (int ms = 1; ms <= 20; ++ms) {
      const auto r = a.update(adc(1000 + static_cast<int>(countsPerMs * ms)), t);
      t += 1000;
      if (r.changed && firstChangeMs < 0) firstChangeMs = ms;
    }
    CHECK(firstChangeMs >= 1 && firstChangeMs <= 2);
  }
}

EPDJ_TEST(output_tracks_a_moving_pot_with_small_lag) {
  AnalogControl a;
  a.configure(cfg());
  uint64_t t = 0;
  settle(a, t, 500, 300);
  int worst = 0;
  for (int ms = 1; ms <= 1500; ++ms) {  // 2 counts/ms for 3000 counts
    const int raw = 500 + 2 * ms;
    const auto r = a.update(adc(raw), t);
    t += 1000;
    const int err = std::abs(static_cast<int>(r.value) - static_cast<int>(steady(cfg(), raw)));
    if (err > worst) worst = err;
  }
  CHECK(worst <= 14 * 4);  // within ~14 counts of the true position (lag is bounded by the filter bands)
}

EPDJ_TEST(still_pot_with_plus_minus_three_counts_never_produces_output) {
  for (uint32_t seed : {1u, 2u, 3u, 77u}) {
    for (int center : {0, 2, 500, 2048, 4093, 4095}) {
      Rng rng(seed);
      AnalogControl a;
      a.configure(cfg());
      uint64_t t = 0;
      a.update(adc(center + rng.noise(3)), t);  // first update reports once by design
      int changes = 0;
      for (int i = 0; i < 60000; ++i) {
        t += 1000;
        changes += a.update(adc(center + rng.noise(3)), t).changed;
      }
      CHECK_EQ(changes, 0);
    }
  }
}

EPDJ_TEST(worst_case_alternating_noise_never_produces_output) {
  // +3 / -3 every sample is the worst case for any threshold below 6 counts.
  AnalogControl a;
  a.configure(cfg());
  uint64_t t = 0;
  a.update(adc(2048 - 3), t);
  int changes = 0;
  for (int i = 0; i < 20000; ++i) {
    t += 1000;
    changes += a.update(adc(2048 + ((i & 1) ? -3 : 3)), t).changed;
  }
  CHECK_EQ(changes, 0);
}

EPDJ_TEST(slow_wander_of_plus_minus_three_counts_never_produces_output) {
  // The adversarial case for the gate: the frozen value is captured at -3 and the pot then sits at +3
  // for a long time (supply ripple / thermal wander). Peak-to-peak 6 must stay below the gate.
  for (int period : {50, 400, 3000}) {
    AnalogControl a;
    a.configure(cfg());
    uint64_t t = 0;
    a.update(adc(2048 - 3), t);
    int changes = 0;
    for (int i = 1; i < 60000; ++i) {
      t += 1000;
      const int wander = ((i / period) & 1) ? -3 : 3;  // square wave, starts at +3 right after the -3 capture
      changes += a.update(adc(2048 + wander), t).changed;
    }
    CHECK_EQ(changes, 0);
  }
}

EPDJ_TEST(noise_after_a_move_does_not_reopen_the_gate) {
  Rng rng(9);
  AnalogControl a;
  a.configure(cfg());
  uint64_t t = 0;
  for (int i = 0; i < 100; ++i) a.update(adc(1000 + rng.noise(3)), t += 1000);
  for (int i = 0; i < 400; ++i) a.update(adc(3000 + rng.noise(3)), t += 1000);  // moves, then rests
  int changes = 0;
  for (int i = 0; i < 30000; ++i) changes += a.update(adc(3000 + rng.noise(3)), t += 1000).changed;
  CHECK_EQ(changes, 0);
}

EPDJ_TEST(real_movement_through_noise_is_followed_to_the_right_place) {
  Rng rng(5);
  AnalogControl a;
  a.configure(cfg());
  uint64_t t = 0;
  for (int i = 0; i < 100; ++i) a.update(adc(1000 + rng.noise(3)), t += 1000);
  uint16_t v = 0;
  for (int i = 0; i < 400; ++i) v = a.update(adc(3000 + rng.noise(3)), t += 1000).value;
  CHECK(std::abs(static_cast<int>(v) - static_cast<int>(steady(cfg(), 3000))) <= 5 * 4);
}

EPDJ_TEST(endpoints_are_exactly_zero_and_full_scale_for_every_curve) {
  for (FaderCurve c : {FaderCurve::Linear, FaderCurve::SharpCut, FaderCurve::Smooth}) {
    for (uint16_t dead : {0, 15, 200, 400}) {
      const auto k = cfg(40, 4055, c, dead, dead);
      CHECK_EQ(steady(k, 0), 0);
      CHECK_EQ(steady(k, 40), 0);
      CHECK_EQ(steady(k, 4055), 16383);
      CHECK_EQ(steady(k, 4095), 16383);
      // Settled approach from inside the range ends on the exact value too.
      AnalogControl a;
      a.configure(k);
      uint64_t t = 0;
      settle(a, t, 2000, 100);
      CHECK_EQ(settle(a, t, 4090, 200), 16383);
      CHECK_EQ(settle(a, t, 5, 200), 0);
    }
  }
}

EPDJ_TEST(dead_zones_pin_the_ends_and_rescale_the_middle) {
  const auto k = cfg(0, 4000, FaderCurve::Linear, 100, 200);  // 10% low, 20% high
  CHECK_EQ(steady(k, 0), 0);
  CHECK_EQ(steady(k, 399), 0);       // inside the low dead zone (< 400 counts)
  CHECK_EQ(steady(k, 400), 0);
  CHECK(steady(k, 420) > 0);
  CHECK_EQ(steady(k, 3200), 16383);  // 80% of 4000: start of the high dead zone
  CHECK_EQ(steady(k, 3999), 16383);
  CHECK(steady(k, 3180) < 16383);
  // Live part spans the whole output range linearly: midpoint of 400..3200 is 1800.
  CHECK_NEAR(steady(k, 1800), 8191.5, 2);
  CHECK_NEAR(steady(k, 1100), 16383.0 * (1100 - 400) / 2800.0, 2);
}

EPDJ_TEST(all_curves_are_monotonic_over_the_whole_adc_range) {
  const uint16_t deads[] = {0, 15, 60, 399};
  for (FaderCurve c : {FaderCurve::Linear, FaderCurve::SharpCut, FaderCurve::Smooth}) {
    for (uint16_t dlo : deads) {
      for (uint16_t dhi : deads) {
        for (bool inv : {false, true}) {
          int prev = inv ? 16384 : -1;
          for (int raw = 0; raw <= 4095; ++raw) {
            const int v = steady(cfg(40, 4055, c, dlo, dhi, inv), raw);
            CHECK(v >= 0 && v <= 16383);
            if (inv) CHECK(v <= prev);
            else CHECK(v >= prev);
            prev = v;
          }
        }
      }
    }
  }
}

EPDJ_TEST(curve_shapes) {
  const auto lin = cfg(0, 4000, FaderCurve::Linear);
  const auto sharp = cfg(0, 4000, FaderCurve::SharpCut);
  const auto smooth = cfg(0, 4000, FaderCurve::Smooth);
  CHECK_NEAR(steady(lin, 1000), 16383 * 0.25, 2);
  // SHARP_CUT: full scale after 10% of the travel, linear below.
  CHECK_NEAR(steady(sharp, 200), 16383 * 0.5, 4);
  CHECK_EQ(steady(sharp, 400), 16383);
  CHECK_EQ(steady(sharp, 2000), 16383);
  // SMOOTH: smoothstep, so 0.5 -> 0.5, 0.25 -> 0.15625, and slow at the ends.
  CHECK_NEAR(steady(smooth, 2000), 16383 * 0.5, 2);
  CHECK_NEAR(steady(smooth, 1000), 16383 * 0.15625, 3);
  CHECK(steady(smooth, 200) < steady(lin, 200));
}

EPDJ_TEST(invert_flag_flips_after_the_curve) {
  const auto k = cfg(0, 4000, FaderCurve::Smooth, 15, 15, false);
  auto ki = k;
  ki.invert = true;
  for (int raw = 0; raw <= 4000; raw += 37) {
    const int a = steady(k, raw), b = steady(ki, raw);
    CHECK(std::abs(a + b - 16383) <= 1);
  }
  CHECK_EQ(steady(ki, 0), 16383);
  CHECK_EQ(steady(ki, 4000), 0);
}

EPDJ_TEST(swapped_min_max_means_inverted_pot) {
  const auto normal = cfg(100, 3900);
  const auto swapped = cfg(3900, 100);
  CHECK_EQ(steady(swapped, 3900), 0);
  CHECK_EQ(steady(swapped, 100), 16383);
  CHECK_EQ(steady(swapped, 4095), 0);
  CHECK_EQ(steady(swapped, 0), 16383);
  for (int raw = 0; raw <= 4095; raw += 61) {
    CHECK(std::abs(steady(normal, raw) + steady(swapped, raw) - 16383) <= 1);
  }
  // Swapped min/max and the invert flag cancel out.
  auto both = swapped;
  both.invert = true;
  for (int raw = 0; raw <= 4095; raw += 61) CHECK_EQ(steady(both, raw), steady(normal, raw));
}

EPDJ_TEST(degenerate_calibration_does_not_crash) {
  CHECK_EQ(steady(cfg(2000, 2000), 100), 0);
  CHECK_EQ(steady(cfg(2000, 2000), 4000), 0);
}

EPDJ_TEST(first_update_reports_and_identical_input_does_not) {
  AnalogControl a;
  a.configure(cfg());
  const auto first = a.update(1234, 0);
  CHECK(first.changed);
  CHECK_EQ(first.value, steady(cfg(), 1234));
  CHECK(!a.update(1234, 1000).changed);
  CHECK(!a.update(1234, 2000).changed);
}

EPDJ_TEST(live_reconfiguration_re_evaluates_the_output) {
  AnalogControl a;
  a.configure(cfg(0, 4095));
  uint64_t t = 0;
  settle(a, t, 2048, 100);
  const uint16_t before = a.value();
  a.configure(cfg(0, 4095, FaderCurve::Linear, 0, 0, true));
  const auto r = a.update(2048, t);
  CHECK(r.changed || r.value == before);  // 8191/8192 mirror: at most one count apart
  CHECK(std::abs(static_cast<int>(r.value) + static_cast<int>(before) - 16383) <= 1);
  a.configure(cfg(2048, 4095));
  CHECK_EQ(a.update(2048, t + 1000).value, 0);
}

EPDJ_TEST(calibration_measures_extremes_with_one_percent_inset) {
  CalibrationTracker c;
  CHECK(!c.active());
  c.begin();
  CHECK(c.active());
  Rng rng(3);
  for (int i = 0; i < 2000; ++i) c.feed(adc(500 + i * 3 + rng.noise(2)));  // sweeps up to ~6500 -> clipped at 4095
  for (int i = 0; i < 2100; ++i) c.feed(adc(4095 - i * 2 + rng.noise(2)));  // back down, clipped at 0
  CalibrationTracker::Result r;
  CHECK(c.end(&r) == CalibrationTracker::Status::Ok);
  CHECK(!c.active());
  // Measured [0 .. 4095]: inset is 1% of the span (41) at each end.
  CHECK(r.rawMin >= 38 && r.rawMin <= 46);
  CHECK(r.rawMax >= 4049 && r.rawMax <= 4057);
}

EPDJ_TEST(calibration_inset_is_exactly_one_percent_for_clean_input) {
  CalibrationTracker c;
  c.begin();
  for (int v : {1000, 1500, 3000, 2000, 5000 / 2 + 1500, 3500}) c.feed(adc(v));
  c.feed(1000);
  c.feed(3500);
  c.feed(1000);
  CalibrationTracker::Result r;
  CHECK(c.end(&r) == CalibrationTracker::Status::Ok);
  CHECK_EQ(r.rawMin, 1025);  // span 2500 -> inset 25
  CHECK_EQ(r.rawMax, 3475);
}

EPDJ_TEST(calibration_fails_below_minimum_span) {
  // Plateaus of 3 identical samples survive the median exactly, so the span is what we say it is.
  auto run = [](int lo, int hi, CalibrationTracker::Result* r) {
    CalibrationTracker c;
    c.begin();
    for (int i = 0; i < 3; ++i) c.feed(adc(lo));
    for (int i = 0; i < 3; ++i) c.feed(adc(hi));
    const auto st = c.end(r);
    CHECK(!c.active());
    return st;
  };
  CalibrationTracker::Result r;
  r.rawMin = 7;
  r.rawMax = 9;
  CHECK(run(1000, 2499, &r) == CalibrationTracker::Status::SpanTooSmall);  // span 1499
  CHECK_EQ(r.rawMin, 7);  // untouched on failure
  CHECK_EQ(r.rawMax, 9);
  CHECK(run(1000, 2500, &r) == CalibrationTracker::Status::Ok);            // span 1500 is the minimum
  CHECK(run(0, 100, &r) == CalibrationTracker::Status::SpanTooSmall);
  CHECK(run(2000, 2000, &r) == CalibrationTracker::Status::SpanTooSmall);
}

EPDJ_TEST(calibration_requires_begin_and_ends_cleanly) {
  CalibrationTracker c;
  CalibrationTracker::Result r;
  c.feed(100);
  c.feed(4000);
  CHECK(c.end(&r) == CalibrationTracker::Status::NotActive);
  c.begin();
  CHECK(c.end(&r) == CalibrationTracker::Status::SpanTooSmall);  // no samples
  CHECK(c.end(&r) == CalibrationTracker::Status::NotActive);     // already ended
  c.begin();
  c.feed(0);
  c.feed(4095);
  c.begin();  // restart discards the previous run
  for (int i = 0; i < 10; ++i) c.feed(2000);
  CHECK(c.end(&r) == CalibrationTracker::Status::SpanTooSmall);
}

EPDJ_TEST(calibration_ignores_single_sample_glitches) {
  CalibrationTracker c;
  c.begin();
  for (int i = 0; i < 500; ++i) {
    int v = 100 + i * 7;  // 100 .. 3593
    if (i == 200) v = 4095;  // one-sample spike
    if (i == 300) v = 0;
    c.feed(adc(v));
  }
  CalibrationTracker::Result r;
  CHECK(c.end(&r) == CalibrationTracker::Status::Ok);
  // True range after the 3-sample median is 107..3586 (span 3479, inset 35). Taking either spike would
  // have given rawMin ~36 or rawMax ~4059.
  CHECK(r.rawMin >= 130 && r.rawMin <= 160);
  CHECK(r.rawMax >= 3530 && r.rawMax <= 3570);
}

EPDJ_TEST(calibration_result_feeds_back_into_exact_endpoints) {
  // End-to-end: sweep a pot that reads 123..3987, calibrate, then both physical ends map exactly.
  CalibrationTracker c;
  c.begin();
  for (int i = 0; i <= 3864; ++i) c.feed(adc(123 + i));
  CalibrationTracker::Result r;
  CHECK(c.end(&r) == CalibrationTracker::Status::Ok);
  AnalogControl::Config k;
  k.rawMin = r.rawMin;
  k.rawMax = r.rawMax;
  k.deadLoPermille = k.deadHiPermille = 0;
  CHECK_EQ(steady(k, 123), 0);
  CHECK_EQ(steady(k, 3987), 16383);
}

EPDJ_TEST(default_constructed_control_uses_default_dead_zones) {
  AnalogControl a;  // never configured: defaults (rawMin 40, rawMax 4055, dead zones 15 per mille)
  const auto lo = a.update(60, 0);   // inside the 1.5% low dead zone (40 + 0.015 * 4015 = 100)
  CHECK_EQ(lo.value, 0);
  AnalogControl b;
  CHECK_EQ(b.update(4000, 0).value, 16383);
  AnalogControl c;
  const uint16_t mid = c.update(2048, 0).value;
  CHECK(mid > 8000 && mid < 8400);
}

EPDJ_TEST(out_of_range_dead_zones_behave_like_the_contract_maximum) {
  // The contract allows 0..400 per mille per end; anything larger is clamped to that, never trusted.
  AnalogControl::Config wild = cfg();
  wild.deadLoPermille = 1000;
  wild.deadHiPermille = 65535;
  AnalogControl::Config max = cfg();
  max.deadLoPermille = 400;
  max.deadHiPermille = 400;
  for (int raw = 0; raw <= 4095; raw += 7) CHECK_EQ(steady(wild, raw), steady(max, raw));
  CHECK(steady(max, 1700) > 0 && steady(max, 1700) < 16383);       // the live middle 20% is still live
}

EPDJ_TEST(linear_mapping_is_unbiased_to_the_nearest_14_bit_code) {
  // out = round(raw * 16383 / 4095): the mean error over the whole ADC range must be ~0 (a floor would
  // sit half a code low) and no single reading may be more than one code off.
  const auto k = cfg(0, 4095, FaderCurve::Linear, 0, 0, false);
  double sum = 0.0;
  for (int raw = 0; raw <= 4095; ++raw) {
    const double e = static_cast<double>(steady(k, raw)) - raw * 16383.0 / 4095.0;
    CHECK(std::fabs(e) <= 1.0);
    sum += e;
  }
  CHECK(std::fabs(sum / 4096.0) < 0.1);
}
