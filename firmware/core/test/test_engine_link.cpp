// Two complete engines on a lossy simulated radio: Deck A (USB host) and Deck B (power only). B's controls
// arrive on A's USB cable 2 on B's channel, host feedback relays back, transport and tempo are mirrored.
#include <cmath>
#include <cstdlib>
#include <memory>

#include "test_engine_sim.h"

using namespace esim;
using proto::sysex::Cmd;

namespace {

constexpr int kSettleMs = 3600;

// Deck A on the Mac, Deck B on a power bank with a clock that has nothing to do with A's.
struct Pair {
  Deck a, b;
  Rig rig;
  Pair(uint32_t seed, double loss, double dup, bool bHasHost = false)
      : a(0, 0x0A, 0, 3), b(1, 0x0B, 7300000, 4), rig(seed) {
    b.in.usbMounted = bHasHost;
    rig.decks = {&a, &b};
    rig.air.loss = loss;
    rig.air.dup = dup;
  }
};

bool onChannel(const std::vector<Bytes>& v, uint8_t ch) {
  for (const Bytes& m : v) {
    if (!m.empty() && m[0] >= 0x80 && m[0] < 0xF0 && (m[0] & 0x0F) != ch) return false;
  }
  return true;
}

std::vector<Bytes> notesNumbered(const std::vector<Bytes>& v, uint8_t note) {
  std::vector<Bytes> r;
  for (const Bytes& m : v) {
    if (m.size() == 3 && ((m[0] & 0xF0) == 0x90 || (m[0] & 0xF0) == 0x80) && m[1] == note) r.push_back(m);
  }
  return r;
}

int lastPairOf(const std::vector<Bytes>& v, uint8_t ch, uint8_t msb, uint8_t lsb) {
  const std::vector<int> p = pairValues(v, ch, msb, lsb);
  return p.empty() ? -1 : p.back();
}

uint64_t hashLogs(const std::vector<Rec>& log) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](uint64_t v) {
    h ^= v;
    h *= 1099511628211ull;
  };
  for (const Rec& r : log) {
    mix(r.t);
    mix(static_cast<uint64_t>(r.kind));
    mix(r.portMask);
    mix(r.cable);
    for (uint8_t x : r.bytes) mix(x);
  }
  return h;
}

// The scenario shared by the exactness and determinism tests: B scratches (AS5600 + finger), moves its
// fader and knob and clicks its buttons for 6 s while the radio loses and duplicates packets.
void bScript(Pair& p, int* clicks) {
  int angle = 0;
  *clicks = 0;
  for (int ms = 0; ms < 6000; ++ms) {
    const int speed = ((ms / 700) % 2 ? -1 : 1) * (8 + (ms / 300) % 9);  // 2..4 ticks per ms
    angle = (angle + speed + 4096) & 4095;
    p.b.in.angle12 = static_cast<uint16_t>(angle);
    p.b.ring.read(0.5, ms >= 500 && ms < 5500 ? 1.0 : 0.0, p.b.rng, p.b.in.touchRaw);
    const int tri = (ms % 2000) < 1000 ? ms % 2000 : 2000 - ms % 2000;  // 0..1000..0
    p.b.in.faderRaw = static_cast<uint16_t>(100 + tri * 3);
    p.b.in.knobRaw = static_cast<uint16_t>(4000 - tri * 3);
    p.b.in.cue = ms % 600 >= 100 && ms % 600 < 150;
    p.b.in.rev = ms % 600 >= 300 && ms % 600 < 350;
    if (ms % 600 == 100) ++*clicks;
    p.rig.step();
  }
  p.b.in.cue = p.b.in.rev = false;
  p.b.in.faderRaw = 2500;
  p.b.in.knobRaw = 3100;
}

}  // namespace

