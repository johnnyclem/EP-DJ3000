// Engine in DJ mode: platter, fader, knob, buttons and gestures, channels, host feedback, USB presence,
// TRS pass-through. Scripted sessions over simulated sensors, simulated time only.
#include <cstdlib>
#include <memory>

#include "test_engine_sim.h"

using namespace esim;

namespace {

constexpr int kSettleMs = 3600;  // the touch ring learns its baseline for 250 ms and settles for 3 s

std::unique_ptr<Deck> makeDeck(int deckId = 0) {
  std::unique_ptr<Deck> d(new Deck(deckId));
  d->ringIdle(kSettleMs);
  d->clearLog();
  return d;
}

bool allOnChannel(const std::vector<Bytes>& v, uint8_t ch) {
  for (const Bytes& m : v) {
    if (m.empty() || m[0] < 0x80 || m[0] >= 0xF0) continue;
    if ((m[0] & 0x0F) != ch) return false;
  }
  return true;
}

std::vector<Bytes> notesOnly(const std::vector<Bytes>& v) {
  std::vector<Bytes> r;
  for (const Bytes& m : v) {
    if (m.size() == 3 && ((m[0] & 0xF0) == 0x90 || (m[0] & 0xF0) == 0x80)) r.push_back(m);
  }
  return r;
}

// USB cable 0 messages grouped by the step that produced them.
std::vector<std::vector<Bytes>> perStep(const std::vector<Rec>& log) {
  std::vector<std::vector<Bytes>> g;
  uint64_t last = ~0ull;
  for (const Rec& r : log) {
    if (r.kind != OutEvent::Kind::Message || !(r.portMask & ports::kUsb) || r.cable != 0) continue;
    if (r.t != last) g.emplace_back();
    last = r.t;
    g.back().push_back(r.bytes);
  }
  return g;
}

bool ccPresent(const std::vector<Bytes>& v, uint8_t cc) {
  for (const Bytes& m : v) {
    if (m.size() == 3 && (m[0] & 0xF0) == 0xB0 && m[1] == cc) return true;
  }
  return false;
}

}  // namespace

// ---- platter ---------------------------------------------------------------------------------

EPDJ_TEST(engine_ring_sweep_jog_on_channel_with_conservation) {
  for (int deckId = 0; deckId < 2; ++deckId) {
    for (int dir = -1; dir <= 1; dir += 2) {
      std::unique_ptr<Deck> d = makeDeck(deckId);
      d->ringGesture(0.25, 0.5 * dir, 2.0, 100);
      d->run(300);  // let the 500 Hz emitter flush

      const uint8_t ch = static_cast<uint8_t>(deckId);  // Deck B = channel 2 (zero-based 1)
      const std::vector<Bytes> msgs = usbMsgs(d->log, 0);
      CHECK(allOnChannel(msgs, ch));
      const int64_t sum = ccSum(msgs, ch, proto::cc::kJog);
      CHECK_EQ(sum, d->engine.jogTotal());  // every tick the tracker produced reached the host
      CHECK(sum * dir > 0);
      CHECK_NEAR(static_cast<double>(sum) * dir, 2.0 * 1024, 2.0 * 1024 * 0.04);  // two revolutions, within 4%

      // Wire rules: never 64 (no change), never 0, at most one message per 2 ms.
      uint64_t lastT = 0;
      int jogMsgs = 0;
      for (const Rec& r : d->log) {
        if (r.kind != OutEvent::Kind::Message || r.bytes.size() != 3 || r.bytes[1] != proto::cc::kJog) continue;
        CHECK(r.bytes[2] != 64 && r.bytes[2] != 0);
        if (jogMsgs++ > 0) CHECK(r.t - lastT >= 2000);
        lastT = r.t;
      }
      CHECK(jogMsgs > 100);
      CHECK_EQ(countEq(notesOnly(msgs), b({0x90 | ch, 38, 127})), 1);  // touch down
      CHECK_EQ(countEq(notesOnly(msgs), b({0x80 | ch, 38, 0})), 1);    // touch up
    }
  }
}

