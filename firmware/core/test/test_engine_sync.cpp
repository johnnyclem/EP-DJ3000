// Engine as clock master (accuracy, transport, tap tempo, platter nudge, MTC) and as follower.
#include <cmath>
#include <cstdlib>
#include <memory>

#include "test_engine_sim.h"

using namespace esim;

namespace {

constexpr int kSettleMs = 3600;

std::unique_ptr<Deck> makeMaster(int mask = 3) {
  std::unique_ptr<Deck> d(new Deck(0));
  d->set(ConfigKey::Mode, 1);
  d->set(ConfigKey::ClockOutMask, mask);
  d->ringIdle(kSettleMs);
  d->clearLog();
  return d;
}

bool isClock(const Rec& r) {
  return r.kind == OutEvent::Kind::Message && r.bytes.size() == 1 && r.bytes[0] == 0xF8;
}

size_t countClocks(const std::vector<Rec>& log, size_t from, size_t to) {
  size_t n = 0;
  for (size_t i = from; i < to && i < log.size(); ++i) n += isClock(log[i]) ? 1u : 0u;
  return n;
}

std::vector<uint64_t> tickTimes(const std::vector<Rec>& log, size_t from = 0) {
  std::vector<uint64_t> t;
  for (size_t i = from; i < log.size(); ++i) {
    if (isClock(log[i])) t.push_back(log[i].t);
  }
  return t;
}

// System messages other than clock and MTC quarter frames on USB cable 0, flattened: the transport
// conversation (Start, Stop, Continue, Song Position).
Bytes transportBytes(const std::vector<Rec>& log, size_t from = 0) {
  Bytes out;
  for (size_t i = from; i < log.size(); ++i) {
    const Rec& r = log[i];
    if (r.kind != OutEvent::Kind::Message || !(r.portMask & ports::kUsb) || r.cable != 0) continue;
    if (r.bytes.empty() || r.bytes[0] < 0xF2 || r.bytes[0] == 0xF8) continue;
    out.insert(out.end(), r.bytes.begin(), r.bytes.end());
  }
  return out;
}

bool ccPresent(const std::vector<Bytes>& v, uint8_t cc) {
  for (const Bytes& m : v) {
    if (m.size() == 3 && (m[0] & 0xF0) == 0xB0 && m[1] == cc) return true;
  }
  return false;
}

void hostTransport(Deck& d, int state) { d.hostMsg(b({0xB0, 29, state})); }

// A host or drum machine sending MIDI clock: ticks of `periodUs` with uniform timestamp jitter, delivered
// to the engine with their own receive timestamps between 1 ms steps.
struct ClockSource {
  Deck& d;
  Engine::MidiPort port;
  double periodUs, jitterFrac;
  Rng rng;
  double nominal;  // local time of the next tick, before jitter
  uint64_t nextAt;
  int sent = 0;
  bool running = true;

  ClockSource(Deck& deck, Engine::MidiPort p, double period, double jitter, uint32_t seed)
      : d(deck), port(p), periodUs(period), jitterFrac(jitter), rng(seed), nominal(static_cast<double>(deck.localNow()) + 5000.0) {
    nextAt = jittered();
  }
  uint64_t jittered() { return static_cast<uint64_t>(std::llround(nominal + jitterFrac * periodUs * rng.sym())); }

  // A tick is handed over once its receive time has passed, with that time as its timestamp.
  void step() {
    const uint8_t clk = 0xF8;
    while (running && nextAt <= d.localNow()) {
      d.engine.onMidiIn(port, &clk, 1, nextAt);
      nominal += periodUs;
      nextAt = jittered();
      ++sent;
    }
    d.step();
  }
  void run(int ms) {
    for (int i = 0; i < ms; ++i) step();
  }
};

}  // namespace

// ---- clock accuracy ----------------------------------------------------------------------------

