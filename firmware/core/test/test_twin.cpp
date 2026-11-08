#include "epdj/twin.h"
#include "test_harness.h"

using namespace epdj;

namespace {

twin::Out step(twin::Bank& b, const twin::Sample& s) {
  twin::Out o;
  b.step(s, &o);
  return o;
}

bool hasNote(const twin::Out& o, uint8_t ch, uint8_t note, uint8_t vel) {
  for (uint8_t i = 0; i < o.count; ++i) {
    const midi::Message& m = o.msg[i];
    if (m.len == 3 && (m.b[0] & 0x0F) == ch && m.b[1] == note && m.b[2] == vel) return true;
  }
  return false;
}

bool hasCc(const twin::Out& o, uint8_t ch, uint8_t cc) {
  for (uint8_t i = 0; i < o.count; ++i) {
    const midi::Message& m = o.msg[i];
    if (m.isControlChange() && m.channel() == ch && m.data1() == cc) return true;
  }
  return false;
}

}  // namespace

EPDJ_TEST(cue_a_is_left_to_the_engine) {
  twin::Bank b;
  twin::Sample s;
  s.buttons = twin::kCueA;
  const twin::Out o = step(b, s);
  CHECK(!hasNote(o, 0, proto::note::kCue, 127));
  CHECK_EQ(o.count, 4);  // pitch B and the crossfader each send MSB then LSB on the first sample
}

EPDJ_TEST(cue_b_is_channel_2) {
  twin::Bank b;
  twin::Sample s;
  s.nowUs = 1000000;  // past the analog "first sample" by using a fresh bank's first step for buttons
  s.buttons = twin::kCueB;
  twin::Out o = step(b, s);
  CHECK(hasNote(o, 1, proto::note::kCue, 127));
  s.nowUs += 5000;
  s.buttons = 0;
  o = step(b, s);
  CHECK(hasNote(o, 1, proto::note::kCue, 0));
}

EPDJ_TEST(play_is_a_pulse) {
  twin::Bank b;
  twin::Sample s;
  s.buttons = twin::kPlayA;
  twin::Out o = step(b, s);
  CHECK(!hasNote(o, 0, proto::note::kPlay, 127));  // held, not yet a tap
  s.nowUs = 5000;                                  // past the 4 ms debounce
  s.buttons = 0;
  o = step(b, s);
  CHECK(hasNote(o, 0, proto::note::kPlay, 127));
  s.nowUs = 5000 + 12000;
  o = step(b, s);
  CHECK(hasNote(o, 0, proto::note::kPlay, 0));
}

EPDJ_TEST(a_long_play_hold_is_not_a_pulse) {
  twin::Bank b;
  twin::Sample s;
  s.buttons = twin::kPlayA;
  step(b, s);
  s.nowUs = 700000;
  s.buttons = 0;
  const twin::Out o = step(b, s);
  CHECK(!hasNote(o, 0, proto::note::kPlay, 127));
}

EPDJ_TEST(loop_and_pad_notes) {
  twin::Bank b;
  twin::Sample s;
  s.buttons = static_cast<uint16_t>(twin::kInB | twin::kPadA0);
  const twin::Out o = step(b, s);
  CHECK(hasNote(o, 1, twin::kLoopIn, 127));
  CHECK(hasNote(o, 0, twin::kPad0, 127));
  CHECK_EQ(o.leds & twin::kLedInB, twin::kLedInB);
}

EPDJ_TEST(host_echo_latches_the_led) {
  twin::Bank b;
  b.onMidi(midi::noteOn(0, twin::kLoopOut, 127));
  twin::Sample s;
  const twin::Out o = step(b, s);
  CHECK_EQ(o.leds & twin::kLedOutA, twin::kLedOutA);
  b.onMidi(midi::noteOff(0, twin::kLoopOut, 0));
  const twin::Out o2 = step(b, s);
  CHECK_EQ(o2.leds & twin::kLedOutA, 0);
}

EPDJ_TEST(muted_buttons_send_nothing) {
  twin::Bank b;
  twin::Sample s;
  s.buttons = twin::kCueB;
  s.muteButtons = true;
  const twin::Out o = step(b, s);
  CHECK(!hasNote(o, 1, proto::note::kCue, 127));
  CHECK_EQ(b.down() & twin::kCueB, twin::kCueB);
}

EPDJ_TEST(pitch_b_and_crossfader_channels) {
  twin::Bank b;
  twin::Sample s;
  s.nowUs = 1000;
  s.pitchB = 4000;
  s.cross = 100;
  const twin::Out o = step(b, s);
  CHECK(hasCc(o, 1, proto::cc::kPitchMsb));
  CHECK(hasCc(o, 0, proto::cc::kCrossfaderMsb));
}

EPDJ_TEST(deck_b_jog_is_channel_2) {
  twin::Bank b;
  twin::Sample s;
  s.angleValid = true;
  s.angle12 = 0;
  // The touch ring learns for 250 ms and the jog emitter waits 2 ms. Spin after that.
  for (int ms = 0; ms < 300; ++ms) {
    s.nowUs = static_cast<uint64_t>(ms) * 1000u;
    step(b, s);
  }
  bool saw = false;
  for (int ms = 300; ms < 340; ++ms) {
    s.nowUs = static_cast<uint64_t>(ms) * 1000u;
    s.angle12 = static_cast<uint16_t>((ms - 300) * 32);  // 8 ticks per ms, well above one message
    const twin::Out o = step(b, s);
    if (hasCc(o, 1, proto::cc::kJog)) saw = true;
  }
  CHECK(saw);
}