EPDJ_TEST(engine_as5600_spin_is_exact_and_respects_invert) {
  for (int invert = 0; invert <= 1; ++invert) {
    Deck d(0);
    d.set(ConfigKey::PlatterSource, 0);
    d.set(ConfigKey::JogInvert, invert);
    d.in.angle12 = 1234;
    d.run(50);
    d.clearLog();
    Rng rng(99);
    int64_t units = 0;  // angle units travelled, clockwise positive
    int angle = 1234;
    for (int i = 0; i < 8000; ++i) {
      const int speed = (i / 1000) % 2 == 0 ? 20 : -35;  // units per ms: about 5 and 8.5 ticks per ms
      const int delta = speed + rng.range(-3, 3);
      angle = (angle + delta + 4096) & 4095;
      units += delta;
      d.in.angle12 = static_cast<uint16_t>(angle);
      d.step();
    }
    d.run(300);
    const int64_t sum = ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog);
    CHECK_EQ(sum, d.engine.jogTotal());
    // Exact integer conservation: at most the sub-tick remainder (< 4 units) is held back.
    const int64_t want = invert ? -units : units;
    CHECK(std::llabs(sum * 4 - want) < 4);
    CHECK(std::llabs(sum) > 1000);
  }
}

EPDJ_TEST(engine_touch_and_tap_notes_and_play_pulse) {
  std::unique_ptr<Deck> d = makeDeck();
  for (int i = 0; i < 100; ++i) d->ringStep(0.4, 1.0);  // a quick, motionless touch
  d->ringIdle(100);
  const std::vector<Bytes> notes = notesOnly(usbMsgs(d->log, 0));
  CHECK_EQ(notes.size(), 4);
  if (notes.size() == 4) {
    CHECK_BYTES(notes[0], b({0x90, 38, 127}));
    CHECK_BYTES(notes[1], b({0x80, 38, 0}));
    CHECK_BYTES(notes[2], b({0x90, 39, 127}));  // play pulse: 127 ...
    CHECK_BYTES(notes[3], b({0x80, 39, 0}));    // ... then 0
  }
  uint64_t on = 0, off = 0;
  for (const Rec& r : d->log) {
    if (r.bytes.size() == 3 && r.bytes[1] == 39) (r.bytes[0] == 0x90 ? on : off) = r.t;
  }
  CHECK(off - on >= 10000 && off - on <= 12000);

  // tapPlay = 0: the touch notes only.
  d->clearLog();
  d->set(ConfigKey::TapPlay, 0);
  for (int i = 0; i < 100; ++i) d->ringStep(0.4, 1.0);
  d->ringIdle(100);
  CHECK_EQ(notesOnly(usbMsgs(d->log, 0)).size(), 2);
}

EPDJ_TEST(engine_long_or_moving_touch_is_not_a_tap) {
  std::unique_ptr<Deck> d = makeDeck();
  for (int i = 0; i < 400; ++i) d->ringStep(0.4, 1.0);  // held longer than tapMaxMs (180 ms)
  d->ringIdle(100);
  CHECK_EQ(countEq(notesOnly(usbMsgs(d->log, 0)), b({0x90, 39, 127})), 0);

  d->clearLog();
  d->ringGesture(0.2, 0.5, 0.2, 20, 0);  // short but travels far more than tapMoveTicks
  CHECK_EQ(countEq(notesOnly(usbMsgs(d->log, 0)), b({0x90, 39, 127})), 0);
  CHECK(ccSum(usbMsgs(d->log, 0), 0, proto::cc::kJog) > 100);
}