EPDJ_TEST(engine_master_clock_10s_at_1khz_tracks_the_ideal_schedule) {
  Deck d(0);
  d.set(ConfigKey::Mode, 1);
  const uint64_t t0 = d.t;  // the clock starts in the first step
  d.run(10001);
  const std::vector<uint64_t> ticks = tickTimes(d.log);
  CHECK_EQ(ticks.size(), 480);  // 10 s of 24 PPQN at 120 BPM
  const double period = 25e6 / 1200;
  for (size_t k = 0; k < ticks.size(); ++k) {
    const double ideal = static_cast<double>(t0) + static_cast<double>(k + 1) * period;
    const double late = static_cast<double>(ticks[k]) - ideal;
    CHECK(late > -1.0 && late < 1000.0);  // first step at or after the ideal time (integer-us schedule: < 1 us early), under a step late
  }
  // Continuous clock while stopped, to both default ports, on cable 0.
  for (const Rec& r : d.log) {
    if (!isClock(r)) continue;
    CHECK_EQ(r.portMask, ports::kUsb | ports::kTrs);
    CHECK_EQ(r.cable, 0);
  }
  CHECK(d.engine.transport() == proto::TransportState::Stopped);
}

EPDJ_TEST(engine_master_clock_10s_stepped_finely_is_within_50us) {
  Deck d(0);
  d.set(ConfigKey::Mode, 1);
  d.set(ConfigKey::ClockOutMask, 1);
  const uint64_t t0 = d.t;
  for (int i = 0; i <= 400000; ++i) d.step(25);  // 10 s at 40 kHz: placement error is bounded by the call interval
  const std::vector<uint64_t> ticks = tickTimes(d.log);
  CHECK_EQ(ticks.size(), 480);
  const double period = 25e6 / 1200;
  double worst = 0.0;
  for (size_t k = 0; k < ticks.size(); ++k) {
    const double late = static_cast<double>(ticks[k]) - (static_cast<double>(t0) + static_cast<double>(k + 1) * period);
    if (late > worst) worst = late;
    CHECK(late > -1.0 && late <= 50.0);
  }
  CHECK(worst < 25.0);
}

EPDJ_TEST(engine_master_random_tempo_changes_follow_the_generator_schedule) {
  // 25 random tempo changes by the host over 60 s. The expected tick times are re-derived from the
  // generator's definition: the interval already scheduled when the tempo changes is left alone and each
  // later interval has the period in force when the previous tick was consumed. A phase reset or a jump
  // would show up as a growing error against that model.
  Deck d(0);
  d.set(ConfigKey::Mode, 1);
  d.set(ConfigKey::ClockOutMask, 1);
  const uint64_t t0 = d.t;
  Rng rng(11);
  struct Change {
    uint64_t atStep;
    int x10;
  };
  std::vector<Change> changes = {{0, 1200}};
  for (int i = 0; i < 25; ++i) {
    const int steps = 20000 + rng.range(0, 90000);  // 0.5 .. 2.75 s
    for (int k = 0; k < steps; ++k) d.step(25);
    const int x10 = rng.range(200, 3000);
    d.hostMsg(b({0xB0, 20, x10 >> 7, 0xB0, 52, x10 & 127}));
    changes.push_back({d.t, x10});  // in force from the next step on
  }
  for (int k = 0; k < 40000; ++k) d.step(25);

  const std::vector<uint64_t> ticks = tickTimes(d.log);
  CHECK(ticks.size() > 500);
  auto x10At = [&](uint64_t step) {
    int x = changes[0].x10;
    for (const Change& c : changes) {
      if (c.atStep <= step) x = c.x10;
    }
    return x;
  };
  double model = static_cast<double>(t0) + 25e6 / 1200;  // first tick
  double worst = 0.0;
  for (size_t k = 0; k < ticks.size(); ++k) {
    const double err = static_cast<double>(ticks[k]) - model;
    if (std::fabs(err - 12.5) > worst) worst = std::fabs(err - 12.5);
    CHECK(err > -1.0 && err < 25.5);  // consumed in the first 25 us step at or after the model time
    model += 25e6 / x10At(ticks[k]);
  }
  CHECK(worst <= 13.6);
}

