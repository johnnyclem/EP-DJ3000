// Two behaviours of the default build (AS5600 platter + ONE hold pad) that decide what a DJ app sees, through the Engine:
//
//   jogUntouched (contract v1.4): is platter motion with no hand on it sent as jog? AUTO says no when a hold pad is
//   wired on the AS5600 platter (a free-spinning platter coasts for seconds after the hand leaves and would bend the pitch
//   the whole time) and yes otherwise; REPORT always; SUPPRESS never. It is applied where ticks enter the emitter, so
//   telemetry's jogTotal counts everything, and a refused tick is discarded, not queued.
//
//   Motion-sticky touch release (StickyTouch, jog.h): once a touch has lasted longer than 7 s, the hold pad's "released"
//   is only believed after the platter has made no tick for 80 ms. The idea is the original Field Jog firmware's.
//
// Units: 4 counts of the 12-bit angle are one tick; 4 counts per millisecond is one tick per millisecond.
#include <cstdio>
#include <vector>

#include "test_engine_sim.h"

using namespace esim;
using proto::JogUntouched;
using proto::sysex::Cmd;

namespace {

constexpr int kSettleMs = 3600;  // the touch detector learns its baseline for 250 ms and settles for 3 s

struct Seg {
  int ms;
  double perMs;  // counts of angle per millisecond (4 = one tick per ms)
  bool finger;
};

// Plays the segments, one step per millisecond. `sends` says whether the policy in force lets an UNTOUCHED tick out. The
// ledger adds up what a correct unit sends: the ticks counted in a step that is touched, or whose previous step was (the
// touch-up step's ticks still belong to the touch), or every tick when `sends` holds. The unit's own touched flag is the
// LED's, so the ledger does not look at the emission code at all.
void play(Deck& d, const std::vector<Seg>& segs, bool sends, int64_t* expected) {
  for (const Seg& s : segs) {
    for (int i = 0; i < s.ms; ++i) {
      const int32_t before = d.engine.jogTotal();
      const bool was = d.engine.led().touched;
      d.turn(s.perMs, s.finger);
      if (sends || was || d.engine.led().touched) *expected += d.engine.jogTotal() - before;
    }
  }
}

// A scratch with a coast: finger down and still, scratch 200 ticks, hand off while the platter coasts 600 ticks, stop.
// 800 ticks of motion in all, 200 of them made with the finger on.
const std::vector<Seg> kScratchThenCoast = {{150, 0, true}, {200, 4, true}, {600, 4, false}, {300, 0, false}};
constexpr int64_t kScratchThenCoastTicks = 800;

std::vector<Bytes> notes(const std::vector<Bytes>& v) {
  std::vector<Bytes> r;
  for (const Bytes& m : v) {
    if (m.size() == 3 && ((m[0] & 0xF0) == 0x90 || (m[0] & 0xF0) == 0x80)) r.push_back(m);
  }
  return r;
}

size_t countCc(const std::vector<Bytes>& v, uint8_t cc) {
  size_t n = 0;
  for (const Bytes& m : v) n += (m.size() == 3 && (m[0] & 0xF0) == 0xB0 && m[1] == cc) ? 1u : 0u;
  return n;
}

// Time of the first (or last) message equal to `want`, or ~0 if none.
uint64_t timeOfMsg(const std::vector<Rec>& log, const Bytes& want, bool last) {
  uint64_t t = ~0ull;
  for (const Rec& r : log) {
    if (r.kind != OutEvent::Kind::Message || r.bytes != want) continue;
    t = r.t;
    if (!last) break;
  }
  return t;
}

uint64_t timeOfJog(const std::vector<Rec>& log, bool last) {
  uint64_t t = ~0ull;
  for (const Rec& r : log) {
    if (r.kind != OutEvent::Kind::Message || r.bytes.size() != 3 || r.bytes[0] != 0xB0 || r.bytes[1] != proto::cc::kJog) continue;
    t = r.t;
    if (!last) break;
  }
  return t;
}

int32_t lastTelemetryJogTotal(const std::vector<Rec>& log) {
  int32_t v = 0;
  bool seen = false;
  for (const Reply& r : replies(log)) {
    if (r.cmd != static_cast<uint8_t>(proto::sysex::Reply::Telemetry)) continue;
    proto::Telemetry t;
    CHECK(proto::deserialize(r.payload.data(), r.payload.size(), &t));
    v = t.jogTotal;
    seen = true;
  }
  CHECK(seen);
  return v;
}

void setPolicy(Deck& d, JogUntouched p) { d.set(ConfigKey::JogUntouched, static_cast<int32_t>(p)); }

// A hand lands on the pad and lets go: the unit now knows the pad works. A short touch, so the sticky release is not involved.
void proveThePad(Deck& d) {
  for (int i = 0; i < 300; ++i) d.padStep(true);
  for (int i = 0; i < 100; ++i) d.padStep(false);
}

// The same through SysEx, as the app does it: the engine must apply it between two samples without a hiccup.
void setPolicyLive(Deck& d, JogUntouched p) {
  const std::vector<Reply> r = ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::JogUntouched, static_cast<int32_t>(p)));
  CHECK(r.size() == 1 && r[0].cmd == 0x7E);  // ACK
}

}  // namespace