EPDJ_TEST(engine_platter_source_change_does_not_strand_the_touch_note) {
  std::unique_ptr<Deck> d = makeDeck();
  for (int i = 0; i < 200; ++i) d->ringStep(0.4, 1.0);  // finger resting
  CHECK_EQ(countEq(notesOnly(usbMsgs(d->log, 0)), b({0x90, 38, 127})), 1);
  d->set(ConfigKey::PlatterSource, 0);  // live switch to the AS5600 while touched
  for (int i = 0; i < 50; ++i) d->ringStep(0.4, 1.0);
  d->ringIdle(100);
  // Every touch-down has its touch-up before the next one: the host never sees a doubled note-on.
  int open = 0, downs = 0;
  for (const Bytes& m : notesOnly(usbMsgs(d->log, 0))) {
    if (m[1] != 38) continue;
    open += m[0] == 0x90 ? 1 : -1;
    downs += m[0] == 0x90 ? 1 : 0;
    CHECK(open == 0 || open == 1);
  }
  CHECK_EQ(open, 0);
  CHECK_EQ(downs, 2);  // the finger that stayed down is a new touch for the new platter source
}

// ---- fader and knob ----------------------------------------------------------------------------

EPDJ_TEST(engine_fader_sweep_14bit_pairs_msb_first_for_every_role) {
  struct Role {
    int role;
    uint8_t msb, lsb;
  };
  const Role roles[] = {{0, 23, 55}, {1, 7, 39}, {2, 25, 57}};
  for (const Role& r : roles) {
    Deck d(0);
    d.set(ConfigKey::FaderRole, r.role);
    d.in.faderRaw = 40;
    d.run(100);
    d.clearLog();

    for (int raw = 40; raw <= 4055; raw += 8) {
      d.in.faderRaw = static_cast<uint16_t>(raw);
      d.step();
    }
    d.run(100);
    int last = -1;
    size_t distinct = 0;
    midi::Cc14Receiver rx(r.msb, r.lsb);  // one receiver for the whole run, like a host
    for (const std::vector<Bytes>& g : perStep(d.log)) {
      CHECK(g.size() <= 2);
      for (const Bytes& m : g) CHECK(m[1] == r.msb || m[1] == r.lsb);  // only this role's CCs appear
      if (g.size() == 2) {
        CHECK_EQ(g[0][1], r.msb);  // MSB first, then LSB
        CHECK_EQ(g[1][1], r.lsb);
      }
      uint16_t v = 0;
      for (const Bytes& m : g) rx.onControlChange(m[1], m[2], &v);
      CHECK(static_cast<int>(v) >= last);  // monotonic up-ramp
      if (static_cast<int>(v) != last) ++distinct;
      last = v;
    }
    CHECK_EQ(last, 16383);  // rawMax end of travel
    CHECK(distinct > 400);

    d.clearLog();
    for (int raw = 4055; raw >= 40; raw -= 8) {
      d.in.faderRaw = static_cast<uint16_t>(raw);
      d.step();
    }
    d.run(100);
    const std::vector<int> down = pairValues(usbMsgs(d.log, 0), 0, r.msb, r.lsb);
    CHECK(!down.empty());
    CHECK_EQ(lastOr(down), 0);

    d.in.faderRaw = 2048;  // mid travel -> mid scale
    d.run(100);
    const std::vector<int> mid = pairValues(usbMsgs(d.log, 0), 0, r.msb, r.lsb);
    CHECK_NEAR(lastOr(mid), 8192, 12);
  }
}

EPDJ_TEST(engine_fader_does_not_chatter_and_reacts_within_a_step) {
  Deck d(0);
  d.in.faderRaw = 2000;
  d.run(200);
  d.clearLog();
  Rng rng(5);
  for (int i = 0; i < 3000; ++i) {  // +/-3 counts of ADC noise on a still pot
    d.in.faderRaw = static_cast<uint16_t>(2000 + rng.range(-3, 3));
    d.step();
  }
  CHECK(!ccPresent(usbMsgs(d.log, 0), proto::cc::kCrossfaderMsb));
  CHECK(!ccPresent(usbMsgs(d.log, 0), proto::cc::kCrossfaderLsb));

  const uint64_t moveT = d.t;  // a real movement of 100 counts: reported by the step that saw it (< 2 ms)
  d.in.faderRaw = 2100;
  d.step();
  d.step();
  bool seen = false;
  for (const Rec& r : d.log) {
    if (r.bytes.size() == 3 && r.bytes[1] == proto::cc::kCrossfaderMsb) {
      seen = true;
      CHECK(r.t - moveT < 2000);
    }
  }
  CHECK(seen);
}

