#include <cmath>
#include <cstdlib>
#include <vector>

#include "epdj/jog.h"
#include "epdj/touch_ring.h"
#include "epdj/util.h"
#include "test_harness.h"

using namespace epdj;
using proto::PlatterSource;

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
  double uni() { return static_cast<double>(next()) * (1.0 / 4294967296.0); }
  double sym() { return 2.0 * uni() - 1.0; }
  int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1)); }
};

// Synthetic touch ring: finger = arc of `widthSectors` sectors with soft edges, rise per sector =
// covered fraction x peak x sector gain, +/- `noisePermille` uniform noise, quantised to counts.
struct RingSim {
  unsigned n;
  double baseline[8];
  double gain[8];
  double peakPermille = 40.0;
  double widthSectors = 1.5;
  double noisePermille = 3.0;

  explicit RingSim(unsigned sectors) : n(sectors) {
    for (unsigned i = 0; i < 8; ++i) {
      baseline[i] = 30000.0 + 900.0 * i;
      gain[i] = 1.0;
    }
  }
  static double wrapped(double d) { return d - std::floor(d + 0.5); }
  double coverage(unsigned sector, double fingerRev) const {
    const double pitch = 1.0 / n;
    const double w = widthSectors * 0.5 * pitch;
    const double edge = 0.15 * pitch;
    double sum = 0.0;
    const int steps = 32;
    for (int k = 0; k < steps; ++k) {
      const double phi = sector * pitch + ((k + 0.5) / steps - 0.5) * pitch;
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
    return sum / steps;
  }
  void read(double fingerRev, double amplitude, Rng& rng, uint16_t* out) const {
    for (unsigned i = 0; i < n; ++i) {
      double rise = noisePermille * rng.sym();
      if (amplitude > 0.0) rise += amplitude * peakPermille * gain[i] * coverage(i, fingerRev);
      out[i] = static_cast<uint16_t>(std::lround(baseline[i] * (1.0 + rise / 1000.0)));
    }
  }
};

// TouchRing + JogTracker at 1 kHz.
struct Pipe {
  TouchRing ring;
  JogTracker jog;
  RingSim sim;
  Rng rng;
  uint64_t nowUs = 0;
  TouchRing::Result lastRing;
  int32_t ticks = 0;
  int downs = 0, ups = 0, taps = 0;
  std::vector<int32_t> perTick;

  // `noise` is the ADC noise (+/- permille of baseline) from the very first sample, so the ring measures
  // the same noise it is then tested with. `settleMs` of idle follows construction.
  Pipe(unsigned n, uint32_t seed, bool invert = false, uint16_t tapMaxMs = 180, uint8_t tapMove = 8,
       double noise = 3.0, int settleMs = 600)
      : sim(n), rng(seed) {
    sim.noisePermille = noise;
    TouchRing::Config rc;
    rc.sectors = static_cast<uint8_t>(n);
    ring.configure(rc);
    JogTracker::Config jc;
    jc.source = PlatterSource::TouchRing;
    jc.invert = invert;
    jc.tapMaxMs = tapMaxMs;
    jc.tapMoveTicks = tapMove;
    jog.configure(jc);
    idle(settleMs);
    ticks = 0;
    perTick.clear();
  }

  void tick(double rev, double amplitude) {
    uint16_t raw[8];
    sim.read(rev, amplitude, rng, raw);
    lastRing = ring.update(raw, nowUs);
    JogTracker::Input in;
    in.touched = lastRing.touched;
    in.angleValid = lastRing.angleValid;
    in.angle = lastRing.angle;
    in.angleSigma = lastRing.angleSigma;
    const auto out = jog.update(in, nowUs);
    nowUs += 1000;
    ticks += out.ticks;
    downs += out.touchDown;
    ups += out.touchUp;
    taps += out.tap;
    perTick.push_back(out.ticks);
  }
  void idle(int ms) {
    for (int i = 0; i < ms; ++i) tick(0, 0);
  }
  void hold(double rev, int ms, double amp = 1.0) {
    for (int i = 0; i < ms; ++i) tick(rev, amp);
  }
  // Land at `startRev`, dwell, rotate `revs` at `revPerSec` (negative = counter-clockwise), dwell, lift.
  // Returns the ticks produced by the whole gesture.
  int32_t gesture(double startRev, double revPerSec, double revs, int dwellMs, int tailMs = 150) {
    const int32_t before = ticks;
    hold(startRev, dwellMs);
    const double dir = revPerSec < 0 ? -1.0 : 1.0;
    const int ms = static_cast<int>(std::lround(revs / std::fabs(revPerSec) * 1000.0));
    for (int i = 1; i <= ms; ++i) tick(startRev + dir * std::fabs(revPerSec) * i * 1e-3, 1.0);
    hold(startRev + dir * revs, tailMs);
    idle(100);
    return ticks - before;
  }
};

// Drives a JogTracker directly with a scripted angle stream.
struct Direct {
  JogTracker jog;
  uint64_t nowUs = 1000;
  int32_t ticks = 0;
  int downs = 0, ups = 0, taps = 0;
  explicit Direct(PlatterSource src = PlatterSource::As5600, bool invert = false, uint16_t tapMaxMs = 180,
                  uint8_t tapMove = 8) {
    JogTracker::Config c;
    c.source = src;
    c.invert = invert;
    c.tapMaxMs = tapMaxMs;
    c.tapMoveTicks = tapMove;
    jog.configure(c);
  }
  JogTracker::Output step(bool touched, uint16_t angle, bool valid = true) {
    JogTracker::Input in;
    in.touched = touched;
    in.angle = angle;
    in.angleValid = valid;
    const auto out = jog.update(in, nowUs);
    nowUs += 1000;
    ticks += out.ticks;
    downs += out.touchDown;
    ups += out.touchUp;
    taps += out.tap;
    return out;
  }
};

uint16_t wrap4096(int64_t a) { return static_cast<uint16_t>(((a % 4096) + 4096) % 4096); }

}  // namespace