EPDJ_TEST(engine_link_discovery_and_status) {
  Pair p(1, 0.0, 0.0);
  p.rig.run(3000);
  CHECK(p.a.engine.link().linkUp() && p.b.engine.link().linkUp());
  CHECK(p.a.engine.led().link == LedStatus::LinkLed::Bridging);  // B has no host: A carries its MIDI
  CHECK(p.b.engine.led().link == LedStatus::LinkLed::Bridging);

  // LINK_STATUS from each unit.
  std::vector<Reply> r = ask(p.a, 0, Cmd::LinkStatus);
  CHECK_EQ(r.size(), 1);
  if (r.size() == 1) {
    CHECK_EQ(r[0].cmd, 0x4B);
    CHECK_EQ(r[0].payload.size(), 1 + proto::Peer::kBytes);
    proto::Peer peer;
    CHECK(proto::deserialize(&r[0].payload[1], proto::Peer::kBytes, &peer));
    CHECK_EQ(peer.deck, 1);
    CHECK_EQ(peer.flags, proto::peerFlags::kForwarding);  // B has no host, forwards, is not the master
    CHECK_EQ(peer.status, 0);
    CHECK_EQ(peer.rssi, -40);
    CHECK(peer.rttUs > 1400 && peer.rttUs < 4000);
  }
  r = ask(p.b, 1, Cmd::LinkStatus);
  CHECK_EQ(r.size(), 1);
  if (r.size() == 1) {
    proto::Peer peer;
    CHECK(proto::deserialize(&r[0].payload[1], proto::Peer::kBytes, &peer));
    CHECK_EQ(peer.deck, 0);
    CHECK_EQ(peer.flags, proto::peerFlags::kUsbHost | proto::peerFlags::kIsMaster);
  }

  // Telemetry flags.
  std::vector<Reply> s = ask(p.a, 0, Cmd::Stream, b({10}));
  p.rig.run(200);
  bool seen = false;
  for (const Rec& rec : p.a.log) {
    syx::Frame f;
    if (rec.kind == OutEvent::Kind::SysEx &&
        syx::decodeFrame(rec.bytes.data(), rec.bytes.size(), syx::kNoDeckFilter, &f) == syx::DecodeStatus::Ok && f.cmd == 0x47) {
      proto::Telemetry t;
      CHECK(proto::deserialize(f.payload, f.payloadLen, &t));
      CHECK((t.flags & proto::stateFlags::kLinkUp) != 0);
      CHECK((t.flags & proto::stateFlags::kBridging) != 0);
      seen = true;
    }
  }
  CHECK(seen);
}

EPDJ_TEST(engine_link_deck_b_controls_arrive_on_deck_a_cable_2_exactly) {
  Pair p(2, 0.20, 0.10);  // 20% of packets lost, 10% duplicated, jitter reorders them
  p.rig.run(kSettleMs + 500);  // ring baseline learned, link up, first state exchanged
  CHECK(p.a.engine.link().routeIn() && p.b.engine.link().routeOut());
  const int32_t jogBase = p.b.engine.jogTotal();
  const size_t aFrom = p.a.log.size();
  int clicks = 0;
  bScript(p, &clicks);
  p.rig.run(3000);  // the last heartbeat repairs whatever the last packets lost
  CHECK(p.rig.air.lost > 100);

  const std::vector<Bytes> c2 = usbMsgs(p.a.log, 2, aFrom);
  CHECK(c2.size() > 500);
  CHECK(onChannel(c2, 1));  // channel 2 = zero-based 1, exactly as B produced it
  CHECK_EQ(ccSum(c2, 1, proto::cc::kJog), p.b.engine.jogTotal() - jogBase);  // every tick, once
  int64_t travelled = 0;  // the platter went both ways; count motion, not just the net
  for (const Bytes& m : c2) travelled += (m.size() == 3 && m[0] == 0xB1 && m[1] == proto::cc::kJog) ? std::abs(m[2] - 64) : 0;
  CHECK(travelled > 5000);

  // Fader and knob: last value wins, and the last value is B's final position.
  AnalogControl fader, knob;
  AnalogControl::Config kc;
  kc.rawMin = 0;
  kc.rawMax = 4095;
  kc.deadLoPermille = kc.deadHiPermille = 0;
  knob.configure(kc);
  CHECK_EQ(lastPairOf(c2, 1, proto::cc::kPitchMsb, proto::cc::kPitchLsb), fader.update(2500, 0).value);  // the default role
  CHECK_EQ(lastPairOf(c2, 1, proto::cc::kCrossfaderMsb, proto::cc::kCrossfaderLsb), -1);
  CHECK_EQ(lastPairOf(c2, 1, proto::cc::kChannelVolumeMsb, proto::cc::kChannelVolumeLsb), -1);
  CHECK_EQ(lastPairOf(c2, 1, proto::cc::kKnobMsb, proto::cc::kKnobLsb), knob.update(3100, 0).value);

  // Buttons: every press and release, once, in order. Touch: one down, one up.
  std::vector<Bytes> cue = notesNumbered(c2, proto::note::kCue), rev = notesNumbered(c2, proto::note::kRev);
  CHECK_EQ(cue.size(), static_cast<size_t>(2 * clicks));
  CHECK_EQ(rev.size(), static_cast<size_t>(2 * clicks));
  for (size_t i = 0; i < cue.size() && i < rev.size(); ++i) {
    CHECK_BYTES(cue[i], i % 2 == 0 ? b({0x91, 36, 127}) : b({0x81, 36, 0}));
    CHECK_BYTES(rev[i], i % 2 == 0 ? b({0x91, 37, 127}) : b({0x81, 37, 0}));
  }
  const std::vector<Bytes> touch = notesNumbered(c2, proto::note::kTouch);
  CHECK_EQ(touch.size(), 2);
  if (touch.size() == 2) {
    CHECK_BYTES(touch[0], b({0x91, 38, 127}));
    CHECK_BYTES(touch[1], b({0x81, 38, 0}));
  }

  // B has no host: nothing of its own went out on USB, and A's own cable 0 is only A's business.
  for (const Rec& r : p.b.log) CHECK((r.portMask & ports::kUsb) == 0);
  CHECK(p.a.engine.link().stats().rxStaleState > 0 || p.rig.air.sent > 0);
}