// ---- jogUntouched -----------------------------------------------------------------------------------

EPDJ_TEST(jog_untouched_defaults_to_auto) {
  Deck d(0);
  CHECK_EQ(d.cfg.get(ConfigKey::JogUntouched), static_cast<int>(JogUntouched::Auto));
}

EPDJ_TEST(auto_reports_untouched_motion_until_the_hold_pad_has_registered_a_touch) {
  Deck d(0);  // the shipped unit: one pad is configured, but nothing says it is connected or works
  d.touchIdle(kSettleMs);
  d.clearLog();
  d.spin(4, 500);  // a platter turning on the bench before the pad exists
  d.run(100);
  CHECK_EQ(ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog), 500);  // the encoder works before the pad does

  proveThePad(d);
  d.clearLog();
  d.spin(4, 500);  // from here on an untouched spin is a coast
  d.run(100);
  CHECK_EQ(countCc(usbMsgs(d.log, 0), proto::cc::kJog), 0u);
  CHECK_EQ(d.engine.jogTotal(), 1000);  // the tracker (and telemetry) still count it
}

EPDJ_TEST(the_pad_proof_is_forgotten_when_the_platter_source_changes) {
  Deck d(0);
  d.touchIdle(kSettleMs);
  proveThePad(d);
  d.clearLog();
  d.spin(4, 300);
  d.run(100);
  CHECK_EQ(countCc(usbMsgs(d.log, 0), proto::cc::kJog), 0u);  // proven: the coast is silent

  d.set(ConfigKey::PlatterSource, static_cast<int32_t>(proto::PlatterSource::TouchHoldOnly));
  d.run(20);  // the engine applies a config change between two samples
  d.set(ConfigKey::PlatterSource, static_cast<int32_t>(proto::PlatterSource::As5600));
  d.run(20);
  d.clearLog();
  d.spin(4, 300);
  d.run(100);
  CHECK_EQ(ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog), 300);  // a source change starts the proof over
}

EPDJ_TEST(auto_with_a_hold_pad_sends_every_touched_tick_and_nothing_of_the_coast) {
  Deck d(0);  // the shipped unit: AS5600 + one hold pad, nothing configured
  d.touchIdle(kSettleMs);
  ask(d, 0, Cmd::Stream, b({100}));
  d.clearLog();
  int64_t expected = 0;
  play(d, kScratchThenCoast, /*sends=*/false, &expected);
  d.run(100);

  const std::vector<Bytes> msgs = usbMsgs(d.log, 0);
  CHECK_EQ(d.engine.jogTotal(), kScratchThenCoastTicks);              // the tracker counted all of it
  CHECK_EQ(ccSum(msgs, 0, proto::cc::kJog), expected);               // and the wire carries exactly the touched part
  CHECK(expected >= 200 && expected <= 215);                          // 200 scratched, plus the ticks before the pad let go
  CHECK_EQ(lastTelemetryJogTotal(d.log), kScratchThenCoastTicks);    // telemetry counts everything
  // Nothing at all leaves after the touch-up (the emitter lags the step that made a tick by a couple of ms).
  const uint64_t tOff = timeOfMsg(d.log, b({0x80, 38, 0}), true);
  CHECK(tOff != ~0ull);
  CHECK(timeOfJog(d.log, true) <= tOff + 5000);
  CHECK_EQ(countEq(msgs, b({0x90, 38, 127})), 1);
}

