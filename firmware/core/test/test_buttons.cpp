#include <vector>

#include "epdj/buttons.h"
#include "test_harness.h"

using namespace epdj;
namespace pb = epdj::proto::buttons;
using Chord = ButtonGestures::Chord;

namespace {

// Drives debouncer -> gestures at 1 kHz from a script of (time ms, raw mask) changes.
struct Rig {
  ButtonDebouncer deb;
  ButtonGestures gest;
  uint8_t raw = 0;
  uint64_t nowUs = 0;
  std::vector<ButtonGestures::Events> log;
  std::vector<uint64_t> logAtUs;

  void tick(uint8_t rawMask) {
    raw = rawMask;
    const auto ev = gest.update(deb.update(raw, nowUs), nowUs);
    if (ev.pressed || ev.released || ev.chord != Chord::None) {
      log.push_back(ev);
      logAtUs.push_back(nowUs);
    }
    nowUs += 1000;
  }
  void hold(uint8_t rawMask, int ms) {
    for (int i = 0; i < ms; ++i) tick(rawMask);
  }
  Chord lastChord() const {
    for (size_t i = log.size(); i-- > 0;)
      if (log[i].chord != Chord::None) return log[i].chord;
    return Chord::None;
  }
  int chordCount() const {
    int n = 0;
    for (const auto& e : log) n += e.chord != Chord::None;
    return n;
  }
};

constexpr uint8_t kBoth = pb::kCue | pb::kRev;

// Both buttons pressed together, held for `heldMs` (measured press tick -> release tick), released together.
Chord chordAfter(int heldMs) {
  Rig r;
  r.hold(0, 10);
  r.tick(kBoth);
  r.hold(kBoth, heldMs - 1);
  r.tick(0);
  r.hold(0, 20);
  return r.lastChord();
}

}  // namespace

EPDJ_TEST(single_press_is_reported_on_the_same_sample) {
  Rig r;
  r.hold(0, 5);
  r.tick(pb::kCue);
  CHECK_EQ(r.log.size(), 1u);
  if (r.log.size() != 1) return;
  CHECK_EQ(r.log[0].pressed, pb::kCue);
  CHECK_EQ(r.logAtUs[0], 5000u);  // first sample with the button down: zero added latency
  r.hold(pb::kCue, 100);
  r.tick(0);
  CHECK_EQ(r.log.size(), 2u);
  if (r.log.size() != 2) return;
  CHECK_EQ(r.log[1].released, pb::kCue);
  CHECK_EQ(r.logAtUs[1], 5000u + 101u * 1000u);
}

EPDJ_TEST(rev_press_is_independent_of_cue) {
  Rig r;
  r.tick(pb::kRev);
  CHECK_EQ(r.log.size(), 1u);
  if (r.log.empty()) return;
  CHECK_EQ(r.log[0].pressed, pb::kRev);
  r.hold(pb::kRev, 50);
  r.tick(pb::kRev | pb::kCue);
  if (r.log.size() < 2) return;
  CHECK_EQ(r.log.back().pressed, pb::kCue);
  CHECK_EQ(r.gest.reportedDown(), kBoth);
}

EPDJ_TEST(bouncing_press_yields_exactly_one_press_and_one_release) {
  Rig r;
  r.deb.setDebounceMs(4);
  r.hold(0, 10);
  // Contact chatter for 3 ms after the real edge: 1 0 1 0 1 then solid.
  const uint8_t press[] = {1, 0, 1, 0, 1, 1, 1, 1, 1, 1};
  for (uint8_t b : press) r.tick(b ? pb::kCue : 0);
  r.hold(pb::kCue, 50);
  CHECK_EQ(r.log.size(), 1u);
  if (r.log.empty()) return;
  CHECK_EQ(r.log[0].pressed, pb::kCue);
  // Release with chatter 1 0 1 0 0 0
  const uint8_t release[] = {0, 1, 0, 1, 0, 0, 0, 0};
  for (uint8_t b : release) r.tick(b ? pb::kCue : 0);
  r.hold(0, 50);
  CHECK_EQ(r.log.size(), 2u);
  if (r.log.size() < 2) return;
  CHECK_EQ(r.log[1].released, pb::kCue);
}

EPDJ_TEST(bounce_that_settles_on_the_opposite_level_is_corrected_after_the_lock) {
  Rig r;
  r.deb.setDebounceMs(4);
  r.hold(0, 5);
  // A 2 ms glitch: reported at once, then the real (released) level wins when the lock ends.
  r.tick(pb::kCue);
  r.tick(pb::kCue);
  r.hold(0, 20);
  CHECK_EQ(r.log.size(), 2u);
  if (r.log.size() < 2) return;
  CHECK_EQ(r.log[0].pressed, pb::kCue);
  CHECK_EQ(r.log[1].released, pb::kCue);
  CHECK_EQ(r.logAtUs[1] - r.logAtUs[0], 4000u);
  CHECK_EQ(r.gest.reportedDown(), 0);
}

EPDJ_TEST(debounce_time_is_configurable_and_per_button) {
  ButtonDebouncer d;
  d.setDebounceMs(10);
  CHECK_EQ(d.update(pb::kCue, 0), pb::kCue);
  CHECK_EQ(d.update(0, 5000), pb::kCue);        // inside the 10 ms lock: ignored
  CHECK_EQ(d.update(pb::kRev, 5000), pb::kCue | pb::kRev);  // other button is not locked
  CHECK_EQ(d.update(pb::kRev, 9999), pb::kCue | pb::kRev);
  CHECK_EQ(d.update(pb::kRev, 10000), pb::kRev);  // lock over: the release is reported
}