EPDJ_TEST(engine_fader_role_change_resends_on_the_new_pair) {
  Deck d(0);
  d.in.faderRaw = 3000;
  d.run(200);
  d.clearLog();
  d.set(ConfigKey::FaderRole, 1);  // CHANNEL_VOLUME, pot untouched
  d.run(5);
  const std::vector<Bytes> m = usbMsgs(d.log, 0);
  CHECK_EQ(m.size(), 2);
  if (m.size() == 2) {
    CHECK_EQ(m[0][1], proto::cc::kChannelVolumeMsb);
    CHECK_EQ(m[1][1], proto::cc::kChannelVolumeLsb);
  }
  CHECK(!ccPresent(m, proto::cc::kCrossfaderMsb));
}

EPDJ_TEST(engine_knob_generic_and_bpm_roles) {
  Deck d(0);
  d.in.knobRaw = 0;
  d.run(100);
  d.clearLog();
  d.in.knobRaw = 4095;
  d.run(100);
  const std::vector<int> v = pairValues(usbMsgs(d.log, 0), 0, proto::cc::kKnobMsb, proto::cc::kKnobLsb);
  CHECK(!v.empty());
  CHECK_EQ(lastOr(v), 16383);
  CHECK_EQ(d.engine.tempoX10(), 1200);  // a generic knob leaves the tempo alone

  d.set(ConfigKey::KnobRole, 1);  // BPM: 60.0 + 120.0 x position
  d.in.knobRaw = 0;
  d.run(100);
  CHECK_EQ(d.engine.tempoX10(), 600);
  d.in.knobRaw = 4095;
  d.run(100);
  CHECK_EQ(d.engine.tempoX10(), 1800);
  d.in.knobRaw = 0;
  d.run(100);
  d.clearLog();
  for (int raw = 0; raw <= 4095; raw += 14) {  // a 300 ms turn
    d.in.knobRaw = static_cast<uint16_t>(raw);
    d.step();
  }
  d.in.knobRaw = 2048;
  d.run(300);
  CHECK_NEAR(d.engine.tempoX10(), 1200, 5);
  CHECK_EQ(d.cfg.get(ConfigKey::BpmX10), d.engine.tempoX10());  // GET_CONFIG / SAVE_CONFIG see the live tempo

  // The tempo goes out as the bpm CC pair (14-bit, BPM x 10), at most 20 Hz, ending on the final value.
  const std::vector<Bytes> msgs = usbMsgs(d.log, 0);
  CHECK(!ccPresent(msgs, proto::cc::kKnobMsb));
  const std::vector<int> bpm = pairValues(msgs, 0, proto::cc::kBpmMsb, proto::cc::kBpmLsb);
  CHECK(!bpm.empty());
  CHECK_EQ(lastOr(bpm), d.engine.tempoX10());
  std::vector<uint64_t> times;
  uint64_t lastT = ~0ull;
  for (const Rec& r : d.log) {
    if (r.bytes.size() == 3 && (r.bytes[1] == proto::cc::kBpmMsb || r.bytes[1] == proto::cc::kBpmLsb) && r.t != lastT) {
      times.push_back(r.t);
      lastT = r.t;
    }
  }
  CHECK(times.size() >= 4);
  for (size_t i = 1; i < times.size(); ++i) CHECK(times[i] - times[i - 1] >= 50000);
}

// ---- buttons -----------------------------------------------------------------------------------

EPDJ_TEST(engine_buttons_send_notes_without_delay) {
  Deck d(0);
  d.run(50);
  d.clearLog();
  const uint64_t downT = d.t;
  d.in.cue = true;
  d.run(30);
  d.in.cue = false;
  d.run(30);
  d.in.rev = true;
  d.run(30);
  d.in.rev = false;
  d.run(30);
  const std::vector<Bytes> n = notesOnly(usbMsgs(d.log, 0));
  CHECK_EQ(n.size(), 4);
  if (n.size() == 4) {
    CHECK_BYTES(n[0], b({0x90, 36, 127}));
    CHECK_BYTES(n[1], b({0x80, 36, 0}));
    CHECK_BYTES(n[2], b({0x90, 37, 127}));
    CHECK_BYTES(n[3], b({0x80, 37, 0}));
  }
  CHECK_EQ(d.log.front().t, downT);  // the press leaves in the step that saw it
}