EPDJ_TEST(auto_without_a_hold_pad_reports_everything_so_the_encoder_works_before_the_pad_does) {
  Deck d(0);
  d.in.touchCount = 0;  // EPDJ_TOUCH_PAD_COUNT = 0: nothing wired
  d.run(kSettleMs);
  d.clearLog();
  int64_t expected = 0;
  play(d, kScratchThenCoast, /*sends=*/true, &expected);  // no pad: "touched" never happens, every tick is untouched
  d.run(100);
  CHECK_EQ(expected, kScratchThenCoastTicks);
  CHECK_EQ(ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog), kScratchThenCoastTicks);
  CHECK_EQ(d.engine.jogTotal(), kScratchThenCoastTicks);
}

EPDJ_TEST(report_always_sends_untouched_motion_even_with_a_hold_pad) {
  Deck d(0);
  setPolicy(d, JogUntouched::Report);
  d.touchIdle(kSettleMs);
  d.clearLog();
  int64_t expected = 0;
  play(d, kScratchThenCoast, /*sends=*/true, &expected);
  d.run(100);
  CHECK_EQ(ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog), kScratchThenCoastTicks);  // the whole coast, as a pitch nudge
  CHECK_EQ(d.engine.jogTotal(), kScratchThenCoastTicks);
}

EPDJ_TEST(suppress_never_sends_untouched_motion_with_or_without_a_pad) {
  {  // a pad: the same as AUTO
    Deck d(0);
    setPolicy(d, JogUntouched::Suppress);
    d.touchIdle(kSettleMs);
    d.clearLog();
    int64_t expected = 0;
    play(d, kScratchThenCoast, false, &expected);
    d.run(100);
    CHECK_EQ(ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog), expected);
    CHECK(expected >= 200 && expected <= 215);
    CHECK_EQ(d.engine.jogTotal(), kScratchThenCoastTicks);
  }
  {  // no pad: nothing is ever "touched", so nothing is sent: SUPPRESS means what it says, AUTO's "report instead" does not apply
    Deck d(0);
    d.in.touchCount = 0;
    setPolicy(d, JogUntouched::Suppress);
    d.run(kSettleMs);
    d.clearLog();
    int64_t expected = 0;
    play(d, kScratchThenCoast, false, &expected);
    d.run(100);
    CHECK_EQ(expected, 0);
    CHECK_EQ(countCc(usbMsgs(d.log, 0), proto::cc::kJog), 0u);
    CHECK_EQ(d.engine.jogTotal(), kScratchThenCoastTicks);
  }
}

EPDJ_TEST(jog_total_in_telemetry_is_the_same_whatever_the_policy) {
  int32_t totals[6];
  int n = 0;
  for (int pads = 0; pads <= 1; ++pads) {
    for (JogUntouched p : {JogUntouched::Auto, JogUntouched::Report, JogUntouched::Suppress}) {
      Deck d(0);
      d.in.touchCount = static_cast<uint8_t>(pads);
      setPolicy(d, p);
      d.run(kSettleMs);
      ask(d, 0, Cmd::Stream, b({100}));
      d.clearLog();
      int64_t expected = 0;
      play(d, kScratchThenCoast, true, &expected);
      d.run(100);
      totals[n] = lastTelemetryJogTotal(d.log);
      CHECK_EQ(totals[n], d.engine.jogTotal());
      CHECK_EQ(totals[n], kScratchThenCoastTicks);
      ++n;
    }
  }
  CHECK_EQ(n, 6);
}

EPDJ_TEST(a_suppressed_coast_leaves_no_backlog_to_burst_out_at_the_next_touch_down) {
  Deck d(0);
  d.touchIdle(kSettleMs);
  proveThePad(d);  // AUTO suppresses a coast only once the pad has registered a hand
  d.clearLog();
  d.spin(4, 3000);  // 3 s of a free platter with nobody touching it: 3000 ticks, all refused
  d.run(200);
  CHECK_EQ(d.engine.jogTotal(), 3000);
  CHECK_EQ(countCc(usbMsgs(d.log, 0), proto::cc::kJog), 0u);

  d.clearLog();
  for (int i = 0; i < 300; ++i) d.padStep(true);  // a hand lands on the stopped platter and rests
  d.run(100);
  const std::vector<Bytes> msgs = usbMsgs(d.log, 0);
  CHECK_EQ(countEq(msgs, b({0x90, 38, 127})), 1);   // scratch mode starts...
  CHECK_EQ(countCc(msgs, proto::cc::kJog), 0u);     // ...with a quiet jog wheel, not with the last 3000 ticks
}