// ---------------------------------------------------------------- ring path

EPDJ_TEST(ring_sweep_clockwise_one_revolution_is_1024_ticks_within_3_percent) {
  for (unsigned n : {3u, 4u, 6u, 8u}) {
    for (double speed : {0.25, 1.0, 3.0}) {
      Pipe p(n, 100 + n);
      const int32_t t = p.gesture(0.137, speed, 1.0, 30);
      CHECK(t >= 1024 - 31 && t <= 1024 + 31);
      CHECK_EQ(p.downs, 1);
      CHECK_EQ(p.ups, 1);
      CHECK_EQ(p.taps, 0);
    }
  }
}

EPDJ_TEST(ring_sweep_counter_clockwise_is_negative_and_jog_invert_flips_both) {
  for (unsigned n : {3u, 4u, 6u, 8u}) {
    Pipe ccw(n, 200 + n);
    const int32_t a = ccw.gesture(0.6, -1.0, 1.0, 30);
    CHECK(a <= -(1024 - 31) && a >= -(1024 + 31));

    Pipe inv(n, 200 + n, /*invert=*/true);
    const int32_t b = inv.gesture(0.6, 1.0, 1.0, 30);   // clockwise with jogInvert
    CHECK(b <= -(1024 - 31) && b >= -(1024 + 31));
    Pipe inv2(n, 300 + n, true);
    const int32_t c = inv2.gesture(0.6, -1.0, 1.0, 30);  // counter-clockwise with jogInvert
    CHECK(c >= 1024 - 31 && c <= 1024 + 31);
  }
}

EPDJ_TEST(ring_sweep_several_revolutions_and_partial_arcs_scale_linearly) {
  Pipe p(8, 7);
  const int32_t three = p.gesture(0.9, 1.5, 3.0, 30);
  CHECK(std::abs(three - 3072) <= 3072 * 3 / 100);
  Pipe q(8, 8);
  const int32_t quarter = q.gesture(0.3, 1.0, 0.25, 30);
  CHECK(std::abs(quarter - 256) <= 12);
  Pipe r(6, 9);
  const int32_t back = r.gesture(0.3, -2.0, 0.5, 30);
  CHECK(std::abs(back + 512) <= 16);
}

EPDJ_TEST(ring_finger_that_lands_already_moving_still_counts_nearly_everything) {
  for (double speed : {1.0, 2.0}) {
    Pipe p(8, 11);
    const int32_t t = p.gesture(0.2, speed, 1.0, /*dwellMs=*/0);
    CHECK(t >= 1024 - 31 && t <= 1024 + 31);
  }
}

EPDJ_TEST(ring_sweeps_are_unaffected_by_unequal_sector_sensitivity) {
  // Sensitivity differences between printed sectors distort the angle (and the gain learning is still
  // converging during the first laps) but the angle winds exactly once per revolution.
  const double g[8] = {1.0, 0.6, 0.9, 1.3, 0.5, 1.0, 0.8, 1.1};
  for (double start : {0.5, 0.1, 0.8}) {
    Pipe p(8, 12);
    for (int i = 0; i < 8; ++i) p.sim.gain[i] = g[i];
    const int32_t t = p.gesture(start, 1.0, 1.0, 30);
    CHECK(t >= 1024 - 31 && t <= 1024 + 31);
    const int32_t u = p.gesture(start, -1.0, 2.0, 30);  // and two laps back, now with learned gains
    CHECK(std::abs(u + 2048) <= 62);
  }
}

EPDJ_TEST(ring_no_jump_on_touch_down_at_arbitrary_angles) {
  // Landing at a random angle, with an instant or a 4 / 10 ms press, then resting for 200 ms: the angle
  // reference is taken from the footprint itself, so nothing the tracker reports may look like motion.
  // What remains is sensor noise that scales with ring coarseness (measured over 48 trials per row:
  // +/-0.15% -> 4/4/3/1 ticks for 3/4/6/8 sectors, +/-0.3% -> 8/7/6/3 ticks). Bounds sit just above.
  struct Case {
    unsigned sectors;
    double noisePermille;
    int maxTotal;
    int maxPerTick;
  };
  for (const Case c : {Case{8, 1.5, 3, 2}, Case{6, 1.5, 4, 2}, Case{4, 1.5, 5, 4}, Case{3, 1.5, 5, 5},
                       Case{8, 3.0, 5, 3}, Case{6, 3.0, 8, 5}, Case{4, 3.0, 9, 7}, Case{3, 3.0, 10, 8}}) {
    Rng rng(5);
    for (int trial = 0; trial < 36; ++trial) {
      Pipe p(c.sectors, 400 + trial, false, 180, 8, c.noisePermille, 3000);
      const double at = rng.uni();
      const double rampMs = (trial % 3 == 0) ? 0.0 : (trial % 3 == 1 ? 4.0 : 10.0);
      for (int ms = 0; ms < 200; ++ms) {
        const double amp = rampMs == 0.0 ? 1.0 : std::min(1.0, (ms + 1) / rampMs);
        p.tick(at, amp);
      }
      CHECK(p.lastRing.touched);
      CHECK(std::abs(p.ticks) <= c.maxTotal);
      int worst = 0;
      for (int32_t v : p.perTick) worst = std::max(worst, std::abs(v));
      CHECK(worst <= c.maxPerTick);
    }
  }
}

