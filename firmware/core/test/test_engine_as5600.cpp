// The default build, end to end through the Engine: a free-spinning platter on an AS5600 (platterSource = AS5600,
// contract v1.3) and ONE touch pad wired as the hold pad. Everything here starts from a Deck with no configuration at all,
// so these tests are also the pin on "the defaults are the AS5600 build". The experimental touch ring has its own
// tests (test_engine_core, test_touch_ring, ...) and selects itself with Deck::useRing().
//
// Units: 4 counts of the 12-bit angle (1/4096 rev) are one jog tick (1024 ticks per revolution).
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "test_engine_sim.h"

using namespace esim;
using proto::sysex::Cmd;

namespace {

constexpr int kSettleMs = 3600;  // the touch detector learns its baseline for 250 ms and settles for 3 s

std::vector<Bytes> notes(const std::vector<Bytes>& v) {
  std::vector<Bytes> r;
  for (const Bytes& m : v) {
    if (m.size() == 3 && ((m[0] & 0xF0) == 0x90 || (m[0] & 0xF0) == 0x80)) r.push_back(m);
  }
  return r;
}

bool hasCc(const std::vector<Bytes>& v, uint8_t cc) {
  for (const Bytes& m : v) {
    if (m.size() == 3 && (m[0] & 0xF0) == 0xB0 && m[1] == cc) return true;
  }
  return false;
}

size_t countCc(const std::vector<Bytes>& v, uint8_t cc) {
  size_t n = 0;
  for (const Bytes& m : v) n += (m.size() == 3 && (m[0] & 0xF0) == 0xB0 && m[1] == cc) ? 1u : 0u;
  return n;
}

// Time of the first (or last) 3-byte message in the log matching a predicate, or ~0 if none.
template <typename Pred>
uint64_t timeOf(const std::vector<Rec>& log, Pred p, bool last) {
  uint64_t t = ~0ull;
  for (const Rec& r : log) {
    if (r.kind != OutEvent::Kind::Message || r.bytes.size() != 3 || !p(r.bytes)) continue;
    t = r.t;
    if (!last) break;
  }
  return t;
}

std::vector<proto::Telemetry> telemetry(const std::vector<Rec>& log) {
  std::vector<proto::Telemetry> v;
  for (const Reply& r : replies(log)) {
    if (r.cmd != static_cast<uint8_t>(proto::sysex::Reply::Telemetry)) continue;
    proto::Telemetry t;
    CHECK(proto::deserialize(r.payload.data(), r.payload.size(), &t));
    v.push_back(t);
  }
  return v;
}

// A fresh default deck whose platter sits at `start` (so the first sample anchors there), then `deltas` applied one per
// millisecond (units of 1/4096 rev, clockwise positive), then 20 ms of rest for the 500 Hz emitter to flush.
// Returns what the firmware counted; checks that exactly that reached the wire.
int32_t jogFor(int start, const std::vector<int>& deltas, bool invert = false) {
  Deck d(0);
  d.reportUntouched();  // an untouched spin: the default policy would keep it off the wire (tests below)
  if (invert) d.set(ConfigKey::JogInvert, 1);
  d.angle = start;
  d.in.angle12 = static_cast<uint16_t>(start);
  d.run(20);
  d.clearLog();
  for (int delta : deltas) d.turn(delta);
  d.run(20);
  CHECK_EQ(ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog), d.engine.jogTotal());
  return d.engine.jogTotal();
}

}  // namespace

// ---- the default ------------------------------------------------------------------------------------

EPDJ_TEST(as5600_one_lap_is_exactly_1024_ticks_in_both_directions_on_both_decks) {
  for (int deckId = 0; deckId < 2; ++deckId) {
    for (int dir = -1; dir <= 1; dir += 2) {
      Deck d(deckId);
      CHECK_EQ(d.cfg.get(ConfigKey::PlatterSource), static_cast<int>(proto::PlatterSource::As5600));  // nothing selected
      d.reportUntouched();
      d.angle = 1234;  // anywhere
      d.in.angle12 = 1234;
      d.run(50);
      d.clearLog();
      d.spin(4.0 * dir, 1024);  // 4 counts per ms: one lap in 1024 ms, one tick per ms
      d.run(300);

      const uint8_t ch = static_cast<uint8_t>(deckId);
      const std::vector<Bytes> msgs = usbMsgs(d.log, 0);
      CHECK_EQ(d.engine.jogTotal(), 1024 * dir);
      CHECK_EQ(ccSum(msgs, ch, proto::cc::kJog), 1024 * dir);
      for (const Bytes& m : msgs) CHECK_EQ(m[0] & 0x0F, ch);
      CHECK(notes(msgs).empty());  // spinning is not touching: no hold pad involved
    }
  }
}