EPDJ_TEST(engine_button_bounce_is_swallowed) {
  Deck d(0);
  d.run(50);
  d.clearLog();
  const bool press[] = {true, false, true, false, true, true, true, true, true, true, true, true};
  for (bool p : press) {
    d.in.cue = p;
    d.step();
  }
  d.run(20);
  const bool release[] = {false, true, false, true, false, false, false, false, false, false, false, false};
  for (bool p : release) {
    d.in.cue = p;
    d.step();
  }
  d.run(20);
  const std::vector<Bytes> n = notesOnly(usbMsgs(d.log, 0));
  CHECK_EQ(n.size(), 2);
  if (n.size() == 2) {
    CHECK_BYTES(n[0], b({0x90, 36, 127}));
    CHECK_BYTES(n[1], b({0x80, 36, 0}));
  }
}

EPDJ_TEST(engine_chord_cycles_modes_and_releases_notes_once) {
  Deck d(0);
  d.run(50);
  const proto::Mode expect[] = {proto::Mode::SyncMaster, proto::Mode::SyncFollow, proto::Mode::Dj};
  for (proto::Mode m : expect) {
    d.clearLog();
    d.in.cue = d.in.rev = true;
    d.run(2100);
    CHECK_EQ(d.engine.led().chordStage, 1);  // the LED tells the user the 2 s threshold has passed
    d.in.rev = false;
    d.step();  // the first release completes the chord
    d.in.cue = false;
    d.run(200);
    CHECK_EQ(d.engine.led().chordStage, 0);
    CHECK_EQ(static_cast<int>(d.engine.mode()), static_cast<int>(m));
    CHECK_EQ(d.cfg.get(ConfigKey::Mode), static_cast<int>(m));
    CHECK(d.plat.deckSets.empty());

    // Whatever the mode of the moment, every note-on got exactly one note-off (SYNC_MASTER has just the tap note).
    int on = 0, off = 0;
    for (const Bytes& n : notesOnly(usbMsgs(d.log, 0))) (n[0] == 0x90 ? on : off)++;
    CHECK(on >= 1);
    CHECK_EQ(on, off);
    d.run(2000);
  }
  // Under 2 s: nothing happens.
  d.clearLog();
  d.in.cue = d.in.rev = true;
  d.run(1500);
  d.in.cue = d.in.rev = false;
  d.run(100);
  CHECK_EQ(static_cast<int>(d.engine.mode()), 0);
}

EPDJ_TEST(engine_chord_over_five_seconds_swaps_deck) {
  Deck d(0);
  d.run(50);
  d.clearLog();
  d.in.cue = d.in.rev = true;
  d.run(5100);
  CHECK_EQ(d.engine.led().chordStage, 2);
  d.in.cue = false;
  d.step();
  d.in.rev = false;
  d.run(100);
  CHECK_EQ(d.plat.deckSets.size(), 1);
  if (!d.plat.deckSets.empty()) CHECK(d.plat.deckSets[0] == proto::Deck::B);
  CHECK_EQ(d.cfg.get(ConfigKey::DeckId), 1);
  CHECK_EQ(static_cast<int>(d.engine.mode()), 0);  // a swap does not also cycle the mode
  int on = 0, off = 0;
  for (const Bytes& n : notesOnly(usbMsgs(d.log, 0))) (n[0] == 0x90 ? on : off)++;
  CHECK_EQ(on, 2);
  CHECK_EQ(off, 2);

  // And back: now deck B.
  d.clearLog();
  d.in.cue = d.in.rev = true;
  d.run(5100);
  d.in.cue = d.in.rev = false;
  d.run(100);
  CHECK_EQ(d.plat.deckSets.size(), 2);
  if (d.plat.deckSets.size() == 2) CHECK(d.plat.deckSets[1] == proto::Deck::A);
}