EPDJ_TEST(ring_resting_finger_is_silent_at_default_noise) {
  for (double pos : {0.0, 0.5 / 8, 0.33, 0.71}) {
    Pipe p(8, 13, false, 180, 8, 3.0, 3000);
    p.hold(pos, 100);
    const int32_t before = p.ticks;
    p.hold(pos, 5000);
    CHECK(std::abs(p.ticks - before) <= 2);
  }
}

EPDJ_TEST(ring_slow_drift_of_the_finger_is_followed_without_stalls) {
  // 0.05 rev/s = 51 ticks/s: slower than the noise band but must still come out.
  Pipe p(8, 14);
  const int32_t t = p.gesture(0.2, 0.05, 0.25, 30, 400);
  CHECK(std::abs(t - 256) <= 20);
}

EPDJ_TEST(ring_direction_reversal_mid_touch_nets_out) {
  Pipe p(8, 15);
  p.hold(0.3, 40);
  for (int i = 1; i <= 300; ++i) p.tick(0.3 + 0.0015 * i, 1.0);   // forward 0.45 rev
  for (int i = 1; i <= 300; ++i) p.tick(0.75 - 0.0015 * i, 1.0);  // and back to the start
  p.hold(0.3, 300);
  CHECK(std::abs(p.ticks) <= 20);
}

EPDJ_TEST(ring_tap_with_a_still_finger_is_reported_and_a_long_press_is_not) {
  for (int holdMs : {60, 120, 170}) {
    Pipe p(8, 16);
    p.hold(0.4, holdMs);
    p.idle(100);
    CHECK_EQ(p.taps, 1);
    CHECK_EQ(p.downs, 1);
    CHECK_EQ(p.ups, 1);
  }
  for (int holdMs : {230, 400, 2000}) {
    Pipe p(8, 17);
    p.hold(0.4, holdMs);
    p.idle(100);
    CHECK_EQ(p.taps, 0);
    CHECK_EQ(p.ups, 1);
  }
}

EPDJ_TEST(ring_tap_rejected_when_the_finger_slides) {
  // Slides 51 ticks within 100 ms of touching. The first ~20 ms after touch-down go into the angle
  // reference and the rest of the filter's unsent remainder is below the noise band, so about 30 of
  // the 51 ticks are visible; what matters is that it is not a tap and the motion is reported.
  for (unsigned n : {6u, 8u}) {
    Pipe p(n, 18);
    p.hold(0.4, 20);
    for (int i = 1; i <= 100; ++i) p.tick(0.4 + 0.0005 * i, 1.0);
    p.idle(100);
    CHECK_EQ(p.taps, 0);
    CHECK(p.ticks > 20 && p.ticks <= 56);
  }
}

EPDJ_TEST(full_chain_to_relative_cc_conserves_motion_and_never_sends_64) {
  Pipe p(8, 19);
  JogEmitter em;
  int32_t decoded = 0;
  int32_t added = 0;
  uint64_t lastSend = 0;
  bool anySend = false;
  auto pump = [&]() {
    const int32_t t = p.perTick.back();
    em.add(t);
    added += t;
    uint8_t v;
    if (em.poll(p.nowUs, &v)) {
      CHECK(v != 64 && v >= 1 && v <= 127);
      decoded += static_cast<int>(v) - 64;
      if (anySend) CHECK(p.nowUs - lastSend >= 2000);
      lastSend = p.nowUs;
      anySend = true;
    }
  };
  p.hold(0.1, 30);
  for (int i = 0; i < 30; ++i) pump();
  for (int i = 1; i <= 700; ++i) {  // 3 rev/s forward
    p.tick(0.1 + 0.003 * i, 1.0);
    pump();
  }
  for (int i = 0; i < 400; ++i) {
    p.tick(0.1 + 0.003 * 700, 1.0);
    pump();
  }
  for (int guard = 0; guard < 10000 && em.pending() != 0; ++guard) {
    p.nowUs += 1000;
    uint8_t v;
    if (em.poll(p.nowUs, &v)) decoded += static_cast<int>(v) - 64;
  }
  CHECK_EQ(em.pending(), 0);
  CHECK_EQ(decoded, added);
  CHECK(added > 2000);
}