EPDJ_TEST(as5600_steps_count_four_to_the_tick_across_the_wrap_both_ways) {
  struct Case {
    const char* what;
    int start;
    std::vector<int> deltas;
    int32_t ticks;
  };
  const Case cases[] = {
      {"4 single counts forward, across 4095 -> 0", 4094, {1, 1, 1, 1}, 1},
      {"3 counts are not a tick yet", 4094, {1, 1, 1}, 0},
      {"4 single counts backward, across 0 -> 4095", 1, {-1, -1, -1, -1}, -1},
      {"one step across the forward wrap", 4000, {96}, 24},
      {"one step across the backward wrap", 10, {-20}, -5},
      {"the carry survives between steps", 100, {3, 1}, 1},
      {"... and between directions: +3 -3 is nothing", 100, {3, -3}, 0},
      {"... and +3 -3 +3 +1 is one tick", 100, {3, -3, 3, 1}, 1},
      {"a quarter turn in one millisecond is still motion", 0, {1024}, 256},
      {"more than a quarter turn in one millisecond is a glitch, not motion", 0, {1025}, 0},
      {"... and the platter is tracked again after it (the first good sample re-anchors)", 0, {1025, 4, 4}, 1},
      {"three laps forward in steps of 64 counts", 777, std::vector<int>(192, 64), 3072},
      {"two laps backward in steps of 37 counts (the wrap falls inside a step)", 5, std::vector<int>(222, -37), -2053},
  };
  for (const Case& c : cases) {
    const int32_t plain = jogFor(c.start, c.deltas);
    if (plain != c.ticks) std::printf("    case: %s -> %d ticks, expected %d\n", c.what, plain, c.ticks);
    CHECK_EQ(plain, c.ticks);
    // jogInvert is the software DIR pin: every case, mirrored.
    const int32_t inverted = jogFor(c.start, c.deltas, /*invert=*/true);
    if (inverted != -c.ticks) std::printf("    case (jogInvert): %s -> %d ticks, expected %d\n", c.what, inverted, -c.ticks);
    CHECK_EQ(inverted, -c.ticks);
  }
}

EPDJ_TEST(as5600_two_hundred_laps_and_back_do_not_drift) {
  Deck d(0);
  d.reportUntouched();
  Rng rng(5);
  const double speed = 5.37;  // counts per ms: irregular steps of 5 and 6, and a different wrap point every lap
  const int start = 2999;
  d.angle = start;
  d.in.angle12 = static_cast<uint16_t>(start);
  d.run(20);
  d.clearLog();

  const int steps = 150000;  // 805,500 counts = 196.6 laps
  int64_t travelled = 0;     // counts, from what the sensor model reports
  int prev = start;
  for (int i = 0; i < steps; ++i) {
    d.turn(speed);
    travelled += ((static_cast<int>(d.in.angle12) - prev + 2048 + 4096) & 4095) - 2048;
    prev = d.in.angle12;
  }
  d.run(300);
  const int64_t forwardTicks = d.engine.jogTotal();
  CHECK(std::llabs(forwardTicks * 4 - travelled) < 4);  // the sub-tick remainder is carried, never dropped
  CHECK(travelled > 190 * 4096);

  for (int i = 0; i < steps; ++i) {  // and straight back, with some jitter on the speed
    d.turn(-speed + 0.3 * rng.sym());
    travelled += ((static_cast<int>(d.in.angle12) - prev + 2048 + 4096) & 4095) - 2048;
    prev = d.in.angle12;
  }
  d.run(300);
  CHECK(std::llabs(static_cast<int64_t>(d.engine.jogTotal()) * 4 - travelled) < 4);
  CHECK(std::llabs(travelled) < 4096);  // the jitter is symmetric: the platter is back within a lap of where it started
  CHECK_EQ(ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog), d.engine.jogTotal());  // and every tick reached the host
}