EPDJ_TEST(live_policy_changes_over_sysex_apply_cleanly_in_every_direction) {
  Deck d(0);
  d.touchIdle(kSettleMs);
  d.clearLog();
  int64_t expected = 0;
  int64_t moved = 0;  // every tick of motion, whoever sent it
  const auto go = [&](std::vector<Seg> segs, bool sends) {
    play(d, segs, sends, &expected);
    for (const Seg& s : segs) moved += static_cast<int64_t>(s.ms * s.perMs / 4);
  };

  go({{150, 0, true}, {100, 4, true}}, false);  // AUTO: a touched scratch
  setPolicyLive(d, JogUntouched::Suppress);      // ... switched mid-touch: nothing changes while the finger is down
  go({{100, 4, true}}, false);
  go({{300, 4, false}}, false);                  // hand off, the platter coasts: refused
  setPolicyLive(d, JogUntouched::Report);        // switched mid-coast: from this sample on the coast is sent
  go({{200, 4, false}}, true);
  setPolicyLive(d, JogUntouched::Auto);          // and back: refused again, with no leftover
  go({{200, 4, false}, {100, 0, false}}, false);
  go({{150, 0, true}, {100, 4, true}}, false);  // the next touch: its own ticks, none of the coasts'
  go({{100, 0, true}}, false);
  d.run(100);

  CHECK_EQ(d.engine.jogTotal(), moved);
  CHECK_EQ(ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog), expected);
  CHECK(expected < moved);                                  // some was refused...
  CHECK(expected >= 100 + 100 + 200 + 100);                 // ...and the touched scratches and the REPORT window all arrived
  CHECK_EQ(d.cfg.get(ConfigKey::JogUntouched), static_cast<int>(JogUntouched::Auto));
}

EPDJ_TEST(the_ring_ignores_the_policy_because_it_only_ever_moves_while_touched) {
  for (JogUntouched p : {JogUntouched::Auto, JogUntouched::Report, JogUntouched::Suppress}) {
    Deck d(0);
    d.useRing();
    setPolicy(d, p);
    d.ringIdle(kSettleMs);
    d.clearLog();
    d.ringGesture(0.2, 0.5, 0.5, 100);
    d.run(100);
    CHECK(d.engine.jogTotal() > 200);
    CHECK_EQ(ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog), d.engine.jogTotal());
  }
}

// ---- the sticky release -----------------------------------------------------------------------------

namespace {

// Touch down on the hold pad and scratch for `ms` milliseconds, reversing every 500 ms so the platter never rests
// (one tick per millisecond throughout). The pad reads "touched" for the whole time except `dropoutAtMs`..+dropoutMs.
void longScratch(Deck& d, int ms, int dropoutAtMs, int dropoutMs) {
  for (int i = 0; i < 150; ++i) d.padStep(true);  // land and rest: the touch note comes first
  for (int i = 0; i < ms; ++i) {
    const bool dropout = i >= dropoutAtMs && i < dropoutAtMs + dropoutMs;
    d.turn((i / 500) % 2 == 0 ? 4 : -4, !dropout);
  }
}

}  // namespace

EPDJ_TEST(a_long_scratch_rides_out_a_pad_dropout_without_leaving_scratch_mode) {
  Deck d(0);
  d.touchIdle(kSettleMs);
  d.clearLog();
  longScratch(d, 8000, 7500, 40);  // 8 s of scratching; the pad drops out for 40 ms at 7.5 s
  for (int i = 0; i < 300; ++i) d.padStep(true);  // the platter stops under the hand
  d.padIdle(300);                                  // and the hand leaves

  const std::vector<Bytes> n = notes(usbMsgs(d.log, 0));
  CHECK_EQ(n.size(), 2u);  // ONE touch-down and ONE touch-up for the whole 8 s: the dropout is bridged; and it was no tap
  if (n.size() == 2) {
    CHECK_BYTES(n[0], b({0x90, 38, 127}));
    CHECK_BYTES(n[1], b({0x80, 38, 0}));
  }
  const uint64_t tOn = timeOfMsg(d.log, b({0x90, 38, 127}), false);
  const uint64_t tOff = timeOfMsg(d.log, b({0x80, 38, 0}), true);
  CHECK(tOff - tOn > 8000000);
  CHECK_EQ(ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog), d.engine.jogTotal());  // every tick of it, dropout included
}