EPDJ_TEST(ring_validity_dropouts_shorter_than_3ms_keep_the_anchor) {
  // Finger moving at 2 ticks/ms (8 units/ms), angle briefly invalid with garbage in the middle.
  auto run = [](int gapMs) {
    Direct d(PlatterSource::TouchRing);
    uint16_t ang = 1000;
    for (int i = 0; i < 40; ++i) d.step(true, ang, true);   // anchored and past the warm-up
    const int32_t before = d.ticks;
    for (int i = 0; i < 60; ++i) {
      ang = wrap4096(ang + 8);
      const bool gap = i >= 20 && i < 20 + gapMs;
      d.step(true, gap ? static_cast<uint16_t>(3333) : ang, !gap);
    }
    for (int i = 0; i < 100; ++i) d.step(true, ang, true);  // let the tail drain
    return d.ticks - before;
  };
  CHECK(std::abs(run(0) - 120) <= 12);
  CHECK(std::abs(run(2) - 120) <= 12);    // bridged: the motion during the gap is still counted
  CHECK(run(10) < 120 - 20);              // longer dropout: re-anchored, the warm-up swallows motion
}

EPDJ_TEST(ring_noise_band_follows_the_reported_angle_sigma) {
  // Identical motion, different reported uncertainty: a wider band means a slower, quieter filter.
  auto emittedAfter = [](float sigma) {
    Direct d(PlatterSource::TouchRing);
    uint16_t ang = 500;
    for (int i = 0; i < 40; ++i) {
      JogTracker::Input in;
      in.touched = true;
      in.angleValid = true;
      in.angle = ang;
      in.angleSigma = sigma;
      d.jog.update(in, d.nowUs);
      d.nowUs += 1000;
    }
    int32_t t = 0;
    for (int i = 0; i < 12; ++i) {  // 12 ms at 2 ticks/ms
      ang = wrap4096(ang + 8);
      JogTracker::Input in;
      in.touched = true;
      in.angleValid = true;
      in.angle = ang;
      in.angleSigma = sigma;
      t += d.jog.update(in, d.nowUs).ticks;
      d.nowUs += 1000;
    }
    return t;
  };
  const int32_t quiet = emittedAfter(2.0f), noisy = emittedAfter(30.0f);
  CHECK(quiet > noisy + 4);
  CHECK(quiet >= 14);  // a quiet ring passes nearly all of the 24 ticks within 12 ms
}

EPDJ_TEST(ring_quiet_sensor_is_more_responsive_than_a_noisy_one_and_both_are_silent_at_rest) {
  struct Out {
    int32_t firstTenMs;
    int32_t stillTicks;
  };
  auto measure = [](double noisePermille) {
    Pipe p(8, 31, false, 180, 8, noisePermille, 4000);  // the ring measures that noise while idle
    p.hold(0.2, 100);
    const int32_t rest0 = p.ticks;
    p.hold(0.2, 3000);
    Out o;
    o.stillTicks = p.ticks - rest0;
    const int32_t t0 = p.ticks;
    for (int ms = 1; ms <= 10; ++ms) p.tick(0.2 + 0.003 * ms, 1.0);  // 3 rev/s
    o.firstTenMs = p.ticks - t0;
    return o;
  };
  const Out quiet = measure(0.8), noisy = measure(3.0);
  CHECK(std::abs(quiet.stillTicks) <= 1);
  CHECK(std::abs(noisy.stillTicks) <= 2);
  CHECK(quiet.firstTenMs > noisy.firstTenMs);
}

EPDJ_TEST(ring_ignores_nonsensical_angle_sigma_values) {
  // NaN / negative / absurd sigma from a buggy caller must behave like "unknown", not poison the filter.
  const float bad[] = {std::nanf(""), -5.0f, 1e30f, INFINITY, -INFINITY};
  for (float sigma : bad) {
    Direct d(PlatterSource::TouchRing);
    uint16_t ang = 700;
    auto feed = [&](uint16_t a) {
      JogTracker::Input in;
      in.touched = true;
      in.angleValid = true;
      in.angle = a;
      in.angleSigma = sigma;
      const auto out = d.jog.update(in, d.nowUs);
      d.nowUs += 1000;
      d.ticks += out.ticks;
    };
    for (int i = 0; i < 40; ++i) feed(ang);
    const int32_t before = d.ticks;
    for (int i = 0; i < 100; ++i) {
      ang = wrap4096(ang + 8);
      feed(ang);
    }
    for (int i = 0; i < 100; ++i) feed(ang);
    CHECK(std::abs((d.ticks - before) - 200) <= 12);
    CHECK(std::isfinite(d.jog.velocity()));
  }
}

// ---------------------------------------------------------------- AS5600 path

EPDJ_TEST(as5600_wraparound_conserves_ticks_exactly_over_thousands_of_laps) {
  for (int dir : {1, -1}) {
    Direct d(PlatterSource::As5600);
    int64_t angle = 3000;
    d.step(false, wrap4096(angle));  // anchor
    Rng rng(static_cast<uint32_t>(40 + dir));
    int64_t units = 0;
    // 2000 laps = 8,192,000 units, random step sizes up to +-1000 (a quarter turn) per sample.
    while (units * dir < 2000LL * 4096) {
      const int step = dir * rng.range(0, 1000);
      angle += step;
      units += step;
      d.step(false, wrap4096(angle));
    }
    // Everything the sensor moved is accounted for except the sub-tick remainder.
    CHECK(std::llabs(units - 4LL * d.ticks) < 4);
    CHECK_EQ(d.jog.total(), d.ticks);
  }
}