EPDJ_TEST(engine_link_bridge_latency_on_a_clean_air) {
  Pair p(3, 0.0, 0.0);
  p.b.set(ConfigKey::FaderRole, 0);  // CROSSFADER, whatever the default is
  p.b.in.faderRaw = 1000;
  p.rig.run(kSettleMs);
  const uint64_t moveT = p.b.t;
  p.b.in.faderRaw = 3500;
  p.rig.run(30);
  uint64_t first = 0;
  for (const Rec& r : p.a.log) {
    if (r.t >= moveT && r.cable == 2 && r.kind == OutEvent::Kind::Message && r.bytes.size() == 3 && r.bytes[1] == proto::cc::kCrossfaderMsb) {
      first = r.t;
      break;
    }
  }
  CHECK(first != 0);
  CHECK(first - moveT <= 5000);  // PROTOCOL.md section 4: <= 5 ms typical
}

EPDJ_TEST(engine_link_host_feedback_relays_back_to_deck_b) {
  Pair p(4, 0.15, 0.05);
  p.rig.run(kSettleMs);

  // The Mac lights B's cue and play LEDs by writing to A's cable 2 (channel 2).
  p.a.midiIn(Engine::MidiPort::Usb2, b({0x91, 36, 127}));
  p.rig.run(150);
  CHECK(p.b.engine.led().cueLit);
  p.a.midiIn(Engine::MidiPort::Usb2, b({0x91, 39, 127}));
  p.rig.run(150);
  CHECK(p.b.engine.led().transport == proto::TransportState::Playing);
  p.a.midiIn(Engine::MidiPort::Usb2, b({0xB1, 29, 2}));
  p.rig.run(150);
  CHECK(p.b.engine.led().transport == proto::TransportState::Paused);
  p.a.midiIn(Engine::MidiPort::Usb2, b({0x81, 36, 0}));
  p.rig.run(150);
  CHECK(!p.b.engine.led().cueLit);

  // Tempo from the host, stored for display on B.
  p.a.midiIn(Engine::MidiPort::Usb2, b({0xB1, 20, 10, 0xB1, 52, 5}));
  p.rig.run(300);
  CHECK_EQ(p.b.engine.tempoX10(), 1285);

  // Messages for another channel, and things that are not Note/CC, do not reach B (or confuse it).
  p.a.midiIn(Engine::MidiPort::Usb2, b({0x90, 36, 127}));  // channel 1 = deck A's, not B's
  p.a.midiIn(Engine::MidiPort::Usb2, b({0xC1, 5}));
  p.rig.run(200);
  CHECK(!p.b.engine.led().cueLit);

  // A's own host talks to A on cable 0 as usual and does not leak to B.
  p.a.hostMsg(b({0x90, 36, 127}));
  p.rig.run(150);
  CHECK(p.a.engine.led().cueLit);
  CHECK(!p.b.engine.led().cueLit);
}