EPDJ_TEST(as5600_noise_at_rest_never_drifts_and_one_count_of_it_never_makes_a_tick) {
  // The tracker needs four counts of net movement for a tick and carries the remainder, so the total is
  // (angle - first angle) / 4 truncated: jitter cannot accumulate, it can only flicker the total between two neighbours
  // when the first sample sat near a tick boundary. +-1 count of white noise (2 counts peak to peak) is below that.
  for (int amp = 1; amp <= 3; ++amp) {
    int flickered = 0;
    for (uint32_t seed = 1; seed <= 20; ++seed) {
      Deck d(0);
      Rng rng(seed * 7919);
      d.angle = 1000;
      for (int i = 0; i < 20; ++i) {  // the first sample the tracker anchors on is noisy too
        d.in.angle12 = static_cast<uint16_t>(1000 + rng.range(-amp, amp));
        d.padStep(false);
      }
      int worst = 0;
      for (int i = 0; i < 10000; ++i) {
        d.in.angle12 = static_cast<uint16_t>(1000 + rng.range(-amp, amp));
        d.padStep(false);
        worst = std::max(worst, std::abs(d.engine.jogTotal()));
      }
      CHECK(worst <= 1);                // never drifts, whatever the amplitude up to +-3
      if (amp == 1) CHECK_EQ(worst, 0);  // +-1 count: not one tick, from any starting position
      flickered += worst;
    }
    if (amp == 3) CHECK(flickered > 0);  // and larger noise does flicker for some starting positions, so the test can fail
  }
}

namespace {

// A still platter as the chip reports it, for the measurements below. `sigma` is the white noise of the chip's own
// measurement in counts rms. With the filter on (EPDJ_AS5600_FILTERED_ANGLE, the ANGLE register, slow filter 16x) the
// reading is that noise through a 1-in-16 exponential average updated at the chip's internal conversion rate (about 7 per
// millisecond), and the register is the integer part of position + filtered noise; with the raw register it is the noise
// itself. A MODEL: neither sigma nor the internal rate has been measured on a chip. firmware/core/README.md has the results.
struct ChipNoise {
  Rng rng;
  double sigma;
  bool filtered;
  double truth;  // the platter's true position inside its count, so a seed decides how close it rests to a boundary
  double state = 0.0;

  ChipNoise(uint32_t seed, double s, bool f) : rng(seed * 2654435761u + 1u), sigma(s), filtered(f), truth(1000.0) {
    truth += rng.uni();
  }
  double gauss() { return std::sqrt(-2.0 * std::log(rng.uni())) * std::cos(6.283185307179586 * rng.uni()); }
  uint16_t read() {
    if (filtered) {
      for (int k = 0; k < 7; ++k) state += (sigma * gauss() - state) / 16.0;
    } else {
      state = sigma * gauss();
    }
    return static_cast<uint16_t>(static_cast<int64_t>(std::floor(truth + state)) & 4095);
  }
};

struct RestResult {
  int worstNet = 0;       // largest |jogTotal| over the run, in ticks (the total is the sum of every tick sent)
  int tickEvents = 0;     // times the total changed: each is a one-tick flicker the host sees as a jog CC
  int64_t cc = 0;         // jog CC messages that reached the wire
};

// A finger rests on the hold pad (so the default AUTO policy sends everything) over a platter that does not move, for
// `seconds`, with `seeds` different resting positions and noise sequences.
RestResult restWithFinger(double sigma, bool filtered, int seeds, int seconds) {
  RestResult r;
  for (int seed = 1; seed <= seeds; ++seed) {
    Deck d(0);
    ChipNoise chip(static_cast<uint32_t>(seed), sigma, filtered);
    d.touchIdle(kSettleMs);
    for (int i = 0; i < 200; ++i) {  // the finger lands; the tracker anchors on a noisy sample like any other
      d.in.angle12 = chip.read();
      d.padStep(true);
    }
    d.clearLog();
    const int32_t base = d.engine.jogTotal();
    int32_t prev = base;
    for (int i = 0; i < seconds * 1000; ++i) {
      d.in.angle12 = chip.read();
      d.padStep(true);
      const int32_t total = d.engine.jogTotal();
      if (total != prev) ++r.tickEvents;
      prev = total;
      r.worstNet = std::max(r.worstNet, std::abs(total - base));
    }
    d.run(20);
    r.cc += static_cast<int64_t>(countCc(usbMsgs(d.log, 0), proto::cc::kJog));
    CHECK(d.engine.led().touched);  // the finger stayed down the whole time
  }
  return r;
}

}  // namespace