EPDJ_TEST(as5600_exact_lap_counts_both_directions) {
  for (int dir : {1, -1}) {
    Direct d(PlatterSource::As5600);
    int64_t angle = 4000;  // start near the wrap
    d.step(false, wrap4096(angle));
    for (int lap = 0; lap < 3000; ++lap)
      for (int k = 0; k < 64; ++k) {  // 64 units per sample = 16 ticks
        angle += dir * 64;
        d.step(false, wrap4096(angle));
      }
    CHECK_EQ(d.ticks, dir * 3000 * 1024);
  }
}

EPDJ_TEST(as5600_forward_then_back_returns_to_exactly_zero) {
  Direct d(PlatterSource::As5600);
  Rng rng(77);
  int64_t angle = 123;
  d.step(false, wrap4096(angle));
  std::vector<int> steps;
  for (int i = 0; i < 50000; ++i) steps.push_back(rng.range(-300, 300));
  for (int s : steps) {
    angle += s;
    d.step(false, wrap4096(angle));
  }
  for (size_t i = steps.size(); i-- > 0;) {
    angle -= steps[i];
    d.step(false, wrap4096(angle));
  }
  CHECK_EQ(d.ticks, 0);
}

EPDJ_TEST(as5600_sub_tick_motion_is_carried_not_dropped) {
  Direct d(PlatterSource::As5600);
  uint16_t angle = 100;
  d.step(false, angle);
  for (int i = 0; i < 4000; ++i) {  // 1 unit per sample = a quarter tick
    ++angle;
    d.step(false, angle);
  }
  CHECK_EQ(d.ticks, 1000);
}

EPDJ_TEST(as5600_reports_motion_when_untouched_and_touch_does_not_change_it) {
  Direct a(PlatterSource::As5600), b(PlatterSource::As5600);
  uint16_t ang = 0;
  a.step(false, ang);
  b.step(false, ang);
  for (int i = 0; i < 1000; ++i) {
    ang = wrap4096(ang + 8);
    a.step(false, ang);
    b.step(i >= 300 && i < 700, ang);
  }
  CHECK_EQ(a.ticks, 2000);
  CHECK_EQ(b.ticks, 2000);
  CHECK_EQ(b.downs, 1);
  CHECK_EQ(b.ups, 1);
}

EPDJ_TEST(as5600_invert_negates_and_total_follows) {
  Direct d(PlatterSource::As5600, /*invert=*/true);
  uint16_t ang = 10;
  d.step(false, ang);
  for (int i = 0; i < 512; ++i) {
    ang = wrap4096(ang + 4);
    d.step(false, ang);
  }
  CHECK_EQ(d.ticks, -512);
  CHECK_EQ(d.jog.total(), -512);
}

EPDJ_TEST(as5600_lsb_flicker_at_rest_is_silent) {
  Rng rng(3);
  for (int center : {0, 1, 2047, 4095}) {
    Direct d(PlatterSource::As5600);
    for (int i = 0; i < 10000; ++i) d.step(false, wrap4096(center + rng.range(-1, 1)));
    CHECK(std::abs(d.ticks) <= 1);
  }
  // Worst case: alternating between two adjacent codes straddling a tick boundary.
  Direct d(PlatterSource::As5600);
  for (int i = 0; i < 20000; ++i) d.step(false, wrap4096(1000 + (i & 1)));
  CHECK(std::abs(d.ticks) <= 1);
}

EPDJ_TEST(as5600_invalid_reads_re_anchor_instead_of_jumping) {
  Direct d(PlatterSource::As5600);
  d.step(false, 100);
  for (int i = 0; i < 10; ++i) d.step(false, wrap4096(100 + 8 * (i + 1)));
  const int32_t before = d.ticks;
  CHECK_EQ(before, 20);
  for (int i = 0; i < 5; ++i) d.step(false, 2000, /*valid=*/false);  // bus error: garbage angle
  d.step(false, 3000, true);                                         // first good read after: anchor only
  CHECK_EQ(d.ticks, before);
  d.step(false, 3008, true);
  CHECK_EQ(d.ticks, before + 2);
}

EPDJ_TEST(implausible_single_sample_jumps_are_rejected) {
  Direct d(PlatterSource::As5600);
  d.step(false, 0);
  d.step(false, 8);
  d.step(false, 2000);  // half a turn in 1 ms: glitch, not motion
  d.step(false, 2008);  // re-anchored: continues from here
  CHECK_EQ(d.ticks, 2 + 0 + 0 + 0 + 0);
  d.step(false, 2016);
  CHECK_EQ(d.ticks, 4);
}

EPDJ_TEST(velocity_estimate_tracks_speed_sign_and_decays) {
  for (bool inv : {false, true}) {
    Direct d(PlatterSource::As5600, inv);
    int64_t ang = 0;
    d.step(false, 0);
    for (int i = 0; i < 400; ++i) {  // 2 rev/s = 8.192 units/ms = 2048 ticks/s
      ang += 8;
      d.step(false, wrap4096(ang));
      if (i == 0) continue;
    }
    // 8 units/ms = 2 ticks/ms = 2000 ticks/s.
    const double expect = inv ? -2000.0 : 2000.0;
    CHECK_NEAR(d.jog.velocity(), expect, 100.0);
    for (int i = 0; i < 300; ++i) d.step(false, wrap4096(ang));
    CHECK(std::fabs(d.jog.velocity()) < 5.0);
  }
}