// ---- modes, channels ---------------------------------------------------------------------------

EPDJ_TEST(engine_dj_mode_emits_no_clock_transport_or_mtc) {
  std::unique_ptr<Deck> d = makeDeck();
  d->set(ConfigKey::MtcEnable, 1);
  d->set(ConfigKey::ClockOutMask, 7);
  d->ringGesture(0.2, 0.5, 1.0, 100);
  d->hostMsg(b({0xB0, 29, 1}));  // even a host asking for transport changes only the mirror
  d->hostMsg(b({0x90, 39, 127}));
  for (int i = 0; i < 20; ++i) d->click(i % 2 == 0, 40);
  d->run(10000);
  CHECK(!anySystemMessage(d->log));
  CHECK(d->engine.transport() == proto::TransportState::Playing);
}

EPDJ_TEST(engine_deck_b_uses_channel_two_for_everything) {
  std::unique_ptr<Deck> d = makeDeck(1);
  d->ringGesture(0.3, 0.5, 1.0, 100);
  for (int raw = 100; raw < 4000; raw += 10) {
    d->in.faderRaw = static_cast<uint16_t>(raw);
    d->in.knobRaw = static_cast<uint16_t>(4095 - raw);
    d->step();
  }
  d->click(true, 20);
  d->click(false, 20);
  d->set(ConfigKey::KnobRole, 1);
  d->in.knobRaw = 100;
  d->run(200);
  const std::vector<Bytes> msgs = usbMsgs(d->log, 0);
  CHECK(msgs.size() > 100);
  CHECK(allOnChannel(msgs, 1));
  CHECK(ccPresent(msgs, proto::cc::kJog) && ccPresent(msgs, proto::cc::kCrossfaderMsb) &&
        ccPresent(msgs, proto::cc::kKnobMsb) && ccPresent(msgs, proto::cc::kBpmMsb));
  CHECK_EQ(countEq(msgs, b({0x91, 36, 127})), 1);
}

EPDJ_TEST(engine_channel_change_cannot_strand_a_note) {
  Deck d(0);
  d.run(50);
  d.clearLog();
  d.in.cue = true;
  d.run(20);
  d.set(ConfigKey::MidiChannel, 9);  // changed while CUE is held
  d.run(20);
  d.in.cue = false;
  d.run(20);
  d.in.cue = true;
  d.run(20);
  d.in.cue = false;
  d.run(20);
  const std::vector<Bytes> n = notesOnly(usbMsgs(d.log, 0));
  CHECK_EQ(n.size(), 4);
  if (n.size() == 4) {
    CHECK_BYTES(n[0], b({0x90, 36, 127}));
    CHECK_BYTES(n[1], b({0x80, 36, 0}));  // the off goes to the channel the on went to
    CHECK_BYTES(n[2], b({0x99, 36, 127}));
    CHECK_BYTES(n[3], b({0x89, 36, 0}));
  }
}

// ---- host feedback -----------------------------------------------------------------------------