EPDJ_TEST(engine_link_master_transport_and_tempo_reach_the_follower) {
  Pair p(5, 0.20, 0.05);
  p.a.set(ConfigKey::Mode, 1);
  p.a.set(ConfigKey::ClockOutMask, 7);  // USB + TRS + LINK
  p.b.set(ConfigKey::Mode, 2);
  p.rig.run(kSettleMs);
  CHECK(p.b.engine.transport() == proto::TransportState::Stopped);
  CHECK_EQ(p.b.engine.displayTempoX10(), 1200);  // the master told it its tempo although nothing plays

  p.a.hostMsg(b({0xB0, 20, 10, 0xB0, 52, 5}));  // 128.5 BPM from the host
  p.rig.run(600);
  CHECK_EQ(p.b.engine.displayTempoX10(), 1285);

  p.a.hostMsg(b({0xB0, 29, 1}));
  p.rig.run(250);  // reliable: a few retries at most
  CHECK(p.b.engine.transport() == proto::TransportState::Playing);
  CHECK(p.b.engine.led().transport == proto::TransportState::Playing);

  // Beat LEDs: the follower flashes on the master's beats (within a tick or two).
  std::vector<uint64_t> edgesA, edgesB;
  bool fa = false, fb = false;
  for (int ms = 0; ms < 10000; ++ms) {
    p.rig.step();
    const bool a = p.a.engine.led().beatFlash, bb = p.b.engine.led().beatFlash;
    if (a && !fa) edgesA.push_back(p.a.t);
    if (bb && !fb) edgesB.push_back(p.b.t);
    fa = a;
    fb = bb;
  }
  CHECK(edgesA.size() >= 18 && edgesB.size() >= 18);  // 10 s at 128.5 BPM = 21 beats
  size_t matched = 0;
  for (uint64_t ta : edgesA) {
    for (uint64_t tb : edgesB) {
      if (std::llabs(static_cast<long long>(ta) - static_cast<long long>(tb)) <= 45000) {
        ++matched;
        break;
      }
    }
  }
  CHECK(matched + 2 >= edgesA.size());

  p.a.hostMsg(b({0xB0, 29, 2}));
  p.rig.run(250);
  CHECK(p.b.engine.transport() == proto::TransportState::Paused);
  p.a.hostMsg(b({0xB0, 29, 0}));
  p.rig.run(250);
  CHECK(p.b.engine.transport() == proto::TransportState::Stopped);

  // The follower emits nothing of its own on its (absent) USB.
  for (const Rec& r : p.b.log) CHECK((r.portMask & ports::kUsb) == 0);

  // Without the LINK bit in clockOutMask the radio carries no transport.
  p.a.set(ConfigKey::ClockOutMask, 3);
  p.a.hostMsg(b({0xB0, 29, 1}));
  p.rig.run(500);
  CHECK(p.b.engine.transport() == proto::TransportState::Stopped);
}

EPDJ_TEST(engine_link_losing_the_peer_releases_held_notes) {
  Pair p(6, 0.05, 0.0);
  p.rig.run(kSettleMs);
  for (int i = 0; i < 300; ++i) {  // B's finger rests on the platter: touch note held
    p.b.ring.read(0.3, 1.0, p.b.rng, p.b.in.touchRaw);
    p.rig.step();
  }
  std::vector<Bytes> c2 = usbMsgs(p.a.log, 2);
  CHECK_EQ(countEq(c2, b({0x91, 38, 127})), 1);
  CHECK_EQ(countEq(c2, b({0x81, 38, 0})), 0);

  const size_t mark = p.a.log.size();
  p.rig.air.partitioned = true;  // B walks out of range
  for (int i = 0; i < 2500; ++i) {
    p.b.ring.read(0.3, 1.0, p.b.rng, p.b.in.touchRaw);
    p.rig.step();
  }
  c2 = usbMsgs(p.a.log, 2, mark);
  CHECK_EQ(countEq(c2, b({0x81, 38, 0})), 1);  // released by A when B timed out: no stuck note at the host
  CHECK(!p.a.engine.link().linkUp());
  CHECK(p.a.engine.led().link == LedStatus::LinkLed::Searching);
}