// ---------------------------------------------------------------- touch / tap / modes (scripted angles)

EPDJ_TEST(tap_duration_boundary_and_configurable_limit) {
  auto tapFor = [](int touchedMs, uint16_t maxMs) {
    Direct d(PlatterSource::TouchHoldOnly, false, maxMs);
    d.step(false, 0);
    for (int i = 0; i < touchedMs; ++i) d.step(true, 0);
    d.step(false, 0);
    return d.taps;
  };
  CHECK_EQ(tapFor(100, 180), 1);
  CHECK_EQ(tapFor(179, 180), 1);
  CHECK_EQ(tapFor(180, 180), 1);   // touch lasted exactly 180 ms: up-edge is 180 ms after the down-edge
  CHECK_EQ(tapFor(181, 180), 0);
  CHECK_EQ(tapFor(300, 180), 0);
  CHECK_EQ(tapFor(300, 500), 1);
  CHECK_EQ(tapFor(61, 60), 0);
  CHECK_EQ(tapFor(60, 60), 1);
}

EPDJ_TEST(tap_movement_limit_uses_excursion_from_touch_down) {
  auto tapFor = [](int ticksOut, int ticksBack, uint8_t limit) {
    Direct d(PlatterSource::As5600, false, 180, limit);
    uint16_t ang = 500;
    d.step(false, ang);
    d.step(true, ang);
    for (int i = 0; i < ticksOut; ++i) {
      ang = wrap4096(ang + 4);
      d.step(true, ang);
    }
    for (int i = 0; i < ticksBack; ++i) {
      ang = wrap4096(ang - 4);
      d.step(true, ang);
    }
    d.step(false, ang);
    return d.taps;
  };
  CHECK_EQ(tapFor(0, 0, 8), 1);
  CHECK_EQ(tapFor(8, 0, 8), 1);     // exactly at the limit
  CHECK_EQ(tapFor(9, 0, 8), 0);
  CHECK_EQ(tapFor(20, 20, 8), 0);   // out and back: net zero but a wide excursion is not a tap
  CHECK_EQ(tapFor(5, 5, 8), 1);
  CHECK_EQ(tapFor(0, 9, 8), 0);     // backwards counts too
  CHECK_EQ(tapFor(40, 0, 64), 1);
  CHECK_EQ(tapFor(1, 0, 0), 0);
  CHECK_EQ(tapFor(0, 0, 0), 1);
}

EPDJ_TEST(touch_edges_are_reported_once_each_for_every_source) {
  for (PlatterSource src : {PlatterSource::As5600, PlatterSource::TouchRing, PlatterSource::TouchHoldOnly}) {
    Direct d(src);
    d.step(false, 0, false);
    const auto a = d.step(true, 0, true);
    CHECK(a.touchDown && !a.touchUp);
    const auto b = d.step(true, 0, true);
    CHECK(!b.touchDown && !b.touchUp);
    const auto c = d.step(false, 0, true);
    CHECK(!c.touchDown && c.touchUp);
    CHECK_EQ(d.downs, 1);
    CHECK_EQ(d.ups, 1);
    CHECK(!d.jog.touched());
  }
}

EPDJ_TEST(touch_hold_only_never_produces_ticks) {
  Direct d(PlatterSource::TouchHoldOnly);
  uint16_t ang = 0;
  for (int i = 0; i < 2000; ++i) {
    ang = wrap4096(ang + 37);
    d.step(i % 500 < 300, ang);
  }
  CHECK_EQ(d.ticks, 0);
  CHECK_EQ(d.jog.total(), 0);
  CHECK(d.downs >= 3);
}

EPDJ_TEST(touch_ring_source_ignores_motion_while_untouched_and_anchors_on_touch_down) {
  Direct d(PlatterSource::TouchRing);
  uint16_t ang = 0;
  for (int i = 0; i < 100; ++i) {  // angle wanders while nobody is touching
    ang = wrap4096(ang + 40);
    d.step(false, ang, false);
  }
  CHECK_EQ(d.ticks, 0);
  d.step(true, 3000, true);         // lands somewhere else entirely
  for (int i = 0; i < 20; ++i) d.step(true, 3000, true);
  CHECK_EQ(d.ticks, 0);
  // A clean 1-revolution move in 4 ms steps of 16 units... after the anchor settle it is all counted.
  uint16_t a = 3000;
  for (int i = 0; i < 256; ++i) {
    a = wrap4096(a + 16);
    d.step(true, a, true);
  }
  CHECK(std::abs(d.ticks - 1024) <= 24);  // the smoothing tail is still in flight
  for (int i = 0; i < 400; ++i) d.step(true, a, true);
  CHECK(std::abs(d.ticks - 1024) <= 2);
}

EPDJ_TEST(reconfiguring_the_source_resets_but_invert_is_live) {
  Direct d(PlatterSource::As5600);
  d.step(false, 0);
  for (int i = 1; i <= 100; ++i) d.step(false, static_cast<uint16_t>(i * 8));
  CHECK_EQ(d.ticks, 200);
  JogTracker::Config c;
  c.source = PlatterSource::As5600;
  c.invert = true;
  d.jog.configure(c);
  for (int i = 101; i <= 200; ++i) d.step(false, static_cast<uint16_t>(i * 8));
  CHECK_EQ(d.ticks, 0);  // second hundred steps count negative
  c.source = PlatterSource::TouchHoldOnly;
  d.jog.configure(c);
  CHECK_EQ(d.jog.total(), 0);
}