EPDJ_TEST(engine_master_tempo_change_keeps_the_scheduled_tick) {
  Deck d(0);
  d.set(ConfigKey::Mode, 1);
  d.set(ConfigKey::ClockOutMask, 1);
  const double oldP = 25e6 / 1200, newP = 25e6 / 1500;
  for (int i = 0; i < 100000; ++i) d.step(25);
  const size_t seen = tickTimes(d.log).size();
  while (tickTimes(d.log).size() == seen) d.step(25);  // just after a tick ...
  const std::vector<uint64_t> before = tickTimes(d.log);
  while (static_cast<double>(d.t) < static_cast<double>(before.back()) + oldP * 0.25) d.step(25);  // ... a quarter into the interval
  const uint64_t changeAt = d.t;  // a quarter of the way into an interval
  d.set(ConfigKey::BpmX10, 1500);  // the SET_CONFIG path
  for (int i = 0; i < 40000; ++i) d.step(25);

  const std::vector<uint64_t> ticks = tickTimes(d.log);
  const size_t n = before.size();
  CHECK(ticks.size() > n + 20);
  if (ticks.size() <= n + 20) return;
  CHECK(static_cast<double>(ticks[n]) > static_cast<double>(changeAt));
  CHECK_NEAR(static_cast<double>(ticks[n]), static_cast<double>(before.back()) + oldP, 26.0);  // not moved
  for (size_t k = n + 1; k < ticks.size(); ++k) {  // later intervals have the new period
    CHECK_NEAR(static_cast<double>(ticks[k]), static_cast<double>(ticks[n]) + static_cast<double>(k - n) * newP, 40.0);
  }
}

EPDJ_TEST(engine_master_clock_ports_follow_clock_out_mask) {
  struct Case {
    int mask;
    bool usbMounted;
    int trsEnable;
    uint8_t expectPorts;  // 0 = no clock events at all
  };
  const Case cases[] = {
      {1, true, 1, ports::kUsb},
      {2, true, 1, ports::kTrs},
      {3, true, 1, ports::kUsb | ports::kTrs},
      {4, true, 1, 0},  // LINK alone: the radio carries TRANSPORT, never 0xF8
      {0, true, 1, 0},
      {3, false, 1, ports::kTrs},
      {3, true, 0, ports::kUsb},
      {3, false, 0, 0},
  };
  for (const Case& c : cases) {
    Deck d(0);
    d.set(ConfigKey::Mode, 1);
    d.set(ConfigKey::ClockOutMask, c.mask);
    d.set(ConfigKey::TrsMidiEnable, c.trsEnable);
    d.in.usbMounted = c.usbMounted;
    d.run(1000);
    size_t n = 0;
    for (const Rec& r : d.log) {
      if (!isClock(r)) continue;
      ++n;
      CHECK_EQ(r.portMask, c.expectPorts);
    }
    if (c.expectPorts != 0) CHECK(n >= 40);  // 1 s at 120 BPM
    else CHECK_EQ(n, 0);
  }
}

EPDJ_TEST(engine_master_stalled_step_is_bounded) {
  Deck d(0);
  d.set(ConfigKey::Mode, 1);
  d.set(ConfigKey::MtcEnable, 1);
  d.run(200);
  hostTransport(d, 1);
  d.run(200);
  d.clearLog();
  d.t += 5000000;  // the engine task was blocked for 5 s
  d.step();
  const size_t events = d.log.size();
  CHECK(events <= 30);  // one beat of clocks (24) at most, never 5 s worth, plus a quarter frame and a radio packet
  d.run(100);
  CHECK(d.log.size() - events < 30);  // and back to the normal rate
}

EPDJ_TEST(engine_master_clock_and_mtc_run_across_the_32bit_microsecond_wrap) {
  // The unit's microsecond counter wraps at 2^32 (71.6 min of uptime); clock, MTC and tempo must not notice.
  for (int64_t startBefore : {5000000ll, 4999000ll, 123456ll}) {
    Deck d(0, 0x0A, 4294967296ll - startBefore - 1000000);  // local time at the first step = 2^32 - startBefore
    d.set(ConfigKey::Mode, 1);
    d.set(ConfigKey::MtcEnable, 1);
    d.run(10);
    hostTransport(d, 1);
    d.run(14000);
    const std::vector<uint64_t> ticks = tickTimes(d.log);
    CHECK(ticks.size() >= 670 && ticks.size() <= 674);  // 14 s at 120 BPM = 672 ticks
    for (size_t k = 1; k < ticks.size(); ++k) {
      const double gap = static_cast<double>(ticks[k] - ticks[k - 1]);
      CHECK(gap > 19900.0 && gap < 21800.0);  // every gap is one period (20833 us) +/- a step, also across the wrap
    }
    size_t qf = 0;
    for (const Rec& r : d.log) qf += (r.kind == OutEvent::Kind::Message && r.bytes.size() == 2 && r.bytes[0] == 0xF1) ? 1u : 0u;
    CHECK(qf > 1600 && qf < 1700);  // 13.99 s x 120
  }
}

// ---- transport -----------------------------------------------------------------------------------