EPDJ_TEST(a_short_scratch_does_not_get_that_help_the_dropout_is_a_touch_up_and_a_touch_down) {
  Deck d(0);
  d.touchIdle(kSettleMs);
  d.clearLog();
  longScratch(d, 3000, 2500, 40);  // 3 s: shorter than kStickyAfterMs
  for (int i = 0; i < 300; ++i) d.padStep(true);
  d.padIdle(300);
  const std::vector<Bytes> n = notes(usbMsgs(d.log, 0));
  CHECK_EQ(n.size(), 4u);  // down, up (the dropout), down, up
  CHECK_EQ(countEq(n, b({0x90, 38, 127})), 2);
  CHECK_EQ(countEq(n, b({0x80, 38, 0})), 2);
}

EPDJ_TEST(the_sticky_window_opens_after_seven_seconds_not_before) {
  // A dropout in the middle of a scratch, the platter moving all the while. Just under 7 s of touch: released (two
  // touches). Just over: held (one). The exact sample boundary is pinned on StickyTouch itself (test_jog.cpp); here the
  // margin covers the pad detector's own latency.
  for (int ms : {6800, 7200}) {
    Deck d(0);
    d.touchIdle(kSettleMs);
    d.clearLog();
    longScratch(d, ms + 200, ms, 40);  // the dropout comes `ms` into the scratch (150 ms of landing come before)
    for (int i = 0; i < 300; ++i) d.padStep(true);
    d.padIdle(300);
    const size_t downs = countEq(notes(usbMsgs(d.log, 0)), b({0x90, 38, 127}));
    CHECK_EQ(downs, ms < 7000 ? 2u : 1u);
  }
}

namespace {

struct Latency {
  uint64_t releaseT = 0;  // true time of the first step with the pad idle
  uint64_t offT = ~0ull;  // time of the touch-up note
};

// touch for `touchMs` (moving all the time except the last `stillMs`), then the hand leaves; the platter keeps turning for
// `coastMs` after that if > 0. Returns when the pad was released and when the touch note went off.
Latency releaseAfter(int touchMs, int stillMs, int coastMs) {
  Deck d(0);
  d.touchIdle(kSettleMs);
  d.clearLog();
  for (int i = 0; i < 150; ++i) d.padStep(true);
  for (int i = 0; i < touchMs - stillMs; ++i) d.turn((i / 500) % 2 == 0 ? 4 : -4, true);
  for (int i = 0; i < stillMs; ++i) d.padStep(true);
  Latency l;
  l.releaseT = d.t;
  for (int i = 0; i < coastMs; ++i) d.turn(4, false);
  d.padIdle(600);
  l.offT = timeOfMsg(d.log, b({0x80, 38, 0}), true);
  return l;
}

}  // namespace

EPDJ_TEST(a_long_touch_released_while_the_platter_is_still_lets_go_as_fast_as_a_short_one) {
  const Latency shortTouch = releaseAfter(1500, 300, 0);
  const Latency longTouch = releaseAfter(8000, 300, 0);
  CHECK(shortTouch.offT != ~0ull && longTouch.offT != ~0ull);
  const int64_t shortLag = static_cast<int64_t>(shortTouch.offT - shortTouch.releaseT);
  const int64_t longLag = static_cast<int64_t>(longTouch.offT - longTouch.releaseT);
  CHECK(shortLag < 10000);                     // the pad detector's own latency, a few ms
  CHECK(longLag < 10000);                      // ...and nowhere near the 80 ms window: the sticky rule adds nothing when still
  CHECK(longLag - shortLag <= 2000 && shortLag - longLag <= 2000);  // (the pad's noise differs between the two runs)
}