EPDJ_TEST(as5600_a_touched_still_platter_makes_no_net_motion_and_at_most_single_tick_flicker) {
  // The question behind "why not drop small per-sample changes like the original firmware does": with the engine's real jog
  // path (no threshold, exact carry) and a finger on the pad, does a platter at rest produce motion? The tracker's total is
  // (position - first position) / 4 truncated, so rest noise cannot accumulate; it can only flicker the total between
  // neighbouring ticks when the platter rests next to a tick boundary and the noise crosses it. Measured on the models above.
  const double sigmas[] = {0.5, 1.0, 2.0, 3.0, 4.0};
  std::printf("    still platter, finger down, 8 resting positions x 15 s each\n");
  std::printf("    raw sigma   filtered ANGLE: worst |net| ticks, ticks flicker/min, jog CC   raw register: same\n");
  for (double sigma : sigmas) {
    const RestResult f = restWithFinger(sigma, true, 8, 15);
    const RestResult r = restWithFinger(sigma, false, 8, 15);
    std::printf("    %4.1f counts   %d, %7.1f, %6lld                              %d, %7.1f, %6lld\n", sigma, f.worstNet,
                f.tickEvents / (8.0 * 15.0 / 60.0), static_cast<long long>(f.cc), r.worstNet, r.tickEvents / (8.0 * 15.0 / 60.0),
                static_cast<long long>(r.cc));
    // The filtered register never makes more than a one-tick flicker, up to 4 counts rms of raw chip noise (16 counts peak
    // to peak, far beyond what a sound magnet gap gives) and is silent up to 3.
    CHECK(f.worstNet <= 1);
    if (sigma <= 3.0) CHECK_EQ(f.tickEvents, 0);
    // The raw register is as quiet only while the chip's own noise stays near one count rms; past that it flickers by more
    // than a tick, over a hundred times a second. That is the case for the filtered ANGLE register as the default, and what
    // the bench A/B (EPDJ_AS5600_FILTERED_ANGLE = n) is for: the real chip's figure is not known here.
    if (sigma <= 1.0) CHECK(r.worstNet <= 1);
    if (sigma >= 2.0) CHECK(r.worstNet >= 2);  // the measurement can see a platter that is not quiet
    CHECK(f.tickEvents <= r.tickEvents);       // and the slow filter never flickers more than the raw register
  }
}

