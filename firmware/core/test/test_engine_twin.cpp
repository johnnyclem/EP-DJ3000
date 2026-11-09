// Twin shell on one unit: right pitch and the crossfader, plus PLAY / LOOP and the right-hand buttons.
// With twinWired left false, none of that traffic exists, so the one-fader tests stay about one fader.
#include "epdj/protocol.h"
#include "test_engine_sim.h"

using namespace epdj;
namespace p = epdj::proto;

namespace {

int countByte(const std::vector<esim::Bytes>& v, uint8_t status, uint8_t data1) {
  int n = 0;
  for (const esim::Bytes& m : v) {
    if (m.size() == 3 && m[0] == status && m[1] == data1) ++n;
  }
  return n;
}

bool hasMsg(const std::vector<esim::Bytes>& v, uint8_t status, uint8_t data1, uint8_t data2) {
  for (const esim::Bytes& m : v) {
    if (m.size() == 3 && m[0] == status && m[1] == data1 && m[2] == data2) return true;
  }
  return false;
}

}  // namespace

EPDJ_TEST(twin_controls_say_nothing_until_the_platform_wires_them) {
  esim::Deck d(0);
  d.in.faderRaw = 2500;
  d.in.pitchRightRaw = 1000;
  d.in.crossfaderRaw = 3800;
  d.in.play = d.in.loopIn = d.in.cueRight = d.in.playRight = d.in.loopInRight = d.in.loopOutRight = true;
  d.run(30);
  const std::vector<esim::Bytes> m = esim::usbMsgs(d.log, 0);
  CHECK_EQ(countByte(m, 0xB0, p::cc::kCrossfaderMsb), 0);
  CHECK_EQ(countByte(m, 0xB1, p::cc::kPitchMsb), 0);
  CHECK(!hasMsg(m, 0x90, p::note::kPlay, 127));
  CHECK(!hasMsg(m, 0x90, p::note::kLoopIn, 127));
  CHECK(!hasMsg(m, 0x91, p::note::kCue, 127));
  CHECK(d.plat.deckSets.empty());
}

EPDJ_TEST(twin_pots_send_right_pitch_on_the_other_channel_and_the_crossfader_on_this_one) {
  esim::Deck d(0);
  d.in.twinWired = true;
  d.in.faderRaw = 3000;
  d.in.pitchRightRaw = 1000;
  d.in.crossfaderRaw = 3500;
  d.run(40);
  const std::vector<esim::Bytes> m = esim::usbMsgs(d.log, 0);
  CHECK(countByte(m, 0xB0, p::cc::kPitchMsb) >= 1);       // left pitch, this deck
  CHECK(countByte(m, 0xB1, p::cc::kPitchMsb) == 1);       // right pitch, other channel, once
  CHECK(countByte(m, 0xB0, p::cc::kCrossfaderMsb) == 1);  // dedicated pot
  CHECK_EQ(countByte(m, 0xB1, p::cc::kCrossfaderMsb), 0);
  const size_t at = d.log.size();
  d.in.pitchRightRaw = 2800;
  d.in.crossfaderRaw = 800;
  d.run(20);
  const std::vector<esim::Bytes> moved = esim::usbMsgs(d.log, 0, at);
  CHECK(countByte(moved, 0xB1, p::cc::kPitchMsb) >= 1);
  CHECK(countByte(moved, 0xB0, p::cc::kCrossfaderMsb) >= 1);
}

EPDJ_TEST(twin_on_deck_b_puts_the_right_pitch_on_channel_1) {
  esim::Deck d(1);
  d.in.twinWired = true;
  d.in.faderRaw = 3000;
  d.in.pitchRightRaw = 1200;
  d.in.crossfaderRaw = 2000;
  d.run(20);
  const std::vector<esim::Bytes> m = esim::usbMsgs(d.log, 0);
  CHECK(countByte(m, 0xB1, p::cc::kPitchMsb) >= 1);       // this deck is B, channel 2
  CHECK(countByte(m, 0xB0, p::cc::kPitchMsb) == 1);       // right pitch on channel 1
  CHECK(countByte(m, 0xB1, p::cc::kCrossfaderMsb) == 1);  // crossfader stays on this deck's channel
}

EPDJ_TEST(a_crossfader_role_on_the_left_slide_does_not_fight_the_dedicated_pot) {
  esim::Deck d(0);
  d.set(p::ConfigKey::FaderRole, static_cast<int32_t>(p::FaderRole::Crossfader));
  d.in.twinWired = true;
  d.in.faderRaw = 1000;
  d.in.crossfaderRaw = 2000;
  d.run(20);
  d.clearLog();
  d.in.faderRaw = 3500;
  d.run(20);
  CHECK_EQ(countByte(esim::usbMsgs(d.log, 0), 0xB0, p::cc::kCrossfaderMsb), 0);
  d.in.crossfaderRaw = 3900;
  d.run(20);
  CHECK(countByte(esim::usbMsgs(d.log, 0), 0xB0, p::cc::kCrossfaderMsb) >= 1);
}

EPDJ_TEST(twin_play_is_a_pulse_and_loop_in_is_held) {
  esim::Deck d(0);
  d.in.twinWired = true;
  d.run(5);
  d.clearLog();
  d.in.play = true;
  d.step();
  CHECK(hasMsg(esim::usbMsgs(d.log, 0), 0x90, p::note::kPlay, 127));
  CHECK(!hasMsg(esim::usbMsgs(d.log, 0), 0x80, p::note::kPlay, 0));
  d.in.play = false;
  d.run(10);
  CHECK(hasMsg(esim::usbMsgs(d.log, 0), 0x80, p::note::kPlay, 0));

  d.clearLog();
  d.in.loopIn = true;
  d.step();
  CHECK(hasMsg(esim::usbMsgs(d.log, 0), 0x90, p::note::kLoopIn, 127));
  d.run(3);
  d.in.loopIn = false;
  d.step();
  CHECK(hasMsg(esim::usbMsgs(d.log, 0), 0x80, p::note::kLoopIn, 0));
}

EPDJ_TEST(twin_right_buttons_use_the_other_channel_and_do_not_swap_decks) {
  esim::Deck d(0);
  d.in.twinWired = true;
  d.run(5);
  d.clearLog();
  d.in.cueRight = true;
  d.in.playRight = true;
  d.in.loopInRight = true;
  d.in.loopOutRight = true;
  d.step();
  const std::vector<esim::Bytes> on = esim::usbMsgs(d.log, 0);
  CHECK(hasMsg(on, 0x91, p::note::kCue, 127));
  CHECK(hasMsg(on, 0x91, p::note::kPlay, 127));
  CHECK(hasMsg(on, 0x91, p::note::kLoopIn, 127));
  CHECK(hasMsg(on, 0x91, p::note::kLoopOut, 127));
  CHECK(!hasMsg(on, 0x90, p::note::kCue, 127));
  d.run(10);
  CHECK(hasMsg(esim::usbMsgs(d.log, 0), 0x81, p::note::kPlay, 0));
  d.in.cueRight = d.in.loopInRight = d.in.loopOutRight = false;
  d.run(6000);
  CHECK(d.plat.deckSets.empty());
  CHECK(d.cfg.deck() == p::Deck::A);
}