EPDJ_TEST(a_long_touch_released_over_a_spinning_platter_holds_until_the_platter_has_been_still_for_80_ms) {
  // By design (the original's guard): the hand comes off a platter that keeps spinning, and the touch stays on until the
  // platter stops. The coast ticks that go out meanwhile are touched ticks.
  Deck d(0);
  d.touchIdle(kSettleMs);
  d.clearLog();
  for (int i = 0; i < 150; ++i) d.padStep(true);
  for (int i = 0; i < 8000; ++i) d.turn((i / 500) % 2 == 0 ? 4 : -4, true);
  const int32_t before = d.engine.jogTotal();
  for (int i = 0; i < 500; ++i) d.turn(4, false);  // hand off, platter coasts at full speed for half a second
  const uint64_t tStop = d.t;                       // the first still sample
  CHECK(timeOfMsg(d.log, b({0x80, 38, 0}), true) == ~0ull);  // no touch-up yet
  d.padIdle(400);

  const uint64_t tOff = timeOfMsg(d.log, b({0x80, 38, 0}), true);
  CHECK(tOff != ~0ull);
  // The last tick was in the sample before tStop; the release is believed in the update that sees 80 ms without one.
  CHECK(tOff >= tStop + 70000 && tOff <= tStop + 90000);
  CHECK_EQ(countEq(notes(usbMsgs(d.log, 0)), b({0x80, 38, 0})), 1);
  CHECK_EQ(d.engine.jogTotal() - before, 500);
  CHECK_EQ(ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog), d.engine.jogTotal());  // held = touched: the coast was sent
}

EPDJ_TEST(the_ring_is_not_sticky_a_long_touch_lets_go_as_the_finger_lifts) {
  // The rule is for the hold pad. A finger sliding on the experimental ring for 8 s and then lifting must be released
  // within the touch detector's own few milliseconds, not held for 80 ms because the finger was moving a moment ago.
  Deck d(0);
  d.useRing();
  d.ringIdle(kSettleMs);
  d.clearLog();
  double rev = 0.1;
  for (int i = 0; i < 8000; ++i) {
    rev += 0.0002;  // 0.2 rev/s: a tick every 5 ms, so there is always recent motion
    d.ringStep(rev, 1.0);
  }
  const uint64_t tLift = d.t;
  d.ringIdle(300);
  const uint64_t off = timeOfMsg(d.log, b({0x80, 38, 0}), true);
  CHECK(off != ~0ull);
  CHECK(off - tLift < 30000);  // the ring's own release latency (a few ms), well under the 80 ms window
}

EPDJ_TEST(the_hold_only_platter_has_no_motion_so_the_sticky_rule_changes_nothing_for_it) {
  Deck d(0);
  d.set(ConfigKey::PlatterSource, static_cast<int>(proto::PlatterSource::TouchHoldOnly));
  d.touchIdle(kSettleMs);
  d.clearLog();
  longScratch(d, 8000, 7500, 40);  // "scratching" a platter nothing measures
  for (int i = 0; i < 300; ++i) d.padStep(true);
  d.padIdle(300);
  CHECK_EQ(d.engine.jogTotal(), 0);
  CHECK_EQ(countEq(notes(usbMsgs(d.log, 0)), b({0x90, 38, 127})), 2);  // the dropout ends the touch, as before
}

EPDJ_TEST(a_live_platter_change_during_a_sticky_hold_closes_the_touch_and_leaves_nothing_stuck) {
  Deck d(0);
  d.touchIdle(kSettleMs);
  d.clearLog();
  for (int i = 0; i < 150; ++i) d.padStep(true);
  for (int i = 0; i < 8000; ++i) d.turn((i / 500) % 2 == 0 ? 4 : -4, true);
  for (int i = 0; i < 50; ++i) d.turn(4, false);  // the hand lifts off a platter that is still turning: the sticky hold keeps the touch
  CHECK(d.engine.led().touched);
  d.set(ConfigKey::PlatterSource, static_cast<int>(proto::PlatterSource::TouchHoldOnly));  // the old hold must not outlive the switch
  d.padIdle(300);
  const std::vector<Bytes> n = notes(usbMsgs(d.log, 0));
  CHECK_EQ(countEq(n, b({0x90, 38, 127})), 1);  // no second touch starts on the new platter from a stale hold...
  CHECK_EQ(countEq(n, b({0x80, 38, 0})), 1);    // ...and the one touch is closed
  CHECK(!d.engine.led().touched);
}