// ---------------------------------------------------------------- emitter

EPDJ_TEST(emitter_encodes_offset_64_and_clamps_to_63) {
  JogEmitter e;
  uint8_t v = 0;
  CHECK(!e.poll(0, &v));  // nothing to say
  e.add(5);
  CHECK(e.poll(0, &v));
  CHECK_EQ(v, 69);
  e.add(-5);
  CHECK(!e.poll(1999, &v));  // rate limited
  CHECK(e.poll(2000, &v));
  CHECK_EQ(v, 59);
  e.add(200);
  CHECK(e.poll(4000, &v));
  CHECK_EQ(v, 127);
  CHECK_EQ(e.pending(), 137);
  CHECK(e.poll(6000, &v));
  CHECK_EQ(v, 127);
  CHECK(e.poll(8000, &v));
  CHECK_EQ(v, 127);
  CHECK_EQ(e.pending(), 11);
  CHECK(e.poll(10000, &v));
  CHECK_EQ(v, 75);
  CHECK(!e.poll(12000, &v));
  e.add(-1000);
  CHECK(e.poll(14000, &v));
  CHECK_EQ(v, 1);
}

EPDJ_TEST(emitter_never_sends_64_and_never_faster_than_500_hz) {
  JogEmitter e;
  uint8_t v = 0;
  uint64_t last = 0;
  bool any = false;
  int sends = 0;
  for (uint64_t t = 0; t < 1000000; t += 100) {
    // Keep it saturated (bursts far above 63 ticks every 20 ms, both signs) so the only thing that
    // can space the messages out is the 2 ms limit.
    if (t % 20000 == 0) e.add((t / 20000) % 2 ? -1500 : 1500);
    if (e.poll(t, &v)) {
      CHECK(v != 64);
      CHECK(v >= 1 && v <= 127);
      if (any) CHECK(t - last >= 2000);
      last = t;
      any = true;
      ++sends;
    }
  }
  CHECK(sends >= 480);  // ~500 Hz sustained: 1 s of busy time
  CHECK(sends <= 500);
}

EPDJ_TEST(emitter_first_motion_after_idle_goes_out_immediately) {
  JogEmitter e;
  uint8_t v = 0;
  e.add(2);
  CHECK(e.poll(5, &v));
  CHECK(!e.poll(6, &v));
  CHECK(!e.poll(1000000, &v));  // nothing pending: no message, and no schedule penalty
  e.add(3);
  CHECK(e.poll(1000001, &v));
  CHECK_EQ(v, 67);
}

EPDJ_TEST(emitter_clamp_and_carry_conserves_the_exact_total_for_random_bursts) {
  for (uint32_t seed : {1u, 2u, 3u, 4u}) {
    Rng rng(seed);
    JogEmitter e;
    int64_t added = 0, sent = 0;
    uint64_t t = 0;
    uint8_t v;
    for (int i = 0; i < 200000; ++i) {
      t += 1000;
      if (rng.next() % 10 == 0) {
        const int burst = (rng.next() % 5 == 0) ? rng.range(-900, 900) : rng.range(-20, 20);
        e.add(burst);
        added += burst;
      }
      if (e.poll(t, &v)) {
        CHECK(v != 64);
        sent += static_cast<int>(v) - 64;
      }
    }
    for (int guard = 0; guard < 100000 && e.pending() != 0; ++guard) {
      t += 2000;
      if (e.poll(t, &v)) sent += static_cast<int>(v) - 64;
    }
    CHECK_EQ(e.pending(), 0);
    CHECK_EQ(sent, added);
  }
}

EPDJ_TEST(emitter_accumulator_saturates_instead_of_wrapping) {
  JogEmitter e;
  e.add(INT32_MAX);
  e.add(1000);
  CHECK_EQ(e.pending(), INT32_MAX);
  e.clear();
  CHECK_EQ(e.pending(), 0);
  e.add(INT32_MIN);
  e.add(-1000);
  CHECK_EQ(e.pending(), INT32_MIN);
}

// ---- StickyTouch: the motion-sticky release of the hold pad -----------------------------------------
//
// The driver below feeds one sample per millisecond. `moving` is whether the platter made a jog tick in that sample.
// Time starts at 1 s: the unit never sees t = 0.

namespace {

struct StickyRun {
  StickyTouch s;
  uint64_t t = 1000000;
  int downs = 0, ups = 0;  // edges of the verdict the tracker would see
  bool last = false;

  // One millisecond. Returns the verdict.
  bool ms(bool pad, bool moving) {
    const bool v = s.update(pad, t);
    s.noteMotion(moving, t);
    if (v && !last) ++downs;
    if (!v && last) ++ups;
    last = v;
    t += 1000;
    return v;
  }
  void run(int n, bool pad, bool moving) {
    for (int i = 0; i < n; ++i) ms(pad, moving);
  }
};

constexpr int kAfter = static_cast<int>(StickyTouch::kStickyAfterMs);
constexpr int kMotion = static_cast<int>(StickyTouch::kStickyMotionMs);

}  // namespace