EPDJ_TEST(engine_host_feedback_drives_leds_and_stores_tempo) {
  Deck d(0);
  d.run(50);
  d.clearLog();
  d.hostMsg(b({0x90, 36, 127}));
  d.step();
  CHECK(d.engine.led().cueLit);
  d.hostMsg(b({0x80, 36, 0}));
  d.step();
  CHECK(!d.engine.led().cueLit);
  d.hostMsg(b({0x91, 36, 127}));  // another deck's channel
  d.step();
  CHECK(!d.engine.led().cueLit);
  d.hostMsg(b({0x90, 36, 100, 36, 0}));  // running status: on, then note-on velocity 0 = off
  d.step();
  CHECK(!d.engine.led().cueLit);

  d.hostMsg(b({0x90, 39, 127}));
  d.step();
  CHECK(d.engine.led().transport == proto::TransportState::Playing);
  d.hostMsg(b({0x90, 39, 0}));
  d.step();
  CHECK(d.engine.led().transport == proto::TransportState::Paused);
  d.hostMsg(b({0xB0, 29, 0}));
  d.step();
  CHECK(d.engine.led().transport == proto::TransportState::Stopped);
  d.hostMsg(b({0xB0, 29, 1}));
  d.step();
  CHECK(d.engine.led().transport == proto::TransportState::Playing);
  d.hostMsg(b({0xB0, 29, 9}));  // not a transport state
  d.step();
  CHECK(d.engine.led().transport == proto::TransportState::Playing);
  d.hostMsg(b({0xB0, 29, 2}));
  d.step();
  CHECK(d.engine.led().transport == proto::TransportState::Paused);

  // Outside SYNC_MASTER the tempo is only stored (for display): no output, no echo to the host.
  d.hostMsg(b({0xB0, 20, 10, 0xB0, 52, 5}));
  d.step();
  CHECK_EQ(d.engine.tempoX10(), 1285);
  d.hostMsg(b({0xB0, 21, 67}));
  d.step();
  CHECK_EQ(d.engine.tempoX10(), 1288);
  d.hostMsg(b({0xB0, 21, 60}));
  d.step();
  CHECK_EQ(d.engine.tempoX10(), 1284);
  d.hostMsg(b({0xB0, 20, 0, 0xB0, 52, 0}));  // below 20.0 BPM: clamped
  d.step();
  CHECK_EQ(d.engine.tempoX10(), 200);
  d.run(200);
  CHECK(!ccPresent(usbMsgs(d.log, 0), proto::cc::kBpmMsb));
  CHECK(!anySystemMessage(d.log));
}

EPDJ_TEST(engine_host_clock_gives_display_tempo_in_dj_mode) {
  Deck d(0);
  d.run(50);
  const uint8_t clk = 0xF8;
  const uint64_t base = d.localNow();
  for (int i = 0; i < 60; ++i) {  // 120 BPM, timestamped at the moment of receipt
    const uint64_t at = base + static_cast<uint64_t>(std::llround(i * 25e6 / 1200));
    while (d.localNow() < at) d.step();
    d.engine.onMidiIn(Engine::MidiPort::Usb0, &clk, 1, at);
  }
  CHECK_NEAR(d.engine.displayTempoX10(), 1200, 3);
  CHECK_EQ(d.engine.tempoX10(), 1200);  // the stored tempo is not overwritten by what is merely observed
  d.hostMsg(b({0xFA}));
  CHECK(d.engine.transport() == proto::TransportState::Playing);
  d.hostMsg(b({0xFC}));
  CHECK(d.engine.transport() == proto::TransportState::Paused);
  d.run(700);  // clock gone: the display falls back to the stored tempo
  d.engine.onMidiIn(Engine::MidiPort::Usb0, &clk, 1, d.localNow());
  CHECK_EQ(d.engine.displayTempoX10(), d.engine.tempoX10());
  CHECK(!anySystemMessage(d.log));
}

EPDJ_TEST(engine_time_never_runs_backwards) {
  Deck d(0);
  d.set(ConfigKey::BpmX10, 1000);  // stored 100.0 BPM, so the display tells a lock (120) from the fallback (100)
  d.run(50);
  const uint8_t clk = 0xF8;
  const uint64_t base = d.localNow();
  for (int i = 0; i < 60; ++i) {
    const uint64_t at = base + static_cast<uint64_t>(std::llround(i * 25e6 / 1200));
    while (d.localNow() < at) d.step();
    d.engine.onMidiIn(Engine::MidiPort::Usb0, &clk, 1, at);
  }
  CHECK_NEAR(d.engine.displayTempoX10(), 1200, 3);
  RawInputs stale = d.in;  // a step stamped before the last tick must not make "time since the tick" wrap
  stale.nowUs = d.localNow() - 200000;
  d.engine.step(stale);
  CHECK_NEAR(d.engine.displayTempoX10(), 1200, 3);
}

// ---- USB presence --------------------------------------------------------------------------------