EPDJ_TEST(as5600_slow_motion_is_passed_exactly_where_a_three_count_per_sample_threshold_would_drop_it) {
  // The original firmware drops any per-sample change smaller than 3 counts while touched. Applied to our 1 kHz samples
  // that rule delivers nothing below about 2 counts per millisecond (0.5 rev/s) and next to nothing up to 3 (0.73 rev/s),
  // where whole samples start to clear it. The engine instead
  // keeps every count (4 counts = 1 tick, the remainder carried), and the chip's own filter plus that quantisation already
  // keep rest noise below one tick (the test above). This prints what each would deliver, from the same angle samples.
  std::printf("    speed      engine ticks / true ticks      3-count rule delivers (of the true motion)\n");
  for (double revPerSec : {0.05, 0.1, 0.2, 0.5, 1.0, 2.0}) {
    Deck d(0);
    d.touchIdle(kSettleMs);
    for (int i = 0; i < 200; ++i) d.padStep(true);
    d.clearLog();
    const double countsPerMs = revPerSec * 4096.0 / 1000.0;
    const int32_t base = d.engine.jogTotal();
    int64_t whatIf = 0;  // counts a per-sample |delta| >= 3 rule would pass
    uint16_t prev = d.in.angle12;
    const int ms = 10000;
    for (int i = 0; i < ms; ++i) {
      d.turn(countsPerMs, true);
      const int delta = ((static_cast<int>(d.in.angle12) - prev + 2048 + 4096) & 4095) - 2048;
      prev = d.in.angle12;
      if (std::abs(delta) >= 3) whatIf += delta;
    }
    d.run(50);
    const double trueTicks = countsPerMs * ms / 4.0;
    const int32_t ours = d.engine.jogTotal() - base;
    std::printf("    %5.2f rev/s   %6d / %9.1f              %5.1f%%\n", revPerSec, ours, trueTicks,
                100.0 * static_cast<double>(whatIf) / (trueTicks * 4.0));
    CHECK(std::fabs(ours - trueTicks) <= 1.0);                // exact up to the sub-tick carry
    CHECK_EQ(ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog), ours);  // and all of it reaches the host (the finger is down)
    if (revPerSec <= 0.2) CHECK_EQ(whatIf, 0);                // below 2 counts/ms the other rule would send nothing at all
  }
}

// ---- no encoder, or a bad read ------------------------------------------------------------------------

EPDJ_TEST(as5600_an_unanswered_read_produces_no_ticks_and_clears_angle_valid) {
  Deck d(0);
  d.angle = 100;
  d.in.angle12 = 100;
  d.run(20);
  ask(d, 0, Cmd::Stream, b({100}));
  d.clearLog();

  d.spin(4, 100);  // healthy: the flag is up in every frame
  std::vector<proto::Telemetry> f = telemetry(d.log);
  CHECK(f.size() >= 9);
  for (const proto::Telemetry& t : f) CHECK((t.flags & proto::stateFlags::kAngleValid) != 0);

  d.clearLog();
  d.in.angleValid = false;  // the I2C read failed (the HAL reports exactly this and leaves angle12 alone)
  Rng rng(3);
  const int32_t before = d.engine.jogTotal();
  for (int i = 0; i < 200; ++i) {
    d.in.angle12 = static_cast<uint16_t>(rng.next() & 4095);  // the platter turns on; the stale register holds whatever it holds
    d.padStep(false);
  }
  CHECK_EQ(d.engine.jogTotal(), before);  // not one tick out of garbage
  f = telemetry(d.log);
  CHECK(f.size() >= 19);
  for (const proto::Telemetry& t : f) CHECK_EQ(t.flags & proto::stateFlags::kAngleValid, 0);

  d.in.angleValid = true;  // the bus recovers while the platter is somewhere else entirely
  d.in.angle12 = 3000;
  d.step();
  d.step();
  CHECK_EQ(d.engine.jogTotal(), before);  // the first valid sample re-anchors; the jump is not motion
  d.in.angle12 = 3040;                    // then 40 counts = 10 ticks
  d.step();
  d.run(20);
  CHECK_EQ(d.engine.jogTotal(), before + 10);
  d.clearLog();
  d.run(30);
  f = telemetry(d.log);
  CHECK(!f.empty());
  for (const proto::Telemetry& t : f) CHECK((t.flags & proto::stateFlags::kAngleValid) != 0);
}

EPDJ_TEST(as5600_a_short_dropout_loses_nothing_a_long_one_loses_only_what_it_cannot_see) {
  // The tracker keeps its anchor across up to 3 ms of invalid reads; beyond that it re-anchors and the motion made while
  // blind is gone, but nothing is invented. 4 counts per ms = one tick per ms, so the arithmetic is in ticks.
  const auto run = [](int invalidSteps) {
    Deck d(0);
    d.run(20);
    d.clearLog();
    d.spin(4, 20);  // 20 ticks
    d.in.angleValid = false;
    d.spin(4, invalidSteps);  // the platter keeps turning while the reads fail
    d.in.angleValid = true;
    d.spin(4, 20);
    d.run(20);
    return d.engine.jogTotal();
  };
  CHECK_EQ(run(0), 40);
  CHECK_EQ(run(1), 41);  // anchor kept: the sample after the gap sees all 8 counts of the 2 ms
  CHECK_EQ(run(3), 43);  // 3 ms of blindness is still bridged
  CHECK_EQ(run(4), 39);  // 4 ms is not: re-anchor on the first good sample (no tick for it), then 19 ticks
  CHECK_EQ(run(100), 39);  // and a 100 ms outage (the HAL's back-off after two failed reads) costs the same: no burst afterwards
}

