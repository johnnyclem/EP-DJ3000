#include <cmath>
#include <vector>

#include "epdj/touch_ring.h"
#include "epdj/util.h"
#include "test_harness.h"

using namespace epdj;

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
};

// Synthetic ring. A finger is an arc of `widthSectors` sectors with soft edges; each sector's rise is
// (fraction of the sector under the finger) x peakPermille x its own gain. Readings carry uniform
// noise of +/- noisePermille of baseline (the spec's 0.1-0.3%) and are quantised to counts.
struct RingSim {
  unsigned n;
  double baseline[8];
  double gain[8];
  double peakPermille = 40.0;
  double widthSectors = 1.5;
  double noisePermille = 3.0;

  explicit RingSim(unsigned sectors) : n(sectors) {
    for (unsigned i = 0; i < 8; ++i) {
      baseline[i] = 30000.0 + 900.0 * i;  // sectors differ in idle capacitance too
      gain[i] = 1.0;
    }
  }

  static double wrapped(double d) {
    d -= std::floor(d + 0.5);
    return d;
  }

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

  // amplitude 0..1 scales the whole finger (0 = no finger). extraPermille[i] is added per sector (drift).
  void read(double fingerRev, double amplitude, const double* extraPermille, Rng& rng, uint16_t* out) const {
    for (unsigned i = 0; i < n; ++i) {
      double rise = extraPermille ? extraPermille[i] : 0.0;
      if (amplitude > 0.0) rise += amplitude * peakPermille * gain[i] * coverage(i, fingerRev);
      rise += noisePermille * rng.sym();
      const double v = baseline[i] * (1.0 + rise / 1000.0);
      out[i] = static_cast<uint16_t>(std::lround(v < 0.0 ? 0.0 : (v > 65535.0 ? 65535.0 : v)));
    }
  }
};

struct Run {
  TouchRing ring;
  RingSim sim;
  Rng rng;
  uint64_t nowUs = 0;
  TouchRing::Result last;

  Run(unsigned n, uint32_t seed, TouchRing::Config cfg = {}) : sim(n), rng(seed) {
    cfg.sectors = static_cast<uint8_t>(n);
    ring.configure(cfg);
  }
  TouchRing::Result tick(double fingerRev, double amplitude, const double* extra = nullptr) {
    uint16_t raw[8];
    sim.read(fingerRev, amplitude, extra, rng, raw);
    last = ring.update(raw, nowUs);
    nowUs += 1000;
    return last;
  }
  void idle(int ms) {
    for (int i = 0; i < ms; ++i) tick(0, 0);
  }
  void hold(double fingerRev, int ms, double amplitude = 1.0) {
    for (int i = 0; i < ms; ++i) tick(fingerRev, amplitude);
  }
};

// Circular error in angle units between estimate and truth (truth given in revolutions).
double angleErr(uint16_t est, double trueRev) {
  double d = est - trueRev * 4096.0;
  d -= 4096.0 * std::floor(d / 4096.0 + 0.5);
  return d;
}

// Static accuracy sweep: settle the finger at `steps` positions, collect the error of the estimate.
struct Sweep {
  double maxAbsErr = 0.0;
  double rmsErr = 0.0;
  double worstBacktrack = 0.0;  // largest decrease of the estimate while the finger moves forward
  unsigned invalid = 0;
};
Sweep staticSweep(Run& r, int steps, double startRev = 0.0) {
  Sweep out;
  double sumSq = 0.0;
  double prevUnwrapped = 0.0;
  bool first = true;
  uint16_t prev = 0;
  for (int k = 0; k < steps; ++k) {
    const double rev = startRev + static_cast<double>(k) / steps;
    r.hold(rev, 25);
    if (!r.last.angleValid) {
      ++out.invalid;
      continue;
    }
    const double e = angleErr(r.last.angle, rev);
    out.maxAbsErr = std::max(out.maxAbsErr, std::fabs(e));
    sumSq += e * e;
    double un = prevUnwrapped;
    if (!first) un += angleDiff4096(prev, r.last.angle);
    if (!first && un < prevUnwrapped) out.worstBacktrack = std::max(out.worstBacktrack, prevUnwrapped - un);
    prevUnwrapped = un;
    prev = r.last.angle;
    first = false;
  }
  out.rmsErr = std::sqrt(sumSq / steps);
  return out;
}

}  // namespace