EPDJ_TEST(engine_master_transport_byte_sequences) {
  std::unique_ptr<Deck> d = makeMaster();
  size_t mark = d->log.size();

  hostTransport(*d, 1);  // play
  CHECK_BYTES(transportBytes(d->log, mark), b({0xFA}));
  CHECK(d->engine.transport() == proto::TransportState::Playing);
  const size_t playMark = mark;
  d->run(1200);

  // Pause: Stop, then Song Position in MIDI beats (6 clocks) of what was played.
  mark = d->log.size();
  hostTransport(*d, 2);
  const size_t beats = countClocks(d->log, playMark, mark) / 6;
  CHECK(beats >= 8);
  CHECK_BYTES(transportBytes(d->log, mark), b({0xFC, 0xF2, static_cast<int>(beats & 0x7F), static_cast<int>(beats >> 7)}));
  CHECK(d->engine.transport() == proto::TransportState::Paused);
  d->run(300);

  mark = d->log.size();
  hostTransport(*d, 1);  // continue exactly where followers think we are
  CHECK_BYTES(transportBytes(d->log, mark), b({0xFB}));
  d->run(500);

  // REV: stop + rewind.
  mark = d->log.size();
  d->in.rev = true;
  d->run(30);
  d->in.rev = false;
  d->run(30);
  CHECK_BYTES(transportBytes(d->log, mark), b({0xFC, 0xF2, 0, 0}));
  CHECK(d->engine.transport() == proto::TransportState::Stopped);
  CHECK_EQ(countEq(usbMsgs(d->log, 0, mark), b({0x90, 37, 127})), 0);  // no REV note in this mode

  // REV while stopped: nothing to stop.
  mark = d->log.size();
  d->in.rev = true;
  d->run(30);
  d->in.rev = false;
  d->run(30);
  CHECK_BYTES(transportBytes(d->log, mark), Bytes());

  // A stop from the host while playing is the same as REV.
  hostTransport(*d, 1);
  d->run(400);
  mark = d->log.size();
  hostTransport(*d, 0);
  CHECK_BYTES(transportBytes(d->log, mark), b({0xFC, 0xF2, 0, 0}));

  // Paused, then stop: only the rewind.
  hostTransport(*d, 1);
  d->run(100);
  hostTransport(*d, 2);
  d->run(100);
  mark = d->log.size();
  hostTransport(*d, 0);
  CHECK_BYTES(transportBytes(d->log, mark), b({0xF2, 0, 0}));
  CHECK(d->engine.transport() == proto::TransportState::Stopped);
}

EPDJ_TEST(engine_master_platter_tap_plays_and_pauses) {
  std::unique_ptr<Deck> d = makeMaster();
  auto tap = [&]() {
    for (int i = 0; i < 100; ++i) d->ringStep(0.4, 1.0);
    d->ringIdle(150);
  };
  size_t mark = d->log.size();
  tap();
  CHECK_BYTES(transportBytes(d->log, mark), b({0xFA}));
  CHECK_EQ(countEq(usbMsgs(d->log, 0, mark), b({0x90, 39, 127})), 1);  // the host also sees the play pulse
  CHECK(d->engine.transport() == proto::TransportState::Playing);
  d->run(1000);
  mark = d->log.size();
  tap();
  const Bytes seq = transportBytes(d->log, mark);
  CHECK_EQ(seq.size(), 4);
  if (seq.size() == 4) {
    CHECK_EQ(seq[0], 0xFC);
    CHECK_EQ(seq[1], 0xF2);
  }
  CHECK(d->engine.transport() == proto::TransportState::Paused);

  // Host feedback: "deck is playing" (note 39, velocity > 0) starts the master; the release changes
  // nothing, so a pulse echoed back by a mapping cannot pause it again.
  mark = d->log.size();
  d->hostMsg(b({0x90, 39, 127}));
  CHECK(d->engine.transport() == proto::TransportState::Playing);
  d->hostMsg(b({0x90, 39, 0}));
  d->hostMsg(b({0x80, 39, 0}));
  d->hostMsg(b({0x90, 39, 127}));
  CHECK(d->engine.transport() == proto::TransportState::Playing);
  CHECK_BYTES(transportBytes(d->log, mark), b({0xFB}));  // one Continue, then idempotent
}

// ---- tempo ---------------------------------------------------------------------------------------