EPDJ_TEST(sticky_constants_are_the_originals) {
  CHECK_EQ(StickyTouch::kStickyAfterMs, 7000u);
  CHECK_EQ(StickyTouch::kStickyMotionMs, 80u);
}

EPDJ_TEST(sticky_a_short_touch_releases_at_once_even_while_the_platter_moves) {
  StickyRun r;
  r.run(3000, true, true);  // a 3 s scratch
  CHECK(r.last);
  CHECK(!r.ms(false, true));  // the pad says released, the platter is moving: a short touch does not care
  CHECK_EQ(r.downs, 1);
  CHECK_EQ(r.ups, 1);
  // and the very first sample after a touch of any short length behaves the same
  StickyRun q;
  q.ms(true, true);
  CHECK(!q.ms(false, true));
}

EPDJ_TEST(sticky_a_long_touch_rides_out_a_pad_dropout_while_the_platter_moves) {
  StickyRun r;
  r.run(kAfter + 500, true, true);  // 7.5 s of scratching
  r.run(40, false, true);           // the pad's signal drops out for 40 ms; the platter does not stop
  CHECK(r.last);
  CHECK_EQ(r.ups, 0);
  r.run(100, true, true);  // the pad comes back: still the same touch, no new touch-down
  CHECK(r.last);
  CHECK_EQ(r.downs, 1);
  CHECK_EQ(r.ups, 0);
  r.run(500, false, true);  // a pad that stays gone for half a second while the platter keeps turning: still held, however long
  CHECK(r.last);
  CHECK_EQ(r.ups, 0);
}

EPDJ_TEST(sticky_a_long_touch_released_while_still_releases_at_once) {
  StickyRun r;
  r.run(kAfter + 500, true, true);
  r.run(kMotion + 5, true, false);  // the hand holds the platter still
  CHECK(r.last);
  CHECK(!r.ms(false, false));  // released the very sample the pad says so
  CHECK_EQ(r.ups, 1);
  CHECK_EQ(r.downs, 1);
}

EPDJ_TEST(sticky_after_a_long_touch_the_release_waits_for_exactly_the_motion_window) {
  // The pad has said "released" and the platter stops: the verdict flips on the sample where the platter has been
  // still for kStickyMotionMs, not one before.
  StickyRun r;
  r.run(kAfter + 500, true, true);
  r.run(10, false, true);  // pad gone, platter still turning (the last tick is the sample just run)
  int still = 0;           // samples since the last tick
  bool released = false;
  for (int i = 1; i <= 200 && !released; ++i) {
    still = i;
    released = !r.ms(false, false);
  }
  // The tick was in the sample 1 ms before the first still sample, so stillness reaches the window when `still` samples
  // have passed after it: the release comes at still = kMotion (the update sees the motion up to the previous sample).
  CHECK(released);
  CHECK_EQ(still, kMotion);
  CHECK_EQ(r.ups, 1);

  // A tick inside the window restarts it.
  StickyRun q;
  q.run(kAfter + 500, true, true);
  q.run(kMotion - 5, false, false);
  CHECK(q.last);
  q.ms(false, true);  // the platter twitches
  q.run(kMotion - 2, false, false);
  CHECK(q.last);  // not yet: the window started again at the twitch
  q.run(3, false, false);
  CHECK(!q.last);
}

EPDJ_TEST(sticky_the_long_touch_boundary_is_strictly_longer_than_kStickyAfterMs) {
  // Touch down at the first sample the pad says touched; "lasted" is measured from there to the sample that says released.
  {
    StickyRun r;
    r.ms(true, true);                   // t0
    r.run(kAfter - 1, true, true);      // up to t0 + kAfter - 1 ms
    CHECK(!r.ms(false, true));          // t0 + kAfter ms exactly: NOT longer, so it releases although the platter moves
  }
  {
    StickyRun r;
    r.ms(true, true);
    r.run(kAfter, true, true);          // up to t0 + kAfter ms
    CHECK(r.ms(false, true));           // t0 + kAfter + 1 ms: longer, and the platter moves, so it is held
    CHECK(r.ms(false, true));
    CHECK_EQ(r.ups, 0);
  }
  {
    StickyRun r;
    r.ms(true, false);
    r.run(kAfter, true, false);
    CHECK(!r.ms(false, false));         // longer, but the platter is still: released at once
  }
}

EPDJ_TEST(sticky_each_touch_is_timed_from_its_own_start) {
  StickyRun r;
  r.run(kAfter + 500, true, true);
  r.run(kMotion + 5, false, false);  // released after a long touch
  CHECK(!r.last);
  r.run(100, true, true);            // a new touch, 100 ms, moving
  CHECK(!r.ms(false, true));         // short: the previous touch's length does not carry over
  CHECK_EQ(r.downs, 2);
  CHECK_EQ(r.ups, 2);
}

EPDJ_TEST(sticky_reset_forgets_a_touch_and_an_untouched_pad_is_never_sticky) {
  StickyRun r;
  r.run(kAfter + 500, true, true);
  r.s.reset();
  CHECK(!r.ms(false, true));  // reset: no touch to hold
  StickyRun q;
  for (int i = 0; i < 20000; ++i) CHECK(!q.ms(false, true));  // untouched pad + endless motion: never reports a touch
}