EPDJ_TEST(ring_is_not_ready_during_power_up_learning) {
  Run r(8, 1);
  CHECK(!r.tick(0, 0).ready);
  r.idle(200);
  CHECK(!r.last.ready);
  r.idle(100);
  CHECK(r.last.ready);
  // A finger during the learn window is never a touch.
  Run q(8, 2);
  for (int i = 0; i < 240; ++i) CHECK(!q.tick(0.0, 1.0).touched);
}

EPDJ_TEST(ring_touch_hysteresis_thresholds) {
  // Noise-free, one sector, baseline 30000 so 1 permille = 30 counts. The 0.4 permille margins cover
  // the baseline creeping towards a held sub-threshold level (5 s time constant).
  TouchRing ring;
  uint64_t t = 0;
  uint16_t raw[8];
  auto step = [&](double rise, int ms = 30) {
    for (int i = 0; i < ms; ++i) {
      for (int s = 0; s < 8; ++s) raw[s] = 30000;
      raw[2] = static_cast<uint16_t>(std::lround(30000.0 * (1.0 + rise / 1000.0)));
      ring.update(raw, t);
      t += 1000;
    }
    return ring.touched();
  };
  step(0, 3500);       // past the power-up settle window
  CHECK(!step(11.6));  // just below on
  CHECK(step(12.4));   // just above on
  CHECK(step(10));     // inside the hysteresis band: stays touched
  CHECK(step(7.4));    // just above off: still touched
  CHECK(!step(6.6));   // just below off
  CHECK(!step(10));    // inside the band again: stays released
  CHECK(!step(11.6));
  CHECK(step(13));
  CHECK(!step(0));
}

EPDJ_TEST(ring_thresholds_follow_live_configuration) {
  TouchRing ring;
  TouchRing::Config c;
  c.onPermille = 40;
  c.offPermille = 25;
  ring.configure(c);
  uint64_t t = 0;
  uint16_t raw[8];
  auto step = [&](double rise, int ms = 30) {
    for (int i = 0; i < ms; ++i) {
      for (int s = 0; s < 8; ++s) raw[s] = 20000;
      raw[5] = static_cast<uint16_t>(std::lround(20000.0 * (1.0 + rise / 1000.0)));
      ring.update(raw, t);
      t += 1000;
    }
    return ring.touched();
  };
  step(0, 3500);
  CHECK(!step(39));
  CHECK(step(41));
  CHECK(step(30));
  CHECK(!step(24));
  // off > on is clamped so the state machine cannot invert: with on=20 the effective off is 20.
  c.onPermille = 20;
  c.offPermille = 50;
  ring.configure(c);
  CHECK(step(25));
  CHECK(!step(15));
}

EPDJ_TEST(ring_touch_and_release_latency_is_a_few_milliseconds) {
  Run r(8, 3);
  r.idle(500);
  int downMs = -1;
  for (int ms = 0; ms < 20; ++ms)
    if (r.tick(0.3, 1.0).touched) {
      downMs = ms;
      break;
    }
  CHECK(downMs >= 0 && downMs <= 4);
  r.hold(0.3, 50);
  int upMs = -1;
  for (int ms = 0; ms < 40; ++ms)
    if (!r.tick(0, 0).touched) {
      upMs = ms;
      break;
    }
  CHECK(upMs >= 0 && upMs <= 8);
}

EPDJ_TEST(ring_noise_alone_never_touches_and_isolated_spikes_are_ignored) {
  Run r(8, 4);
  r.idle(400);
  for (int i = 0; i < 100000; ++i) {
    uint16_t raw[8];
    r.sim.read(0, 0, nullptr, r.rng, raw);
    if (i % 7 == 0) {  // isolated one-sample spike of +25% (a given sector is hit every 56 samples)
      const unsigned s = static_cast<unsigned>((i / 7) % 8);
      raw[s] = static_cast<uint16_t>(raw[s] * 1.25);
    }
    const auto res = r.ring.update(raw, r.nowUs);
    r.nowUs += 1000;
    CHECK(!res.touched);
  }
}