EPDJ_TEST(engine_master_cue_taps_set_tempo_to_128_bpm) {
  std::unique_ptr<Deck> d = makeMaster();
  const int gaps[] = {0, 469, 469, 469, 468, 469};  // ms between taps; 128 BPM = 468.75 ms per beat
  for (int g : gaps) {
    d->run(g > 40 ? g - 40 : g);
    d->in.cue = true;
    d->run(20);
    d->in.cue = false;
    d->run(20);
  }
  CHECK_EQ(d->engine.tempoX10(), 1280);
  CHECK_EQ(d->cfg.get(ConfigKey::BpmX10), 1280);

  // Each tap is also reported as a tap note; CUE is not sent as a cue in this mode.
  const std::vector<Bytes> msgs = usbMsgs(d->log, 0);
  CHECK_EQ(countEq(msgs, b({0x90, 41, 127})), 6);
  CHECK_EQ(countEq(msgs, b({0x80, 41, 0})), 6);
  CHECK_EQ(countEq(msgs, b({0x90, 36, 127})), 0);

  // The tempo is announced as the bpm pair, at most 20 Hz, ending on 128.0.
  d->run(200);
  const std::vector<int> bpm = pairValues(usbMsgs(d->log, 0), 0, proto::cc::kBpmMsb, proto::cc::kBpmLsb);
  CHECK(!bpm.empty());
  CHECK_EQ(lastOr(bpm), 1280);
  uint64_t lastT = 0;
  bool first = true;
  for (const Rec& r : d->log) {
    if (r.bytes.size() != 3 || r.bytes[1] != proto::cc::kBpmMsb) continue;
    if (!first) CHECK(r.t - lastT >= 50000);
    first = false;
    lastT = r.t;
  }

  // ... and the clock runs at it: 100 ticks of 468.75 / 24 ms.
  d->clearLog();
  d->run(2200);
  const std::vector<uint64_t> t = tickTimes(d->log);
  CHECK(t.size() > 100);
  if (t.size() > 100) CHECK_NEAR(static_cast<double>(t[100] - t[0]) / 100.0, 468750.0 / 24.0, 12.0);

  // A gap of 2 s forgets the series: one tap on its own changes nothing.
  d->run(2500);
  d->in.cue = true;
  d->run(20);
  d->in.cue = false;
  d->run(20);
  CHECK_EQ(d->engine.tempoX10(), 1280);
}

EPDJ_TEST(engine_master_platter_spin_nudges_tempo_exactly_with_as5600) {
  Deck d(0);
  d.set(ConfigKey::Mode, 1);
  d.set(ConfigKey::PlatterSource, 0);
  d.in.angle12 = 100;
  d.ringIdle(kSettleMs);
  d.clearLog();

  int angle = 100;
  auto spin = [&](int unitsPerMs, int ms, double finger) {
    for (int i = 0; i < ms; ++i) {
      angle = (angle + unitsPerMs + 4096) & 4095;
      d.in.angle12 = static_cast<uint16_t>(angle);
      d.ringStep(0.5, finger);
    }
  };
  spin(0, 100, 1.0);   // finger down, still
  spin(4, 320, 1.0);   // 320 ticks clockwise (4 units = 1 tick): +10 steps
  CHECK_EQ(d.engine.tempoX10(), 1210);
  spin(-4, 160, 1.0);  // 160 ticks counter-clockwise: -5 steps
  CHECK_EQ(d.engine.tempoX10(), 1205);
  spin(4, 31, 1.0);    // 31 ticks: not a step yet ...
  CHECK_EQ(d.engine.tempoX10(), 1205);
  spin(4, 1, 1.0);     // ... the 32nd completes it
  CHECK_EQ(d.engine.tempoX10(), 1206);
  spin(0, 100, 0.0);   // finger up
  spin(8, 600, 0.0);   // untouched spinning is not a tempo control
  CHECK_EQ(d.engine.tempoX10(), 1206);
  spin(0, 50, 1.0);    // touching again starts a fresh count
  spin(4, 31, 1.0);
  spin(0, 50, 0.0);
  spin(0, 50, 1.0);
  spin(4, 31, 1.0);
  CHECK_EQ(d.engine.tempoX10(), 1206);

  CHECK(!ccPresent(usbMsgs(d.log, 0), proto::cc::kJog));  // a tempo wheel in this mode: no jog
  d.run(200);
  const std::vector<int> bpm = pairValues(usbMsgs(d.log, 0), 0, proto::cc::kBpmMsb, proto::cc::kBpmLsb, 1200);
  CHECK(!bpm.empty());
  CHECK_EQ(lastOr(bpm), 1206);
  CHECK_EQ(d.cfg.get(ConfigKey::BpmX10), 1206);
}