EPDJ_TEST(engine_link_route_follows_who_has_a_usb_host) {
  Pair p(7, 0.0, 0.0);
  p.rig.run(kSettleMs);
  CHECK(p.a.engine.link().routeIn());

  // B gets plugged into a computer too: it now talks on its own cable 0 and A stops bridging.
  p.b.in.usbMounted = true;
  p.rig.run(1000);
  CHECK(!p.a.engine.link().routeIn() && !p.b.engine.link().routeOut());
  CHECK(p.a.engine.led().link == LedStatus::LinkLed::Up);
  const size_t markA = p.a.log.size(), markB = p.b.log.size();
  p.b.in.cue = true;
  p.rig.run(30);
  p.b.in.cue = false;
  p.rig.run(30);
  CHECK_EQ(countEq(usbMsgs(p.b.log, 0, markB), b({0x91, 36, 127})), 1);
  CHECK_EQ(usbMsgs(p.a.log, 2, markA).size(), 0);

  // linkBridge = OFF on A: even a power-only B is not bridged.
  p.b.in.usbMounted = false;
  p.a.set(ConfigKey::LinkBridge, 1);
  p.rig.run(1200);
  CHECK(!p.a.engine.link().routeIn());
  // FORCE on A: bridged although B has a host.
  p.a.set(ConfigKey::LinkBridge, 2);
  p.b.in.usbMounted = true;
  p.rig.run(1200);
  CHECK(p.a.engine.link().routeIn());
}

EPDJ_TEST(engine_link_disable_and_deck_conflict) {
  Pair p(8, 0.0, 0.0);
  p.rig.run(3000);
  CHECK(p.a.engine.link().linkUp());
  p.b.set(ConfigKey::LinkEnable, 0);
  p.rig.run(10);
  const size_t markB = p.b.log.size();
  p.rig.run(3000);
  CHECK(p.b.engine.led().link == LedStatus::LinkLed::Off);
  CHECK(p.a.engine.led().link == LedStatus::LinkLed::Searching);  // B stopped talking; A timed it out
  for (size_t i = markB; i < p.b.log.size(); ++i) CHECK(p.b.log[i].kind != OutEvent::Kind::Link);
  std::vector<Reply> r = ask(p.b, 127, Cmd::LinkStatus);
  CHECK(r.size() == 1 && r[0].payload.size() == 1 && r[0].payload[0] == 0);

  // Two units that both claim Deck A: surfaced, not fought over.
  Pair q(9, 0.0, 0.0);
  q.b.cfg.set(ConfigKey::DeckId, 0);  // Deck::cfg was built as B; the engine reads it on its first step
  q.rig.run(3000);
  CHECK(q.a.engine.led().link == LedStatus::LinkLed::Conflict);
  CHECK(q.b.engine.led().link == LedStatus::LinkLed::Conflict);
  r = ask(q.a, 0, Cmd::LinkStatus);
  CHECK_EQ(r.size(), 1);
  if (r.size() == 1 && r[0].payload.size() == 1 + proto::Peer::kBytes) {
    proto::Peer peer;
    CHECK(proto::deserialize(&r[0].payload[1], proto::Peer::kBytes, &peer));
    CHECK_EQ(peer.deck, 0);
    CHECK_EQ(peer.status & link::Link::kPeerConflict, link::Link::kPeerConflict);
  }
}