EPDJ_TEST(ring_slow_thermal_drift_does_not_cause_false_touches) {
  for (double rate : {0.5, 1.0}) {  // permille per second, i.e. up to 6% per minute
    Run r(8, 5);
    r.idle(500);
    double extra[8];
    const double coeff[8] = {1.0, 0.6, 0.9, 0.3, 1.1, 0.7, 0.5, 0.8};
    double worstPeak = 0.0;
    int touches = 0;
    // 60 s up then 60 s back down (heating then cooling).
    for (int ms = 0; ms < 120000; ++ms) {
      const double t = ms * 1e-3;
      const double ramp = t < 60.0 ? t : 120.0 - t;
      for (int i = 0; i < 8; ++i) extra[i] = rate * ramp * coeff[i];
      const auto res = r.tick(0, 0, extra);
      touches += res.touched;
      worstPeak = std::max(worstPeak, static_cast<double>(res.peakPermille));
    }
    CHECK_EQ(touches, 0);
    // The baseline lags a ramp by rate x 5 s, so the residual stays under the touch threshold (12) with
    // margin up to ~1.5 permille/s; at 0.5 it stays under the release threshold too.
    CHECK(worstPeak < (rate < 0.75 ? 7.0 : 10.0));
    // The ring still works afterwards.
    r.idle(200);
    int downMs = -1;
    for (int ms = 0; ms < 20 && downMs < 0; ++ms)
      if (r.tick(0.6, 1.0, extra).touched) downMs = ms;
    CHECK(downMs >= 0 && downMs <= 4);
  }
}

EPDJ_TEST(ring_finger_resting_at_power_up_relearns_after_lift) {
  Run r(8, 6);
  // Finger on sector 3 from the very first sample, for 1.5 s.
  int touchedWhileResting = 0;
  for (int ms = 0; ms < 1500; ++ms) touchedWhileResting += r.tick(3.0 / 8, 1.0).touched;
  CHECK_EQ(touchedWhileResting, 0);  // indistinguishable from idle: documented limitation
  // Lifted: the contaminated baseline drains quickly and nothing ever looks like a touch.
  int touchedAfterLift = 0;
  for (int ms = 0; ms < 2000; ++ms) touchedAfterLift += r.tick(0, 0).touched;
  CHECK_EQ(touchedAfterLift, 0);
  // Baseline of sector 3 is back near the true idle level, within the noise band.
  CHECK(std::fabs(r.ring.baseline(3) - r.sim.baseline[3]) < 0.004 * r.sim.baseline[3]);
  // And the same sector works as a normal sensor again.
  int downMs = -1;
  for (int ms = 0; ms < 20 && downMs < 0; ++ms)
    if (r.tick(3.0 / 8, 1.0).touched) downMs = ms;
  CHECK(downMs >= 0 && downMs <= 4);
}

EPDJ_TEST(ring_brief_finger_inside_the_learn_window_is_harmless) {
  Run r(8, 7);
  r.hold(0.5, 100);  // 100 ms of finger, then gone before learning ends
  int touched = 0;
  for (int ms = 0; ms < 3000; ++ms) touched += r.tick(0, 0).touched;
  CHECK_EQ(touched, 0);
  CHECK(std::fabs(r.ring.baseline(4) - r.sim.baseline[4]) < 0.004 * r.sim.baseline[4]);
}

EPDJ_TEST(ring_power_up_transient_is_absorbed_not_read_as_a_finger) {
  Run r(8, 8);
  double extra[8];
  int touched = 0;
  for (int ms = 0; ms < 6000; ++ms) {
    // Readings start 2% low and climb back with a 0.5 s time constant.
    for (int i = 0; i < 8; ++i) extra[i] = -20.0 * std::exp(-ms * 1e-3 / 0.5);
    touched += r.tick(0, 0, extra).touched;
  }
  CHECK_EQ(touched, 0);
  int downMs = -1;
  for (int ms = 0; ms < 20 && downMs < 0; ++ms)
    if (r.tick(0.1, 1.0).touched) downMs = ms;
  CHECK(downMs >= 0 && downMs <= 4);
}

EPDJ_TEST(ring_baseline_is_frozen_while_touched) {
  Run r(8, 9);
  r.idle(3500);
  r.hold(2.0 / 8, 20);
  CHECK(r.last.touched);
  const float before = r.ring.baseline(2);
  r.hold(2.0 / 8, 30000);
  CHECK(r.last.touched);
  CHECK(std::fabs(r.ring.baseline(2) - before) < 0.5);
  r.idle(50);
  CHECK(!r.last.touched);
}