EPDJ_TEST(engine_unmounted_usb_is_silent_and_mount_resends_state) {
  Deck d(0);
  d.set(ConfigKey::PlatterSource, 0);
  d.in.usbMounted = false;
  d.in.faderRaw = 1000;
  d.in.knobRaw = 3000;
  d.run(100);
  int angle = 0;
  for (int i = 0; i < 2000; ++i) {  // lots of platter motion, pot moves, buttons, with nobody listening
    angle = (angle + 12) & 4095;
    d.in.angle12 = static_cast<uint16_t>(angle);
    d.in.faderRaw = static_cast<uint16_t>(1000 + i);
    d.in.cue = (i / 300) % 2 == 0;
    d.step();
  }
  d.in.cue = false;
  d.run(100);
  for (const Rec& r : d.log) CHECK((r.portMask & ports::kUsb) == 0);

  d.clearLog();
  d.in.usbMounted = true;
  d.run(300);
  const std::vector<Bytes> m = usbMsgs(d.log, 0);
  CHECK(ccPresent(m, proto::cc::kCrossfaderMsb) && ccPresent(m, proto::cc::kCrossfaderLsb));  // current state, both halves
  CHECK(ccPresent(m, proto::cc::kKnobMsb) && ccPresent(m, proto::cc::kKnobLsb));
  CHECK_EQ(ccSum(m, 0, proto::cc::kJog), 0);  // motion made while nobody listened is not replayed
  const std::vector<int> f = pairValues(m, 0, proto::cc::kCrossfaderMsb, proto::cc::kCrossfaderLsb);
  CHECK(!f.empty());
  CHECK_NEAR(lastOr(f), 12195, 150);  // where the pot is now (raw 2999)
}

// ---- TRS ---------------------------------------------------------------------------------------

EPDJ_TEST(engine_trs_passthrough_both_ways) {
  Deck d(0);
  d.run(10);
  d.clearLog();
  // USB cable 1 -> TRS: bytes are forwarded as they come.
  d.midiIn(Engine::MidiPort::Usb1, b({0x90, 60, 100}));
  d.midiIn(Engine::MidiPort::Usb1, b({0xF0, 0x7D, 0x01}));
  d.midiIn(Engine::MidiPort::Usb1, b({0x02, 0x03, 0xF7}));
  d.midiIn(Engine::MidiPort::Usb1, b({0xF8}));
  Bytes toTrs;
  for (const Rec& r : d.log) {
    if (r.kind == OutEvent::Kind::Bytes) {
      CHECK_EQ(r.portMask, ports::kTrs);
      toTrs.insert(toTrs.end(), r.bytes.begin(), r.bytes.end());
    }
  }
  CHECK_BYTES(toTrs, b({0x90, 60, 100, 0xF0, 0x7D, 0x01, 0x02, 0x03, 0xF7, 0xF8}));

  // TRS -> USB cable 1: parsed (running status, real-time in the middle of a message, SysEx).
  d.clearLog();
  d.midiIn(Engine::MidiPort::Trs, b({0x91, 60, 100, 61, 0xF8, 90, 0xF0, 0x7D, 0x05, 0xF7}));
  const std::vector<Bytes> c1 = usbMsgs(d.log, 1);
  CHECK_EQ(c1.size(), 3);
  if (c1.size() == 3) {
    CHECK_BYTES(c1[0], b({0x91, 60, 100}));
    CHECK_BYTES(c1[1], b({0xF8}));  // real-time overtakes the half-built running-status message
    CHECK_BYTES(c1[2], b({0x91, 61, 90}));
  }
  const std::vector<Rec> sx = sysexRecs(d.log);
  CHECK_EQ(sx.size(), 1);
  if (sx.size() == 1) {
    CHECK_EQ(sx[0].cable, proto::cable::kTrsMidi);
    CHECK_BYTES(sx[0].bytes, b({0xF0, 0x7D, 0x05, 0xF7}));
  }
}

EPDJ_TEST(engine_trs_disable_silences_the_jack) {
  Deck d(0);
  d.set(ConfigKey::TrsMidiEnable, 0);
  d.run(10);
  d.clearLog();
  d.midiIn(Engine::MidiPort::Usb1, b({0x90, 60, 100}));
  d.midiIn(Engine::MidiPort::Trs, b({0x90, 60, 100}));
  CHECK(d.log.empty());
}