EPDJ_TEST(engine_master_ring_spin_nudges_tempo) {
  std::unique_ptr<Deck> d = makeMaster();
  d->ringGesture(0.25, 0.5, 1.0, 100);  // one revolution clockwise = 1024 ticks = 32 steps of 0.1 BPM
  CHECK_NEAR(d->engine.tempoX10(), 1232, 3);
  const int up = d->engine.tempoX10();
  d->ringGesture(0.25, -0.5, 1.0, 100);
  CHECK_NEAR(d->engine.tempoX10(), up - 32, 3);
  CHECK(!ccPresent(usbMsgs(d->log, 0), proto::cc::kJog));
}

EPDJ_TEST(engine_master_host_tempo_controls) {
  std::unique_ptr<Deck> d = makeMaster();
  d->hostMsg(b({0xB0, 20, 10, 0xB0, 52, 5}));  // bpm pair: 128.5
  d->run(5);
  CHECK_EQ(d->engine.tempoX10(), 1285);
  CHECK_EQ(d->cfg.get(ConfigKey::BpmX10), 1285);
  d->hostMsg(b({0xB0, 21, 67}));  // nudge +3 steps
  d->run(5);
  CHECK_EQ(d->engine.tempoX10(), 1288);
  d->hostMsg(b({0xB0, 21, 56}));  // -8
  d->run(5);
  CHECK_EQ(d->engine.tempoX10(), 1280);
  d->run(500);
  CHECK(!ccPresent(usbMsgs(d->log, 0), proto::cc::kBpmMsb));  // the host is not told its own change

  d->run(2500);  // tap tempo from the host: note 41 at 500 ms
  for (int i = 0; i < 5; ++i) {
    d->hostMsg(b({0x90, 41, 127}));
    d->hostMsg(b({0x80, 41, 0}));
    d->run(500);
  }
  CHECK_EQ(d->engine.tempoX10(), 1200);

  // SET_CONFIG bpmX10 (the macOS app) is live, and announced.
  d->clearLog();
  d->set(ConfigKey::BpmX10, 1500);
  d->run(100);
  CHECK_EQ(d->engine.tempoX10(), 1500);
  const std::vector<int> bpm = pairValues(usbMsgs(d->log, 0), 0, proto::cc::kBpmMsb, proto::cc::kBpmLsb);
  CHECK(!bpm.empty());
  CHECK_EQ(lastOr(bpm), 1500);
  d->run(1000);
  const std::vector<uint64_t> t = tickTimes(d->log);
  CHECK(t.size() > 50);
  if (t.size() > 50) CHECK_NEAR(static_cast<double>(t[40] - t[0]) / 40.0, 25e6 / 1500, 30.0);
}

// ---- MTC -----------------------------------------------------------------------------------------