EPDJ_TEST(chord_under_two_seconds_does_nothing) {
  CHECK(chordAfter(1900) == Chord::None);
  CHECK(chordAfter(100) == Chord::None);
}

EPDJ_TEST(chord_between_two_and_five_seconds_cycles_mode) {
  CHECK(chordAfter(2100) == Chord::CycleMode);
  CHECK(chordAfter(2000) == Chord::CycleMode);
  CHECK(chordAfter(4900) == Chord::CycleMode);
}

EPDJ_TEST(chord_from_five_seconds_swaps_deck) {
  CHECK(chordAfter(5100) == Chord::SwapDeck);
  CHECK(chordAfter(5000) == Chord::SwapDeck);
  CHECK(chordAfter(4999) == Chord::CycleMode);
  CHECK(chordAfter(9000) == Chord::SwapDeck);
}

EPDJ_TEST(chord_is_timed_from_the_second_press_not_the_first) {
  Rig r;
  r.tick(pb::kCue);
  r.hold(pb::kCue, 800);      // CUE alone for 0.8 s
  r.tick(kBoth);              // REV joins
  r.hold(kBoth, 1200);        // both held 1.2 s: total since CUE is 2.0+ s but the chord is only 1.2 s
  r.tick(0);
  r.hold(0, 20);
  CHECK_EQ(r.chordCount(), 0);
}

EPDJ_TEST(chord_fires_once_on_first_release_and_swallows_the_second_release) {
  Rig r;
  r.hold(0, 5);
  r.tick(kBoth);
  r.hold(kBoth, 2500);
  const size_t before = r.log.size();
  r.tick(pb::kRev);  // CUE let go first
  CHECK_EQ(r.log.size(), before + 1);
  const auto& ev = r.log.back();
  CHECK(ev.chord == Chord::CycleMode);
  CHECK_EQ(ev.released, pb::kCue);
  CHECK_EQ(ev.noteOffMask, pb::kRev);       // engine must note-off REV itself
  CHECK_EQ(r.gest.reportedDown(), 0);       // nothing left for the engine to track
  r.hold(pb::kRev, 300);
  r.tick(0);                                // REV let go: no second note-off
  r.hold(0, 20);
  CHECK_EQ(r.log.size(), before + 1);
  CHECK_EQ(r.chordCount(), 1);
}

EPDJ_TEST(simultaneous_release_has_no_pending_note_off) {
  Rig r;
  r.tick(kBoth);
  r.hold(kBoth, 5200);
  r.tick(0);
  const auto& ev = r.log.back();
  CHECK(ev.chord == Chord::SwapDeck);
  CHECK_EQ(ev.released, kBoth);
  CHECK_EQ(ev.noteOffMask, 0);
  CHECK_EQ(r.gest.reportedDown(), 0);
}

EPDJ_TEST(short_double_press_reports_normal_edges_and_no_swallowing) {
  Rig r;
  r.tick(kBoth);
  r.hold(kBoth, 300);
  r.tick(pb::kRev);
  r.hold(pb::kRev, 100);
  r.tick(0);
  r.hold(0, 20);
  CHECK_EQ(r.chordCount(), 0);
  // press(both), release(cue), release(rev)
  CHECK_EQ(r.log.size(), 3u);
  if (r.log.size() < 3) return;
  CHECK_EQ(r.log[1].released, pb::kCue);
  CHECK_EQ(r.log[2].released, pb::kRev);
}

EPDJ_TEST(a_new_press_after_a_chord_works_normally) {
  Rig r;
  r.tick(kBoth);
  r.hold(kBoth, 2500);
  r.tick(pb::kRev);
  r.tick(0);          // REV swallowed
  r.hold(0, 20);
  const size_t n = r.log.size();
  r.tick(pb::kRev);   // fresh press is reported again
  CHECK_EQ(r.log.size(), n + 1);
  CHECK_EQ(r.log.back().pressed, pb::kRev);
  r.hold(pb::kRev, 30);
  r.tick(0);
  CHECK_EQ(r.log.back().released, pb::kRev);
  CHECK_EQ(r.gest.reportedDown(), 0);
}

EPDJ_TEST(chord_held_time_tracks_progress_for_led_feedback) {
  Rig r;
  r.tick(pb::kCue);
  CHECK_EQ(r.gest.chordHeldMs(r.nowUs), 0u);
  r.hold(pb::kCue, 500);
  r.tick(kBoth);
  const uint64_t start = r.nowUs - 1000;
  r.hold(kBoth, 1000);
  CHECK_EQ(r.gest.chordHeldMs(r.nowUs - 1000), static_cast<uint32_t>((r.nowUs - 1000 - start) / 1000));
  CHECK(r.gest.chordHeldMs(r.nowUs) >= 1000);
  r.tick(0);
  CHECK_EQ(r.gest.chordHeldMs(r.nowUs), 0u);
}

EPDJ_TEST(held_buttons_at_power_up_are_reported_once) {
  Rig r;
  r.tick(pb::kCue);   // first sample already pressed
  CHECK_EQ(r.log.size(), 1u);
  if (r.log.empty()) return;
  CHECK_EQ(r.log[0].pressed, pb::kCue);
  r.hold(pb::kCue, 200);
  CHECK_EQ(r.log.size(), 1u);
}