EPDJ_TEST(as5600_an_encoder_that_turns_up_after_boot_is_picked_up_without_a_jump) {
  // The platform asks an encoder that did not answer at boot again every 500 ms (encoder_retry.h). To the engine that is
  // angleValid = false from the first step, a long time of it while the platter turns, and then true at some position.
  Deck d(0);
  d.reportUntouched();
  d.in.angleValid = false;
  d.touchIdle(kSettleMs);
  ask(d, 0, Cmd::Stream, b({100}));
  d.clearLog();
  Rng rng(21);
  for (int i = 0; i < 2500; ++i) {  // 2.5 s: five trials of the HAL would all have failed
    d.in.angle12 = static_cast<uint16_t>(rng.next() & 4095);
    d.padStep(false);
  }
  CHECK_EQ(d.engine.jogTotal(), 0);
  std::vector<proto::Telemetry> f = telemetry(d.log);
  CHECK(!f.empty());
  for (const proto::Telemetry& t : f) CHECK_EQ(t.flags & proto::stateFlags::kAngleValid, 0);

  d.clearLog();
  d.in.angleValid = true;  // the wire is fixed; the first good read finds the platter wherever it is
  d.angle = 3333;
  d.in.angle12 = 3333;
  d.step();
  d.step();
  CHECK_EQ(d.engine.jogTotal(), 0);  // the position at that moment is not motion
  d.spin(4, 100);
  d.run(20);
  CHECK_EQ(d.engine.jogTotal(), 100);  // and from there on counting is exact
  CHECK_EQ(ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog), 100);
  f = telemetry(d.log);
  CHECK(!f.empty());
  for (const proto::Telemetry& t : f) CHECK((t.flags & proto::stateFlags::kAngleValid) != 0);  // ANGLE_VALID is back
}

EPDJ_TEST(as5600_unit_with_no_encoder_stays_silent_on_jog_but_everything_else_works) {
  Deck d(0);
  d.set(ConfigKey::FaderRole, 0);
  d.in.angleValid = false;  // nobody ever answers on I2C: the HAL leaves angleValid false and angle12 at 0
  d.touchIdle(kSettleMs);
  ask(d, 0, Cmd::Stream, b({100}));
  d.clearLog();

  Rng rng(11);
  for (int i = 0; i < 6000; ++i) {
    d.in.angle12 = static_cast<uint16_t>(rng.next() & 4095);  // even if the register were noise
    d.in.faderRaw = static_cast<uint16_t>(500 + i / 2);
    d.in.cue = (i / 700) % 2 == 1;
    d.padStep((i / 900) % 2 == 1);  // a finger comes and goes on the hold pad
  }
  d.run(200);

  const std::vector<Bytes> msgs = usbMsgs(d.log, 0);
  CHECK(!hasCc(msgs, proto::cc::kJog));  // silent: not a single jog message
  CHECK_EQ(d.engine.jogTotal(), 0);
  for (const proto::Telemetry& t : telemetry(d.log)) {
    CHECK_EQ(t.flags & proto::stateFlags::kAngleValid, 0);
    CHECK_EQ(t.jogTotal, 0);
  }
  // The rest of the unit is alive: touch notes from the hold pad, the CUE button, the fader.
  CHECK(countEq(msgs, b({0x90, 38, 127})) >= 3);
  CHECK(countEq(msgs, b({0x90, 36, 127})) >= 3);
  CHECK(hasCc(msgs, proto::cc::kCrossfaderMsb));

  // As a clock master the platter is the tempo wheel, and a blind one does not move the tempo.
  d.set(ConfigKey::Mode, 1);
  d.run(50);
  const uint16_t tempo = d.engine.tempoX10();
  for (int i = 0; i < 1500; ++i) {
    d.in.angle12 = static_cast<uint16_t>(rng.next() & 4095);
    d.padStep(true);
  }
  CHECK_EQ(d.engine.tempoX10(), tempo);
}