EPDJ_TEST(engine_link_group_is_a_live_config_value) {
  Pair p(10, 0.0, 0.0);
  p.rig.run(kSettleMs);
  CHECK(p.a.engine.link().routeIn() && p.b.engine.link().routeOut());
  for (int i = 0; i < 300; ++i) {  // a finger rests on B: its touch note is held at A's host
    p.b.ring.read(0.3, 1.0, p.b.rng, p.b.in.touchRaw);
    p.rig.step();
  }
  CHECK_EQ(countEq(usbMsgs(p.a.log, 2), b({0x91, 38, 127})), 1);
  const size_t mark = p.a.log.size();

  // B joins rig 5 over SysEx: it forgets A at once, no reboot.
  std::vector<Reply> r = ask(p.b, 1, Cmd::SetConfig, setConfigPayload(ConfigKey::LinkGroup, 5));
  CHECK(r.size() == 1 && r[0].cmd == proto::sysex::kAck);
  CHECK_EQ(p.b.cfg.get(ConfigKey::LinkGroup), 5);
  CHECK(!p.b.engine.link().linkUp());
  CHECK(p.b.engine.led().link == LedStatus::LinkLed::Searching);
  p.rig.run(2500);
  CHECK(!p.a.engine.link().linkUp());                                     // A timed B out ...
  CHECK_EQ(countEq(usbMsgs(p.a.log, 2, mark), b({0x81, 38, 0})), 1);     // ... and released B's held note
  CHECK(p.a.engine.link().stats().rxForeign > 0 && p.b.engine.link().stats().rxForeign > 0);
  CHECK(!p.b.engine.link().routeOut() && !p.a.engine.link().routeIn());

  // A follows, the two find each other again and the bridge works.
  r = ask(p.a, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::LinkGroup, 5));
  CHECK(r.size() == 1 && r[0].cmd == proto::sysex::kAck);
  p.rig.run(2500);
  CHECK(p.a.engine.link().linkUp() && p.b.engine.link().linkUp());
  CHECK(p.a.engine.link().routeIn() && p.b.engine.link().routeOut());
  CHECK(p.a.engine.led().link == LedStatus::LinkLed::Bridging);
  const size_t mark2 = p.a.log.size();
  p.b.in.faderRaw = 3500;
  p.rig.run(100);
  CHECK(lastPairOf(usbMsgs(p.a.log, 2, mark2), 1, proto::cc::kPitchMsb, proto::cc::kPitchLsb) > 0);
}

// The radio group of a unit as its HELLO announces it.
static uint8_t helloGroup(Engine& e, RawInputs* in, uint64_t* t) {
  in->nowUs = *t;
  e.step(*in);
  *t += 1000;
  int group = -1;
  OutEvent ev;
  while (e.popOut(&ev)) {
    link::Header h;
    const uint8_t* payload = nullptr;
    if (ev.kind == OutEvent::Kind::Link && link::decodePacket(ev.data, ev.len, &h, &payload) == link::DecodeError::None) group = h.group;
  }
  return static_cast<uint8_t>(group);
}

EPDJ_TEST(engine_link_group_config_wins_over_the_build_time_default) {
  Config cfg;
  FakePlatform plat;
  Engine::Identity id = Deck::ident(0x0A, 100);
  id.linkGroup = 3;  // the platform's Kconfig value
  Engine e(cfg, plat, id);
  RawInputs in;
  in.usbMounted = true;
  uint64_t t = 1000000;
  CHECK_EQ(helloGroup(e, &in, &t), 3);  // linkGroup 0 in the config: the build-time default applies

  cfg.set(ConfigKey::LinkGroup, 9);     // live: the next packet is already in the new group
  CHECK_EQ(helloGroup(e, &in, &t), 9);
  cfg.set(ConfigKey::LinkGroup, 0);     // back to "not set": the default again
  CHECK_EQ(helloGroup(e, &in, &t), 3);

  Config cfg2;
  cfg2.set(ConfigKey::LinkGroup, 200);  // a value stored in flash is used from the first packet
  FakePlatform plat2;
  Engine e2(cfg2, plat2, id);
  uint64_t t2 = 1000000;
  RawInputs in2;
  CHECK_EQ(helloGroup(e2, &in2, &t2), 200);

  Config cfg3;
  FakePlatform plat3;
  Engine e3(cfg3, plat3, Deck::ident(0x0C, 102));  // no default at all
  uint64_t t3 = 1000000;
  RawInputs in3;
  CHECK_EQ(helloGroup(e3, &in3, &t3), 0);
}

EPDJ_TEST(engine_pair_session_is_deterministic) {
  uint64_t hashes[2][2] = {};
  int jog[2] = {};
  for (int run = 0; run < 2; ++run) {
    Pair p(42, 0.2, 0.1);
      p.rig.run(kSettleMs + 500);
    int clicks = 0;
    bScript(p, &clicks);
    p.rig.run(1000);
    hashes[run][0] = hashLogs(p.a.log);
    hashes[run][1] = hashLogs(p.b.log);
    jog[run] = p.b.engine.jogTotal();
  }
  CHECK(hashes[0][0] == hashes[1][0]);
  CHECK(hashes[0][1] == hashes[1][1]);
  CHECK(hashes[0][0] != 0);
  CHECK_EQ(jog[0], jog[1]);
}