EPDJ_TEST(engine_master_mtc_quarter_frames_and_full_frames) {
  Deck d(0);
  d.set(ConfigKey::Mode, 1);
  d.set(ConfigKey::MtcEnable, 1);  // 30 fps by default
  d.run(100);
  d.clearLog();

  auto fullFrames = [&](size_t from) {
    std::vector<Bytes> v;
    for (size_t i = from; i < d.log.size(); ++i) {
      if (d.log[i].kind == OutEvent::Kind::SysEx) {
        CHECK_EQ(d.log[i].portMask, ports::kUsb | ports::kTrs);
        v.push_back(d.log[i].bytes);
      }
    }
    return v;
  };
  auto quarterFrames = [&](size_t from) {
    std::vector<uint8_t> v;
    for (size_t i = from; i < d.log.size(); ++i) {
      if (d.log[i].kind == OutEvent::Kind::Message && d.log[i].bytes.size() == 2 && d.log[i].bytes[0] == 0xF1) {
        v.push_back(d.log[i].bytes[1]);
      }
    }
    return v;
  };

  hostTransport(d, 1);
  std::vector<Bytes> ff = fullFrames(0);
  CHECK_EQ(ff.size(), 1);
  if (ff.size() == 1) CHECK_BYTES(ff[0], b({0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x60, 0, 0, 0, 0xF7}));  // 00:00:00:00 at 30 fps
  d.run(2000);
  const std::vector<uint8_t> qf = quarterFrames(0);
  CHECK(qf.size() >= 240 && qf.size() <= 242);  // 4 per frame at 30 fps = 120 per second
  const uint8_t firstSeq[8] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x76};  // 0:00:00:00, rate type 3 in piece 7
  for (size_t i = 0; i < qf.size(); ++i) CHECK_EQ(qf[i] >> 4, i % 8);  // pieces 0..7 in order
  for (size_t i = 0; i < 8 && i < qf.size(); ++i) CHECK_EQ(qf[i], firstSeq[i]);
  if (qf.size() > 127) CHECK_EQ(qf[120 + 2], 0x21);  // the sequence starting at frame 30 says seconds = 1

  const size_t pauseMark = d.log.size();
  hostTransport(d, 2);
  ff = fullFrames(pauseMark);
  CHECK_EQ(ff.size(), 1);
  if (ff.size() == 1) CHECK_BYTES(ff[0], b({0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x60, 0, 2, 0, 0xF7}));  // paused at 00:00:02:00
  const size_t qfAtPause = quarterFrames(0).size();
  d.run(500);
  CHECK_EQ(quarterFrames(0).size(), qfAtPause);  // none while paused

  const size_t contMark = d.log.size();
  hostTransport(d, 1);
  ff = fullFrames(contMark);
  CHECK_EQ(ff.size(), 1);
  if (ff.size() == 1) CHECK_BYTES(ff[0], b({0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x60, 0, 2, 0, 0xF7}));  // resumes from 2 s
  d.run(1000);
  CHECK(quarterFrames(0).size() > qfAtPause + 110);

  const size_t stopMark = d.log.size();
  hostTransport(d, 0);
  ff = fullFrames(stopMark);
  CHECK_EQ(ff.size(), 1);
  if (ff.size() == 1) CHECK_BYTES(ff[0], b({0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x60, 0, 0, 0, 0xF7}));  // rewound
  const size_t qfAtStop = quarterFrames(0).size();
  d.run(500);
  CHECK_EQ(quarterFrames(0).size(), qfAtStop);
}

EPDJ_TEST(engine_master_mtc_off_by_default_and_follows_the_frame_rate) {
  Deck d(0);
  d.set(ConfigKey::Mode, 1);
  d.run(100);
  hostTransport(d, 1);
  d.run(1000);
  hostTransport(d, 2);
  for (const Rec& r : d.log) {
    CHECK(!(r.kind == OutEvent::Kind::SysEx));
    CHECK(!(r.kind == OutEvent::Kind::Message && !r.bytes.empty() && r.bytes[0] == 0xF1));
  }

  d.clearLog();  // 25 fps: 100 quarter frames per second, rate type 1 in the full frame
  d.set(ConfigKey::MtcEnable, 1);
  d.set(ConfigKey::MtcFps, 1);
  d.run(5);
  hostTransport(d, 0);
  hostTransport(d, 1);
  d.run(1000);
  size_t qf = 0;
  for (const Rec& r : d.log) {
    if (r.kind == OutEvent::Kind::Message && !r.bytes.empty() && r.bytes[0] == 0xF1) ++qf;
    if (r.kind == OutEvent::Kind::SysEx) CHECK_EQ(r.bytes[5], 0x20);  // (1 << 5) | hours 0
  }
  CHECK(qf >= 100 && qf <= 102);
}

// ---- leaving the master role -------------------------------------------------------------------

EPDJ_TEST(engine_leaving_sync_master_stops_what_it_started) {
  std::unique_ptr<Deck> d = makeMaster();
  hostTransport(*d, 1);
  d->run(800);
  const size_t mark = d->log.size();
  const std::vector<Reply> r = ask(*d, 127, proto::sysex::Cmd::SetConfig, setConfigPayload(ConfigKey::Mode, 0));
  CHECK_EQ(r.size(), 1);
  if (r.size() == 1) CHECK_EQ(r[0].cmd, proto::sysex::kAck);
  CHECK_EQ(static_cast<int>(d->engine.mode()), 0);
  CHECK_BYTES(transportBytes(d->log, mark), b({0xFC, 0xF2, 0, 0}));
  CHECK(d->engine.transport() == proto::TransportState::Stopped);
  const size_t after = d->log.size();
  d->run(2000);
  CHECK_EQ(countClocks(d->log, after, d->log.size()), 0);  // DJ mode: no clock
  CHECK(!anySystemMessage(std::vector<Rec>(d->log.begin() + static_cast<long>(after), d->log.end())));
}