// ---- the hold pad ------------------------------------------------------------------------------------

EPDJ_TEST(as5600_one_hold_pad_gives_touch_notes_and_taps_while_the_encoder_gives_motion) {
  Deck d(0);
  CHECK_EQ(d.in.touchCount, 1);  // the pad count of the default build
  d.touchIdle(kSettleMs);
  d.clearLog();

  // A tap: touch, no motion, release.
  for (int i = 0; i < 100; ++i) d.padStep(true);
  d.padIdle(150);
  std::vector<Bytes> n = notes(usbMsgs(d.log, 0));
  CHECK_EQ(n.size(), 4);
  if (n.size() == 4) {
    CHECK_BYTES(n[0], b({0x90, 38, 127}));  // touch
    CHECK_BYTES(n[1], b({0x80, 38, 0}));
    CHECK_BYTES(n[2], b({0x90, 39, 127}));  // tap-play pulse
    CHECK_BYTES(n[3], b({0x80, 39, 0}));
  }

  // A scratch: touch, turn the platter by hand 0.5 rev, release. Touch notes bracket the ticks; no tap.
  d.clearLog();
  const int32_t jogBefore = d.engine.jogTotal();
  for (int i = 0; i < 100; ++i) d.padStep(true);
  d.spin(8.0, 256, true);  // 256 ms at 8 counts per ms (2 rev/s): 512 ticks
  for (int i = 0; i < 100; ++i) d.padStep(true);
  d.padIdle(150);
  n = notes(usbMsgs(d.log, 0));
  CHECK_EQ(n.size(), 2);  // touch on, touch off; the motion made it not a tap
  if (n.size() == 2) {
    CHECK_BYTES(n[0], b({0x90, 38, 127}));
    CHECK_BYTES(n[1], b({0x80, 38, 0}));
  }
  const uint64_t tOn = timeOf(d.log, [](const Bytes& m) { return m[0] == 0x90 && m[1] == 38; }, false);
  const uint64_t tOff = timeOf(d.log, [](const Bytes& m) { return m[0] == 0x80 && m[1] == 38; }, true);
  const uint64_t tJogFirst = timeOf(d.log, [](const Bytes& m) { return m[0] == 0xB0 && m[1] == proto::cc::kJog; }, false);
  const uint64_t tJogLast = timeOf(d.log, [](const Bytes& m) { return m[0] == 0xB0 && m[1] == proto::cc::kJog; }, true);
  CHECK(tOn < tJogFirst && tJogLast < tOff);  // scratch mode is entered before the first tick and left after the last
  CHECK_EQ(d.engine.jogTotal() - jogBefore, 512);
}

EPDJ_TEST(as5600_a_platter_that_keeps_spinning_after_the_hand_leaves_reports_motion_when_asked_to) {
  // jogUntouched = REPORT: untouched motion is a pitch nudge. (The default, AUTO, keeps it off the wire when a hold pad is
  // wired: test_engine_jog_policy.cpp.)
  Deck d(0);
  d.reportUntouched();
  d.touchIdle(kSettleMs);
  d.clearLog();

  for (int i = 0; i < 50; ++i) d.padStep(true);
  d.spin(4, 200, true);   // pushed by hand: 200 ticks while touched
  d.spin(4, 500, false);  // the hand lets go; the platter coasts on at the same speed: 500 ticks untouched
  d.spin(0, 300, false);  // and stops
  d.run(100);

  const std::vector<Bytes> msgs = usbMsgs(d.log, 0);
  CHECK_EQ(d.engine.jogTotal(), 700);
  CHECK_EQ(ccSum(msgs, 0, proto::cc::kJog), 700);
  CHECK_EQ(countEq(msgs, b({0x90, 38, 127})), 1);
  CHECK_EQ(countEq(msgs, b({0x80, 38, 0})), 1);
  CHECK_EQ(countEq(msgs, b({0x90, 39, 127})), 0);  // pushing it is not a tap

  // The touch-up note comes in the middle of the motion, and motion goes on after it.
  const uint64_t tOff = timeOf(d.log, [](const Bytes& m) { return m[0] == 0x80 && m[1] == 38; }, true);
  int64_t after = 0;
  for (const Rec& r : d.log) {
    if (r.kind == OutEvent::Kind::Message && r.bytes.size() == 3 && r.bytes[0] == 0xB0 && r.bytes[1] == proto::cc::kJog && r.t > tOff) {
      after += r.bytes[2] - 64;
    }
  }
  CHECK(after >= 450);  // the part of the 500 coasting ticks that left after the release (the emitter lags a few ms)
}