EPDJ_TEST(ring_stuck_touch_is_relearned_after_the_hold_bound) {
  TouchRing ring;
  uint64_t t = 0;
  uint16_t raw[8];
  auto run = [&](bool finger, int ms) {
    for (int i = 0; i < ms; ++i) {
      for (int s = 0; s < 8; ++s) raw[s] = 30000;
      if (finger) raw[1] = 31500;
      ring.update(raw, t);
      t += 1000;
    }
    return ring.touched();
  };
  run(false, 500);
  CHECK(run(true, 100000));   // still a touch at 100 s
  CHECK(!run(true, 25000));   // relearned by 125 s: baseline now contains the finger
  CHECK(!run(true, 1000));
  CHECK(!run(false, 100));    // lifting never produces a false touch
  CHECK(run(true, 50));       // and the next touch is a normal one
}

EPDJ_TEST(ring_confidence_distinguishes_finger_from_palm) {
  Run r(8, 10);
  r.idle(500);
  r.hold(0.4, 40);
  CHECK(r.last.touched && r.last.angleValid);
  const uint8_t fingerConfidence = r.last.confidence;
  CHECK(fingerConfidence > 150);
  // Everything lit up equally (palm / common-mode shift): touched, but there is no angle to trust.
  double all[8];
  for (int i = 0; i < 8; ++i) all[i] = 35.0;
  for (int ms = 0; ms < 40; ++ms) r.tick(0, 0, all);
  CHECK(r.last.touched);
  CHECK(!r.last.angleValid);
  CHECK(r.last.confidence < 32);
  // A feather-light touch just above the threshold is still located but with low confidence.
  Run q(8, 11);
  q.idle(500);
  q.sim.peakPermille = 14.0;
  q.hold(0.4, 40);
  CHECK(q.last.touched);
  CHECK(q.last.confidence < fingerConfidence);
}

EPDJ_TEST(ring_angle_convention_is_clockwise_from_twelve_oclock) {
  for (unsigned n : {4u, 8u}) {
    Run r(n, 12);
    r.idle(500);
    r.hold(0.0, 40);
    CHECK(angleErr(r.last.angle, 0.0) < 20 && angleErr(r.last.angle, 0.0) > -20);
    r.hold(0.25, 40);  // 3 o'clock
    CHECK(std::fabs(angleErr(r.last.angle, 0.25)) < 20);
    r.hold(0.5, 40);   // 6 o'clock
    CHECK(std::fabs(angleErr(r.last.angle, 0.5)) < 20);
    r.hold(0.75, 40);  // 9 o'clock
    CHECK(std::fabs(angleErr(r.last.angle, 0.75)) < 20);
  }
}

EPDJ_TEST(ring_angle_is_monotonic_and_accurate_for_3_4_6_8_sectors) {
  // Noise-free: the systematic error of the phasor estimator for a 1.5-sector finger.
  for (unsigned n : {3u, 4u, 6u, 8u}) {
    Run r(n, 20 + n);
    r.sim.noisePermille = 0.0;
    r.idle(500);
    const Sweep s = staticSweep(r, 256);
    const double pitch = 4096.0 / n;
    CHECK_EQ(s.invalid, 0u);
    CHECK(s.worstBacktrack <= 2.0);       // monotonic
    CHECK(s.maxAbsErr <= 0.10 * pitch);
    CHECK(s.rmsErr <= 0.06 * pitch);
  }
}

EPDJ_TEST(ring_angle_with_adc_noise_stays_within_a_fraction_of_a_sector) {
  for (unsigned n : {3u, 4u, 6u, 8u}) {
    Run r(n, 40 + n);  // +/-0.3% noise, 4% finger signal
    r.idle(500);
    const Sweep s = staticSweep(r, 128);
    const double pitch = 4096.0 / n;
    CHECK_EQ(s.invalid, 0u);
    CHECK(s.maxAbsErr <= 0.22 * pitch);
    CHECK(s.rmsErr <= 0.08 * pitch);
  }
}