EPDJ_TEST(engine_set_config_enters_master_live) {
  Deck d(0);
  d.run(500);
  CHECK(!anySystemMessage(d.log));
  d.clearLog();
  const std::vector<Reply> r = ask(d, 0, proto::sysex::Cmd::SetConfig, setConfigPayload(ConfigKey::Mode, 1));
  CHECK_EQ(r.size(), 1);
  d.run(1000);
  CHECK(countClocks(d.log, 0, d.log.size()) >= 40);
  CHECK_EQ(static_cast<int>(d.engine.mode()), 1);
}

// ---- SYNC_FOLLOW ---------------------------------------------------------------------------------

EPDJ_TEST(engine_follow_locks_to_24ppqn_and_mirrors_transport) {
  for (Engine::MidiPort port : {Engine::MidiPort::Usb0, Engine::MidiPort::Trs}) {
    Deck d(0);
    d.set(ConfigKey::Mode, 2);
    d.run(100);
    d.clearLog();
    ClockSource src(d, port, 25e6 / 1200, 0.05, 21);  // 120 BPM, +/-5% timestamp jitter

    src.run(200);
    CHECK_EQ(d.engine.displayTempoX10(), d.engine.tempoX10());  // not locked after ~10 ticks
    src.run(600);
    CHECK(src.sent >= 36);
    CHECK_NEAR(d.engine.displayTempoX10(), 1200, 6);
    CHECK_EQ(d.engine.tempoX10(), 1200);  // the unit's own tempo is untouched

    // Transport mirrors Start / Stop / Continue / Song Position.
    auto sendSys = [&](std::initializer_list<int> bytes) { d.midiIn(port, b(bytes)); };
    sendSys({0xFA});
    CHECK(d.engine.transport() == proto::TransportState::Playing);
    int risingEdges = 0;
    bool flash = false;
    for (int i = 0; i < 5000; ++i) {  // 10 beats = 240 ticks = 5 s
      src.step();
      if (d.engine.led().beatFlash && !flash) ++risingEdges;
      flash = d.engine.led().beatFlash;
    }
    CHECK(risingEdges >= 9 && risingEdges <= 11);
    CHECK_NEAR(d.engine.displayTempoX10(), 1200, 6);
    sendSys({0xFC});
    CHECK(d.engine.transport() == proto::TransportState::Paused);
    sendSys({0xFB});
    CHECK(d.engine.transport() == proto::TransportState::Playing);
    sendSys({0xFC});
    sendSys({0xF2, 0, 0});
    CHECK(d.engine.transport() == proto::TransportState::Stopped);

    // Follower mode outputs nothing of its own (cable 1 pass-through of TRS input is not its output).
    CHECK(!anySystemMessage(d.log));

    // The master goes quiet: unlocked after 500 ms, display falls back to the stored tempo.
    src.running = false;
    d.run(700);
    CHECK_EQ(d.engine.displayTempoX10(), d.engine.tempoX10());
    CHECK(!d.engine.led().beatFlash);
  }
}

EPDJ_TEST(engine_follow_other_tempi_and_input_from_trs_passes_through) {
  const double tempi[] = {90.0, 133.3, 174.3};
  for (double bpmV : tempi) {
    Deck d(0);
    d.set(ConfigKey::Mode, 2);
    d.run(50);
    ClockSource src(d, Engine::MidiPort::Trs, 60e6 / (bpmV * 24.0), 0.03, 4);
    src.run(2500);
    CHECK_NEAR(d.engine.displayTempoX10(), bpmV * 10.0, bpmV * 10.0 * 0.005);
    // The clock also appears on USB cable 1 (the jack is passed through to the host).
    size_t onCable1 = 0;
    for (const Rec& r : d.log) {
      if (isClock(r) && (r.portMask & ports::kUsb) && r.cable == 1) ++onCable1;
    }
    CHECK(static_cast<int>(onCable1) >= src.sent - 1);
  }
}

EPDJ_TEST(engine_master_mode_ignores_external_transport_for_its_own_state) {
  std::unique_ptr<Deck> d = makeMaster();
  hostTransport(*d, 1);
  d->run(100);
  d->midiIn(Engine::MidiPort::Usb0, b({0xFC}));  // a stray Stop from the host / another device on TRS
  d->midiIn(Engine::MidiPort::Trs, b({0xFC}));
  CHECK(d->engine.transport() == proto::TransportState::Playing);  // the FSM owns the state
}