EPDJ_TEST(as5600_untouched_spin_never_makes_a_touch_note_even_for_ten_seconds) {
  Deck d(0);
  d.touchIdle(kSettleMs);
  d.clearLog();
  d.spin(6.25, 10000);  // 10 s of free spinning, 15 laps, with the hold pad's idle noise on it
  d.run(100);
  CHECK(notes(usbMsgs(d.log, 0)).empty());
  CHECK_EQ(d.engine.jogTotal(), 15625);  // 62500 counts / 4
}

EPDJ_TEST(as5600_without_a_hold_pad_jog_works_but_no_touch_note_ever_comes) {
  // EPDJ_TOUCH_PAD_COUNT = 0: nothing wired to GPIO4. The platter spins and the host gets pitch-bend motion, but a DJ
  // app never hears "touched", so it never enters scratch mode; no tap-play either.
  Deck d(0);
  d.in.touchCount = 0;
  d.run(kSettleMs);
  d.clearLog();
  ask(d, 0, Cmd::Stream, b({100}));
  d.gesture(0.0, 0.5, 1.0, 100);  // "touching" is not something this build can sense
  d.run(300);
  const std::vector<Bytes> msgs = usbMsgs(d.log, 0);
  CHECK(notes(msgs).empty());
  CHECK_EQ(d.engine.jogTotal(), 1024);
  CHECK_EQ(ccSum(msgs, 0, proto::cc::kJog), 1024);
  for (const proto::Telemetry& t : telemetry(d.log)) CHECK_EQ(t.flags & proto::stateFlags::kTouched, 0);
}

// ---- switching away from, and back to, the default ---------------------------------------------------

EPDJ_TEST(as5600_live_switch_to_the_ring_does_not_strand_the_touch_note) {
  Deck d(0);
  d.touchIdle(kSettleMs);
  d.clearLog();
  for (int i = 0; i < 200; ++i) d.padStep(true);  // finger resting on the hold pad
  CHECK_EQ(countEq(usbMsgs(d.log, 0), b({0x90, 38, 127})), 1);
  d.set(ConfigKey::PlatterSource, static_cast<int>(proto::PlatterSource::TouchRing));  // one pad is not a ring, but it must not wedge
  for (int i = 0; i < 100; ++i) d.padStep(true);
  d.padIdle(200);
  int open = 0;
  for (const Bytes& m : notes(usbMsgs(d.log, 0))) {
    if (m[1] != 38) continue;
    open += m[0] == 0x90 ? 1 : -1;
    CHECK(open == 0 || open == 1);
  }
  CHECK_EQ(open, 0);
}

EPDJ_TEST(as5600_load_defaults_puts_a_unit_back_on_the_encoder) {
  Deck d(0);
  d.set(ConfigKey::PlatterSource, static_cast<int>(proto::PlatterSource::TouchHoldOnly));
  d.run(50);
  d.spin(4, 100);
  CHECK_EQ(d.engine.jogTotal(), 0);  // hold-only: no rotation at all

  const std::vector<Reply> r = ask(d, 0, Cmd::LoadDefaults);
  CHECK(!r.empty() && r[0].cmd == 0x7E);
  CHECK_EQ(d.cfg.get(ConfigKey::PlatterSource), static_cast<int>(proto::PlatterSource::As5600));
  d.run(20);
  d.spin(4, 100);
  d.run(20);
  CHECK_EQ(d.engine.jogTotal(), 100);  // the step that applied the new config anchored the tracker, so every count after it is motion
}