EPDJ_TEST(ring_still_finger_angle_jitter_is_bounded) {
  // This is the noise the jog filter has to absorb: 8 sectors, 4% signal, +/-0.3% ADC noise.
  for (double pos : {0.0, 0.5 / 8, 0.33}) {
    Run r(8, 60);
    r.idle(500);
    r.hold(pos, 50);
    const uint16_t ref = r.last.angle;
    double sum = 0.0, sq = 0.0;
    for (int i = 0; i < 5000; ++i) {
      r.tick(pos, 1.0);
      CHECK(r.last.angleValid);
      const double e = angleDiff4096(ref, r.last.angle);
      sum += e;
      sq += e * e;
    }
    const double mean = sum / 5000, sd = std::sqrt(sq / 5000 - mean * mean);
    CHECK(sd < 16.0);  // < 4 ticks rms
  }
}

EPDJ_TEST(ring_unequal_sector_gains_are_learned_and_equalised) {
  const double g[8] = {1.0, 0.55, 0.9, 1.3, 0.5, 1.0, 0.75, 1.1};
  auto setup = [&](Run& r) {
    for (int i = 0; i < 8; ++i) r.sim.gain[i] = g[i];
  };
  // Training: 3 slow clockwise laps (0.5 rev/s).
  Run eq(8, 30);
  setup(eq);
  eq.idle(500);
  for (int ms = 0; ms < 6000; ++ms) eq.tick(std::fmod(ms * 0.5e-3, 1.0), 1.0);
  Run flat(8, 30, [] {
    TouchRing::Config c;
    c.equalizeGains = false;
    return c;
  }());
  setup(flat);
  flat.idle(500);
  for (int ms = 0; ms < 6000; ++ms) flat.tick(std::fmod(ms * 0.5e-3, 1.0), 1.0);

  // Learned gain x true sensitivity should be about the same for all sectors.
  double lo = 1e9, hi = 0.0;
  for (int i = 0; i < 8; ++i) {
    const double eff = eq.ring.gain(i) * g[i];
    lo = std::min(lo, eff);
    hi = std::max(hi, eff);
  }
  CHECK(hi / lo < 1.2);
  for (int i = 0; i < 8; ++i) CHECK_NEAR(flat.ring.gain(i), 1.0, 1e-9);

  const Sweep a = staticSweep(eq, 128);
  const Sweep b = staticSweep(flat, 128);
  CHECK_EQ(a.invalid, 0u);
  const double pitch = 4096.0 / 8;
  CHECK(a.maxAbsErr <= 0.25 * pitch);
  CHECK(a.rmsErr < 0.6 * b.rmsErr);       // equalisation must clearly help
  CHECK(a.maxAbsErr < b.maxAbsErr);
}

EPDJ_TEST(ring_gains_stay_neutral_with_equal_sectors) {
  Run r(8, 31);
  r.idle(500);
  for (int ms = 0; ms < 8000; ++ms) r.tick(std::fmod(ms * 0.5e-3, 1.0), 1.0);
  for (int i = 0; i < 8; ++i) CHECK(r.ring.gain(i) > 0.85f && r.ring.gain(i) < 1.18f);
}

EPDJ_TEST(ring_reconfigure_keeps_state_unless_the_sector_count_changes) {
  Run r(8, 32);
  r.idle(600);
  CHECK(r.last.ready);
  TouchRing::Config c;
  c.sectors = 8;
  c.onPermille = 20;
  r.ring.configure(c);
  r.idle(5);
  CHECK(r.last.ready);  // thresholds only: still ready
  c.sectors = 6;
  r.ring.configure(c);
  uint16_t raw[8] = {30000, 30000, 30000, 30000, 30000, 30000, 0, 0};
  CHECK(!r.ring.update(raw, r.nowUs).ready);  // layout changed: relearn
  // Out-of-range counts are clamped, not trusted.
  c.sectors = 20;
  r.ring.configure(c);
  c.sectors = 1;
  r.ring.configure(c);
}

EPDJ_TEST(ring_reported_angle_sigma_tracks_noise_and_position) {
  auto sigmaAt = [](double noise, double pos) {
    Run r(8, 91);
    r.sim.noisePermille = noise;
    r.idle(4000);  // lets the per-sector idle noise be measured
    r.hold(pos, 60);
    CHECK(r.last.angleValid);
    return static_cast<double>(r.last.angleSigma);
  };
  const double quiet = sigmaAt(1.0, 0.0), loud = sigmaAt(3.0, 0.0);
  CHECK(quiet > 0.0);
  CHECK(loud > 2.0 * quiet);                         // ~3x the noise -> ~3x the uncertainty
  CHECK(sigmaAt(3.0, 0.5 / 8) < 0.75 * loud);        // finger between two sectors is better conditioned
  // And the prediction is in the right ballpark of the real jitter (conservative by < 2.2x).
  Run r(8, 92);
  r.idle(4000);
  r.hold(0.0, 50);
  const uint16_t ref = r.last.angle;
  double sq = 0.0, sig = 0.0;
  for (int i = 0; i < 3000; ++i) {
    r.tick(0.0, 1.0);
    const double e = angleDiff4096(ref, r.last.angle);
    sq += e * e;
    sig += r.last.angleSigma;
  }
  const double measured = std::sqrt(sq / 3000), predicted = sig / 3000;
  CHECK(predicted >= 0.9 * measured && predicted <= 2.2 * measured);
}

EPDJ_TEST(ring_noise_estimate_survives_noise_free_stretches_and_finger_lifts) {
  Run r(8, 93);
  r.sim.noisePermille = 0.0;  // a perfectly clean ADC for a while
  r.idle(6000);
  r.sim.noisePermille = 3.0;  // ...then it gets noisy
  r.idle(6000);
  r.hold(0.0, 50);
  const double a = r.last.angleSigma;
  CHECK(a > 5.0);             // the estimate recovered instead of staying stuck near zero
  for (int k = 0; k < 20; ++k) {  // lots of lifts must not inflate it either
    r.hold(0.3, 100);
    r.idle(200);
  }
  r.idle(3000);
  r.hold(0.0, 50);
  CHECK(r.last.angleSigma < 1.6 * a);
}

EPDJ_TEST(ring_two_sample_bursts_do_not_register_as_a_touch) {
  // The median removes isolated spikes; a 2-sample burst of finger-like size gets through it but must
  // still fail the two-sample confirmation.
  Run r(8, 94);
  r.idle(3500);
  for (int i = 0; i < 50000; ++i) {
    uint16_t raw[8];
    r.sim.read(0, 0, nullptr, r.rng, raw);
    if (i % 40 == 0 || i % 40 == 1) {
      const unsigned s = static_cast<unsigned>((i / 40) % 8);
      raw[s] = static_cast<uint16_t>(raw[s] * 1.025);  // +25 permille for two samples
    }
    const auto res = r.ring.update(raw, r.nowUs);
    r.nowUs += 1000;
    CHECK(!res.touched);
  }
}

EPDJ_TEST(ring_measures_each_sectors_idle_noise_and_does_not_learn_from_fingers) {
  Run r(8, 95);  // uniform +/-3 permille -> 1.73 rms raw, ~0.8 after the median + 2 ms smoothing
  r.idle(8000);
  double before[8];
  for (int i = 0; i < 8; ++i) {
    before[i] = r.ring.noiseSigma(static_cast<unsigned>(i));
    CHECK(before[i] > 0.4 && before[i] < 1.4);
  }
  // A long, slow hold plus many gestures: the skirt of the finger sits in the neighbouring sectors the
  // whole time, and none of it may leak into their noise estimates.
  r.hold(3.0 / 8, 60000);
  for (int k = 0; k < 10; ++k) {
    r.hold(0.1 * k, 150);
    r.idle(150);
  }
  for (int i = 0; i < 8; ++i) {
    const double after = r.ring.noiseSigma(static_cast<unsigned>(i));
    CHECK(after < before[i] * 1.12 + 0.05);
  }
}

EPDJ_TEST(ring_brief_brushes_do_not_distort_the_learned_gains) {
  // Equal sectors. A 60 ms swipe over a quarter turn spends ~20 ms on each sector, far too little to
  // believe any one sector's peak, so no gain may move. (Once sectors have been seen long enough the
  // gains stay within a few percent of 1 for an equal ring, see ring_gains_stay_neutral_*.)
  Run r(8, 96);
  r.idle(4000);
  for (int swipe = 0; swipe < 3; ++swipe) {
    for (int ms = 0; ms < 60; ++ms) r.tick(0.25 * ms / 60.0 + 0.2 * swipe, 1.0);
    r.idle(300);
  }
  for (unsigned i = 0; i < 8; ++i) CHECK_NEAR(r.ring.gain(i), 1.0, 0.005);
}
