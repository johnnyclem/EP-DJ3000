// Two (or three) Link instances over a simulated ESP-NOW air: discovery, election, the bridge, feedback
// relay, reliable transport, time sync, reboots, partitions, conflicts and hostile packets.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "epdj/link.h"
#include "test_harness.h"

using namespace epdj;
using namespace epdj::link;
using proto::LinkBridge;

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
  double uni() { return (static_cast<double>(next()) + 0.5) / 4294967296.0; }
  uint32_t below(uint32_t n) { return next() % n; }
  int range(int lo, int hi) { return lo + static_cast<int>(below(static_cast<uint32_t>(hi - lo + 1))); }
};

constexpr double kTwo32 = 4294967296.0;

struct Sim;

struct Node final : LinkTransport, LinkSink {
  Sim* sim;
  size_t idx;
  double origin, rate;  // this unit's microsecond counter: origin + t * rate
  std::unique_ptr<Link> link;
  bool down = false;  // out of radio range / powered off
  std::vector<midi::Message> bridged, feedback;
  std::vector<TransportEvent> transports;

  Node(Sim* s, size_t i, double o, double ppm) : sim(s), idx(i), origin(o), rate(1.0 + ppm * 1e-6) {
    link.reset(new Link(*this, *this));
  }
  uint64_t clock(int64_t trueUs) const {
    return static_cast<uint64_t>(origin + static_cast<double>(trueUs) * rate);
  }
  void reboot() { link.reset(new Link(*this, *this)); }

  bool send(const uint8_t* data, size_t len) override;
  void onBridgedMidi(const midi::Message& m) override { bridged.push_back(m); }
  void onFeedbackMidi(const midi::Message& m) override { feedback.push_back(m); }
  void onTransport(const TransportEvent& e) override { transports.push_back(e); }

  int64_t jogTicksReceived() const {
    int64_t t = 0;
    for (const auto& m : bridged) {
      if (m.isControlChange() && m.data1() == proto::cc::kJog) t += midi::decodeRelative(m.data2());
    }
    return t;
  }
};

struct Sim {
  Rng rng;
  std::vector<std::unique_ptr<Node>> nodes;
  double loss = 0.0, dup = 0.0;
  double baseUs = 800.0, jitterUs = 150.0;
  int64_t nowUs = 0;  // true time
  struct Delivery {
    size_t to;
    std::vector<uint8_t> bytes;
  };
  std::multimap<int64_t, Delivery> air;
  size_t sentPackets = 0;

  explicit Sim(uint32_t seed) : rng(seed) {}

  Node& add(uint8_t macLast, uint8_t deck, bool usbHost, double originUs = 5e6, double ppm = 0.0, uint8_t group = 0) {
    nodes.emplace_back(new Node(this, nodes.size(), originUs, ppm));
    Node& n = *nodes.back();
    configure(n, macLast, deck, usbHost, group, static_cast<uint16_t>(100 + macLast));
    return n;
  }
  static void configure(Node& n, uint8_t macLast, uint8_t deck, bool usbHost, uint8_t group, uint16_t epoch) {
    Link::Config c;
    c.mac.b[0] = 2;
    c.mac.b[5] = macLast;
    c.deck = deck;
    c.group = group;
    c.epoch = epoch;
    n.link->configure(c);
    n.link->setUsbHost(usbHost);
  }

  void broadcast(size_t from, const uint8_t* data, size_t len) {
    ++sentPackets;
    if (nodes[from]->down) return;
    for (size_t to = 0; to < nodes.size(); ++to) {
      if (to == from || nodes[to]->down) continue;
      const int copies = rng.uni() < dup ? 2 : 1;
      for (int c = 0; c < copies; ++c) {
        if (rng.uni() < loss) continue;
        const double delay = baseUs - jitterUs * std::log(rng.uni());
        air.emplace(nowUs + static_cast<int64_t>(delay), Delivery{to, std::vector<uint8_t>(data, data + len)});
      }
    }
  }

  void step() {  // 1 ms
    nowUs += 1000;
    while (!air.empty() && air.begin()->first <= nowUs) {
      const int64_t when = air.begin()->first;
      Delivery d = std::move(air.begin()->second);
      air.erase(air.begin());
      Node& n = *nodes[d.to];
      if (n.down) continue;
      n.link->onPacket(d.bytes.data(), d.bytes.size(), -40, n.clock(when), n.clock(nowUs));
    }
    for (auto& n : nodes) {
      if (!n->down) n->link->poll(n->clock(nowUs));
    }
  }
  void run(int ms) {
    for (int i = 0; i < ms; ++i) step();
  }
};

bool Node::send(const uint8_t* data, size_t len) {
  sim->broadcast(idx, data, len);
  return true;
}

bool has(const std::vector<midi::Message>& v, const midi::Message& m) {
  for (const auto& x : v) {
    if (x == m) return true;
  }
  return false;
}
size_t count(const std::vector<midi::Message>& v, const midi::Message& m) {
  size_t n = 0;
  for (const auto& x : v) n += x == m ? 1u : 0u;
  return n;
}

}  // namespace

EPDJ_TEST(node_discovery_election_and_status) {
  Sim sim(1);
  Node& deckB = sim.add(0x0B, 1, /*usbHost=*/false);
  Node& deckA = sim.add(0x0A, 0, /*usbHost=*/true);
  CHECK(!deckA.link->linkUp());
  sim.run(3000);
  CHECK(deckA.link->linkUp() && deckB.link->linkUp());
  CHECK_EQ(deckA.link->peers().count(), 1);
  CHECK(deckA.link->selfIsMaster());   // deck A: lower deck id
  CHECK(!deckB.link->selfIsMaster());
  CHECK(!deckA.link->deckConflict());

  proto::Peer p[3];
  CHECK_EQ(deckA.link->fillPeers(p, 3, sim.nodes[1]->clock(sim.nowUs)), 1);
  CHECK_EQ(p[0].deck, 1);
  CHECK_EQ(p[0].flags, proto::peerFlags::kForwarding);  // B has no host, is forwarding, is not master
  CHECK_EQ(p[0].status, 0);
  CHECK_EQ(p[0].rssi, -40);
  CHECK(p[0].rttUs > 1400 && p[0].rttUs < 3000);

  CHECK_EQ(deckB.link->fillPeers(p, 3, deckB.clock(sim.nowUs)), 1);
  CHECK_EQ(p[0].deck, 0);
  CHECK_EQ(p[0].flags, proto::peerFlags::kUsbHost | proto::peerFlags::kIsMaster);
  // peer clock minus ours: A's counter is the same as B's here, so ~0
  CHECK(std::abs(p[0].offsetUs) < 300);
  CHECK_EQ(deckB.link->fillPeers(p, 0, 0), 0);  // capacity respected

  CHECK(deckB.link->routeOut() && deckA.link->routeIn());
  CHECK(!deckB.link->routeIn() && !deckA.link->routeOut());
  CHECK(deckB.link->bridging() && deckA.link->bridging());
  CHECK_EQ(deckB.link->headerFlags(), proto::peerFlags::kForwarding);
  CHECK_EQ(deckA.link->headerFlags(), proto::peerFlags::kUsbHost | proto::peerFlags::kIsMaster);
}

EPDJ_TEST(node_bridge_forwards_deck_midi_and_relays_feedback_back) {
  Sim sim(2);
  Node& deckB = sim.add(0x0B, 1, false);
  Node& deckA = sim.add(0x0A, 0, true);
  sim.run(2000);
  // Deck B's cable-0 traffic appears on deck A's cable 2 handler, channel preserved (deck B = channel 2).
  CHECK(deckB.link->forwardMidi(midi::noteOn(1, proto::note::kTouch, 127)));
  CHECK(deckB.link->forwardMidi(midi::controlChange(1, proto::cc::kJog, 64 + 9)));
  CHECK(deckB.link->forwardMidi(midi::controlChange(1, proto::cc::kCrossfaderMsb, 100)));
  CHECK(deckB.link->forwardMidi(midi::controlChange(1, proto::cc::kCrossfaderLsb, 5)));
  CHECK(!deckB.link->forwardMidi(midi::clock()));
  sim.run(60);
  CHECK_EQ(count(deckA.bridged, midi::noteOn(1, proto::note::kTouch, 127)), 1);
  CHECK(has(deckA.bridged, midi::controlChange(1, proto::cc::kJog, 64 + 9)));
  CHECK(has(deckA.bridged, midi::controlChange(1, proto::cc::kCrossfaderMsb, 100)));
  CHECK(has(deckA.bridged, midi::controlChange(1, proto::cc::kCrossfaderLsb, 5)));
  CHECK_EQ(deckA.bridged.size(), 4);

  // Host feedback arriving on A's cable 2 is relayed to B and handled as B's own cable 0.
  CHECK(deckA.link->relayHostMidi(midi::noteOn(1, proto::note::kCue, 127)));
  CHECK(deckA.link->relayHostMidi(midi::controlChange(1, proto::cc::kBpmMsb, 60)));
  sim.run(60);
  CHECK_EQ(count(deckB.feedback, midi::noteOn(1, proto::note::kCue, 127)), 1);
  CHECK(has(deckB.feedback, midi::controlChange(1, proto::cc::kBpmMsb, 60)));
  CHECK_EQ(deckB.feedback.size(), 2);

  // The routes are one-way: A has a host, so A's own MIDI goes out its own USB, not over the air.
  CHECK(!deckA.link->forwardMidi(midi::noteOn(0, 36, 127)));
  CHECK(!deckB.link->relayHostMidi(midi::noteOn(0, 36, 127)));
  sim.run(40);
  CHECK_EQ(deckB.bridged.size(), 0);
}

EPDJ_TEST(node_bridge_modes_follow_host_presence_and_linkbridge) {
  Sim sim(3);
  Node& b = sim.add(0x0B, 1, false);
  Node& a = sim.add(0x0A, 0, true);
  sim.run(1500);
  CHECK(b.link->routeOut());
  // Both decks plug into the Mac: nobody needs a bridge any more.
  b.link->setUsbHost(true);
  CHECK(!b.link->routeOut());
  sim.run(700);  // the new flag reaches A with B's next packets
  CHECK(!a.link->routeIn());
  CHECK(!b.link->forwardMidi(midi::noteOn(1, 36, 127)));
  // Unplugged again.
  b.link->setUsbHost(false);
  sim.run(700);
  CHECK(b.link->routeOut() && a.link->routeIn());
  // linkBridge OFF on the receiving unit switches it off for both.
  a.link->setBridgeMode(LinkBridge::Off);
  sim.run(1200);  // announced in A's HELLO
  CHECK(!a.link->routeIn());
  CHECK(!b.link->routeOut());
  // FORCE on the receiver bridges even though the sender has a host.
  a.link->setBridgeMode(LinkBridge::Force);
  b.link->setUsbHost(true);
  sim.run(1200);
  CHECK(a.link->routeIn() && b.link->routeOut());
  // Back to AUTO with both hosts: off.
  a.link->setBridgeMode(LinkBridge::Auto);
  sim.run(1200);
  CHECK(!a.link->routeIn() && !b.link->routeOut());
}

EPDJ_TEST(node_jog_notes_and_faders_survive_30pct_loss_reorder_duplication) {
  Sim sim(4);
  sim.loss = 0.30;
  sim.dup = 0.10;
  sim.jitterUs = 2500;  // enough spread to reorder packets sent 2 ms apart
  Node& b = sim.add(0x0B, 1, false);
  Node& a = sim.add(0x0A, 0, true);
  sim.run(4000);  // link up and state adopted before the motion starts
  CHECK(a.link->routeIn() && b.link->routeOut());
  a.bridged.clear();

  int64_t sentJog = 0;
  std::vector<midi::Message> sentEvents;
  uint8_t lastFader = 0;
  for (int ms = 0; ms < 20000; ++ms) {
    if (sim.rng.below(2)) {
      const int d = sim.rng.range(-30, 63);
      if (d != 0 && b.link->forwardMidi(midi::controlChange(1, proto::cc::kJog, static_cast<uint8_t>(64 + d)))) {
        sentJog += d;
      }
    }
    if (sim.rng.below(25) == 0) {
      lastFader = static_cast<uint8_t>(sim.rng.below(128));
      b.link->forwardMidi(midi::controlChange(1, proto::cc::kCrossfaderMsb, lastFader));
    }
    if (sim.rng.below(60) == 0) {
      const midi::Message m = midi::noteOn(1, static_cast<uint8_t>(36 + sim.rng.below(5)), sim.rng.below(2) ? 127 : 0);
      CHECK(b.link->forwardMidi(m));
      sentEvents.push_back(m);
    }
    sim.step();
  }
  sim.run(3000);  // settle: heartbeats repair the tail, the jog emitter drains
  std::printf("    (jog %lld ticks, %zu note events, %zu packets on the air)\n", static_cast<long long>(sentJog),
              sentEvents.size(), sim.sentPackets);
  CHECK_EQ(a.jogTicksReceived(), sentJog);
  std::vector<midi::Message> gotEvents;
  uint8_t gotFader = 0xFF;
  for (const auto& m : a.bridged) {
    if (m.type() == midi::kNoteOn) gotEvents.push_back(m);
    if (m.isControlChange() && m.data1() == proto::cc::kCrossfaderMsb) gotFader = m.data2();
  }
  CHECK(gotEvents == sentEvents);       // each exactly once, in order
  CHECK_EQ(gotFader, lastFader);        // last value wins
  CHECK(b.link->forwarder().droppedEvents() == 0);
  CHECK(a.link->stats().rxStaleState > 0);  // the air really reordered/duplicated
  // the jog emitter never sends the "no change" value 64 nor exceeds the relative range
  for (const auto& m : a.bridged) {
    if (m.isControlChange() && m.data1() == proto::cc::kJog) {
      CHECK(m.data2() != 64);
      CHECK(m.data2() >= 1 && m.data2() <= 127);
    }
  }
}

EPDJ_TEST(node_peer_loss_releases_held_notes_and_rejoin_does_not_jump) {
  Sim sim(5);
  Node& b = sim.add(0x0B, 1, false);
  Node& a = sim.add(0x0A, 0, true);
  sim.run(2000);
  CHECK(b.link->forwardMidi(midi::noteOn(1, proto::note::kCue, 127)));
  CHECK(b.link->forwardMidi(midi::controlChange(1, proto::cc::kJog, 64 + 40)));
  sim.run(100);
  CHECK(has(a.bridged, midi::noteOn(1, proto::note::kCue, 127)));
  CHECK_EQ(a.jogTicksReceived(), 40);

  b.down = true;  // cable pulled / out of range while CUE is held
  sim.run(1300);
  CHECK(a.link->linkUp());  // not yet: the timeout is 1.5 s
  CHECK(!has(a.bridged, midi::noteOff(1, proto::note::kCue, 0)));
  sim.run(400);
  CHECK(!a.link->linkUp());
  CHECK(!a.link->routeIn());
  CHECK_EQ(count(a.bridged, midi::noteOff(1, proto::note::kCue, 0)), 1);  // no stuck note at the host

  // B moves while out of range, then comes back: that motion is not replayed as a burst.
  b.link->setUsbHost(false);
  b.link->forwardMidi(midi::controlChange(1, proto::cc::kJog, 64 + 55));  // routeOut is still latched on B
  b.down = false;
  sim.run(3000);
  CHECK(a.link->linkUp() && a.link->routeIn());
  CHECK_EQ(a.jogTicksReceived(), 40);  // the 55 ticks made while apart are not replayed as a burst
  const int64_t before = a.jogTicksReceived();
  b.link->forwardMidi(midi::controlChange(1, proto::cc::kJog, 64 + 12));
  sim.run(200);
  CHECK_EQ(a.jogTicksReceived(), before + 12);
}

EPDJ_TEST(node_reboot_with_new_epoch_resyncs_counters) {
  Sim sim(6);
  Node& b = sim.add(0x0B, 1, false);
  Node& a = sim.add(0x0A, 0, true);
  sim.run(2000);
  for (int i = 0; i < 10; ++i) {
    b.link->forwardMidi(midi::controlChange(1, proto::cc::kJog, 64 + 60));
    sim.run(5);
  }
  sim.run(200);
  CHECK_EQ(a.jogTicksReceived(), 600);
  const size_t heldBefore = a.bridged.size();
  CHECK(b.link->forwardMidi(midi::noteOn(1, proto::note::kTouch, 127)));  // held when B reboots
  sim.run(100);

  // B restarts in well under the 1.5 s timeout: same MAC, new epoch, jog counter back at 0.
  b.reboot();
  Sim::configure(b, 0x0B, 1, false, 0, 777);
  sim.run(2000);
  CHECK(a.link->routeIn() && b.link->routeOut());
  CHECK_EQ(a.jogTicksReceived(), 600);  // no phantom -600
  CHECK_EQ(count(a.bridged, midi::noteOff(1, proto::note::kTouch, 0)), 1);  // the old boot's held note is released
  CHECK(a.bridged.size() > heldBefore);
  b.link->forwardMidi(midi::controlChange(1, proto::cc::kJog, 64 + 7));
  sim.run(100);
  CHECK_EQ(a.jogTicksReceived(), 607);
  // and the reliable channel works again in both directions
  CHECK(b.link->forwardMidi(midi::noteOn(1, proto::note::kRev, 127)));
  CHECK(a.link->relayHostMidi(midi::noteOn(1, proto::note::kPlay, 127)));
  sim.run(100);
  CHECK_EQ(count(a.bridged, midi::noteOn(1, proto::note::kRev, 127)), 1);
  CHECK_EQ(count(b.feedback, midi::noteOn(1, proto::note::kPlay, 127)), 1);
}

EPDJ_TEST(node_transport_is_reliable_exactly_once_and_carries_translated_time) {
  Sim sim(7);
  sim.loss = 0.30;
  sim.dup = 0.20;
  // Different counters (B is ~2 hours ahead, A's wraps during the run) and crystals 150 ppm apart.
  Node& b = sim.add(0x0B, 1, false, 7.2e9, +70.0);
  Node& a = sim.add(0x0A, 0, true, kTwo32 - 6e6, -80.0);
  sim.run(8000);
  CHECK(b.link->timeSync().locked());
  CHECK(!b.link->selfIsMaster());

  proto::TransportState st[3] = {proto::TransportState::Playing, proto::TransportState::Paused, proto::TransportState::Stopped};
  for (int round = 0; round < 3; ++round) {
    TransportMsg m;
    m.state = st[round];
    m.bpmX10 = static_cast<uint16_t>(1280 + round);
    m.positionClocks = static_cast<uint32_t>(96 * round);
    const int64_t at = sim.nowUs;
    m.atUs = static_cast<uint32_t>(b.clock(at));
    CHECK(b.link->sendTransport(m, b.clock(at)));
    sim.run(400);
    CHECK_EQ(a.transports.size(), static_cast<size_t>(round + 1));  // once, despite loss and duplication
    const TransportEvent& e = a.transports.back();
    CHECK(e.msg.state == m.state);
    CHECK_EQ(e.msg.bpmX10, m.bpmX10);
    CHECK_EQ(e.msg.positionClocks, m.positionClocks);
    CHECK(!e.fromMaster);  // B is the follower; A (deck A) is the master
    CHECK(e.atLocalValid);
    // B's timestamp of "now" expressed in A's clock vs A's actual clock at that moment
    const double err = static_cast<double>(static_cast<int32_t>(e.atLocalUs - static_cast<uint32_t>(a.clock(at))));
    CHECK(std::fabs(err) < 300.0);
  }
  // From the master to the follower: flagged as master's.
  TransportMsg fromA;
  fromA.state = proto::TransportState::Playing;
  fromA.atUs = static_cast<uint32_t>(a.clock(sim.nowUs));
  CHECK(a.link->sendTransport(fromA, a.clock(sim.nowUs)));
  sim.run(400);
  CHECK_EQ(b.transports.size(), 1);
  CHECK(b.transports[0].fromMaster);

  // Follower and master convert the master's clock the same way the events did.
  uint32_t local = 0;
  CHECK(b.link->masterTimeToLocal(static_cast<uint32_t>(a.clock(sim.nowUs)), &local));
  CHECK(std::fabs(static_cast<double>(static_cast<int32_t>(local - static_cast<uint32_t>(b.clock(sim.nowUs))))) < 300.0);
  uint32_t master = 0;
  CHECK(a.link->masterTimeToLocal(12345, &local) && local == 12345);  // the master's own clock is the time base
  CHECK(b.link->localTimeToMaster(static_cast<uint32_t>(b.clock(sim.nowUs)), &master));
  CHECK(std::fabs(static_cast<double>(static_cast<int32_t>(master - static_cast<uint32_t>(a.clock(sim.nowUs))))) < 300.0);
}

EPDJ_TEST(node_newer_transport_supersedes_one_in_flight_and_late_joiner_gets_state) {
  Sim sim(8);
  Node& a = sim.add(0x0A, 0, true);
  // A alone: the state is remembered for whoever appears.
  TransportMsg m;
  m.state = proto::TransportState::Playing;
  m.bpmX10 = 1330;
  m.positionClocks = 48;
  CHECK(!a.link->sendTransport(m, a.clock(sim.nowUs)));  // nobody to send to yet
  sim.run(500);
  Node& b = sim.add(0x0B, 1, false);
  sim.run(2500);
  CHECK_EQ(b.transports.size(), 1);  // delivered once the peer showed up
  CHECK_EQ(b.transports[0].msg.bpmX10, 1330);

  // Three updates in a row while the air is dead; only the newest survives to be delivered.
  sim.loss = 1.0;
  for (uint16_t bpm : {1400, 1410, 1420}) {
    m.bpmX10 = bpm;
    a.link->sendTransport(m, a.clock(sim.nowUs));
    sim.run(5);
  }
  sim.loss = 0.0;
  sim.run(300);
  CHECK(b.transports.size() >= 2);
  CHECK_EQ(b.transports.back().msg.bpmX10, 1420);
  for (size_t i = 1; i + 1 < b.transports.size(); ++i) CHECK(b.transports[i].msg.bpmX10 != 1400);
}

EPDJ_TEST(node_time_sync_through_the_link_stays_within_200us) {
  Sim sim(9);
  sim.baseUs = 800;
  sim.jitterUs = 120;
  Node& b = sim.add(0x0B, 1, false, kTwo32 - 15e6, +90.0);
  Node& a = sim.add(0x0A, 0, true, 3.3e9, -60.0);
  double worst = 0.0;
  for (int ms = 0; ms < 40000; ++ms) {
    sim.step();
    if (ms > 12000 && ms % 7 == 0) {
      uint32_t local = 0;
      CHECK(b.link->masterTimeToLocal(static_cast<uint32_t>(a.clock(sim.nowUs)), &local));
      worst = std::max(worst, std::fabs(static_cast<double>(static_cast<int32_t>(local - static_cast<uint32_t>(b.clock(sim.nowUs))))));
    }
  }
  std::printf("    (follower's view of the master's clock: worst error %.0f us after 12 s, 150 ppm crystal mismatch)\n", worst);
  CHECK(worst < 200.0);
  CHECK(b.link->timeSync().locked());
  CHECK_NEAR(b.link->timeSync().skewPpm(), -150.0, 12.0);
}

EPDJ_TEST(node_duplicate_deck_id_is_surfaced_not_fought) {
  Sim sim(10);
  Node& x = sim.add(0x21, 0, true);
  Node& y = sim.add(0x12, 0, false);  // also "deck A"
  sim.run(3000);
  CHECK(x.link->deckConflict() && y.link->deckConflict());
  proto::Peer p[3];
  CHECK_EQ(x.link->fillPeers(p, 3, 0), 1);
  CHECK_EQ(p[0].status, Link::kPeerConflict);
  CHECK_EQ(p[0].deck, 0);
  // Still exactly one master (lower MAC), still a working bridge: the conflict is reported, not a hang.
  CHECK(y.link->selfIsMaster() && !x.link->selfIsMaster());
  CHECK(y.link->routeOut() && x.link->routeIn());
  // Fixing one unit's deck id clears it (after the reboot the new id is in every packet).
  x.reboot();
  Sim::configure(x, 0x21, 1, true, 0, 4242);
  sim.run(1500);
  CHECK(!x.link->deckConflict() && !y.link->deckConflict());
  CHECK_EQ(y.link->fillPeers(p, 3, 0), 1);
  CHECK_EQ(p[0].status, 0);
}

EPDJ_TEST(node_groups_isolate_rigs_and_non_deck_peers_are_listed_not_used) {
  Sim sim(11);
  Node& a1 = sim.add(0x0A, 0, true, 5e6, 0, /*group=*/1);
  Node& b1 = sim.add(0x0B, 1, false, 5e6, 0, 1);
  Node& a2 = sim.add(0x1A, 0, true, 5e6, 0, /*group=*/2);  // the DJ at the next table
  sim.run(3000);
  CHECK_EQ(a1.link->peers().count(), 1);
  CHECK_EQ(a2.link->peers().count(), 0);
  CHECK(a1.link->routeIn() && b1.link->routeOut());
  CHECK(!a2.link->linkUp());
  CHECK(a2.link->stats().rxForeign > 0);
  CHECK(a1.link->stats().rxForeign > 0);

  // A third device that is not a deck shows up in LINK_STATUS but never becomes partner or master.
  Node& mixer = sim.add(0x3C, kDeckNone, false, 5e6, 0, 1);
  sim.run(2000);
  CHECK_EQ(a1.link->peers().count(), 2);
  proto::Peer p[3];
  CHECK_EQ(a1.link->fillPeers(p, 3, 0), 2);
  bool sawMixer = false;
  for (size_t i = 0; i < 2; ++i) sawMixer = sawMixer || p[i].deck == kDeckNone;
  CHECK(sawMixer);
  CHECK(a1.link->selfIsMaster());
  CHECK(a1.link->routeIn());  // still bridging with B, not confused by the third device
  (void)mixer;
}

EPDJ_TEST(node_hostile_input_is_counted_and_ignored) {
  Sim sim(12);
  Node& b = sim.add(0x0B, 1, false);
  Node& a = sim.add(0x0A, 0, true);
  sim.run(2000);
  const uint8_t junk[] = {1, 2, 3};
  a.link->onPacket(junk, sizeof junk, -30, a.clock(sim.nowUs), a.clock(sim.nowUs));
  a.link->onPacket(nullptr, 0, -30, 0, 0);
  CHECK_EQ(a.link->stats().rxCorrupt, 2);
  Rng r(5);
  for (int i = 0; i < 20000; ++i) {
    uint8_t buf[260];
    const size_t n = r.below(sizeof buf);
    for (size_t k = 0; k < n; ++k) buf[k] = static_cast<uint8_t>(r.next() >> 24);
    a.link->onPacket(buf, n, -50, a.clock(sim.nowUs), a.clock(sim.nowUs));
  }
  CHECK(a.link->stats().rxCorrupt >= 19990);
  CHECK(a.link->linkUp() && a.link->routeIn());

  // Correctly framed packets from the partner carrying nonsense payloads: counted as malformed, no effect.
  Header h;
  h.type = Type::Midi;
  h.deck = 1;
  h.epoch = 100 + 0x0B;
  h.mac = b.link->peers().selfMac();
  uint8_t pkt[kMaxPacket];
  const uint8_t bad[] = {0xFF, 1, 2, 3, 4};
  size_t n = encodePacket(h, bad, sizeof bad, pkt, sizeof pkt);
  const uint32_t before = a.link->stats().rxMalformed;
  a.link->onPacket(pkt, n, -30, a.clock(sim.nowUs), a.clock(sim.nowUs));
  CHECK_EQ(a.link->stats().rxMalformed, before + 1);
  h.type = Type::TimeResp;  // a forged answer to a request nobody made
  const size_t exchanges = a.link->timeSync().exchanges();
  TimeResp forged;
  forged.dst = a.link->peers().selfMac();
  forged.reqId = 200;
  forged.t1 = 1;
  forged.t2 = 2;
  forged.t3 = 3;
  uint8_t pl[TimeResp::kBytes];
  encodePayload(forged, pl, sizeof pl);
  n = encodePacket(h, pl, sizeof pl, pkt, sizeof pkt);
  a.link->onPacket(pkt, n, -30, a.clock(sim.nowUs), a.clock(sim.nowUs));
  CHECK_EQ(a.link->timeSync().exchanges(), exchanges);
  // An ACK addressed to someone else does not complete A's reliable message; one addressed to A does.
  sim.loss = 1.0;  // the real ACKs never arrive
  CHECK(a.link->relayHostMidi(midi::noteOn(1, 36, 127)));
  sim.run(20);
  const uint32_t r0 = a.link->forwarder().retriedBatches();
  Ack ack;
  ack.dst.b[0] = 2;
  ack.dst.b[5] = 0x77;  // not A
  ack.acked = Type::Midi;
  ack.msgId = 1;
  h.type = Type::Ack;
  encodePayload(ack, pl, sizeof pl);
  n = encodePacket(h, pl, Ack::kBytes, pkt, sizeof pkt);
  a.link->onPacket(pkt, n, -30, a.clock(sim.nowUs), a.clock(sim.nowUs));
  sim.run(100);
  CHECK(a.link->forwarder().retriedBatches() > r0);  // still retrying
  ack.dst = a.link->peers().selfMac();
  encodePayload(ack, pl, sizeof pl);
  n = encodePacket(h, pl, Ack::kBytes, pkt, sizeof pkt);
  a.link->onPacket(pkt, n, -30, a.clock(sim.nowUs), a.clock(sim.nowUs));
  const uint32_t r1 = a.link->forwarder().retriedBatches();
  sim.run(200);
  CHECK_EQ(a.link->forwarder().retriedBatches(), r1);  // done
}

EPDJ_TEST(node_transport_keeps_repeating_through_a_dead_patch_and_applies_once) {
  Sim sim(13);
  Node& b = sim.add(0x0B, 1, false);
  Node& a = sim.add(0x0A, 0, true);
  sim.run(2000);
  sim.loss = 1.0;  // 900 ms of dead air: longer than one retry budget (~400 ms), shorter than the 1.5 s timeout
  TransportMsg m;
  m.state = proto::TransportState::Playing;
  m.bpmX10 = 1240;
  CHECK(a.link->sendTransport(m, a.clock(sim.nowUs)));
  sim.run(900);
  CHECK(a.link->stats().transportGiveUps >= 1);
  CHECK(a.link->linkUp());
  CHECK_EQ(b.transports.size(), 0);
  sim.loss = 0.0;
  sim.run(400);
  CHECK_EQ(b.transports.size(), 1);
  CHECK_EQ(b.transports[0].msg.bpmX10, 1240);
  const uint32_t g = a.link->stats().transportGiveUps;
  sim.run(1000);  // acknowledged: no more repeats
  CHECK_EQ(a.link->stats().transportGiveUps, g);
  CHECK_EQ(b.transports.size(), 1);
}

// Everything at once, at random: radio loss, hostile and damaged packets (some with a valid CRC), hosts
// plugged and unplugged, bridge modes flipped, reboots, MIDI of every kind, transport. Under ASan/UBSan
// this is the "nothing can crash or corrupt memory" test; the asserts are the structural invariants.
EPDJ_TEST(node_random_api_soup_keeps_its_invariants) {
  Sim sim(14);
  sim.loss = 0.2;
  sim.dup = 0.2;
  sim.jitterUs = 3000;
  Node& a = sim.add(0x0A, 0, true);
  Node& b = sim.add(0x0B, 1, false);
  Node& m = sim.add(0x3C, kDeckNone, false);
  Rng r(99);
  int upSamples = 0, bridgedSamples = 0;
  for (int ms = 0; ms < 60000; ++ms) {
    Node& n = *sim.nodes[r.below(3)];
    switch (r.below(40)) {
      case 0:
        n.link->setUsbHost(r.below(2) != 0);
        break;
      case 1:
        n.link->setBridgeMode(static_cast<LinkBridge>(r.below(3)));
        break;
      case 2:
        if (r.below(20) == 0) {
          const uint8_t last = n.link->peers().selfMac().b[5];
          const uint8_t deck = n.link->peers().selfDeck();
          n.reboot();
          Sim::configure(n, last, deck, r.below(2) != 0, 0, static_cast<uint16_t>(r.next()));
        }
        break;
      case 3:
      case 4:
      case 5: {  // any MIDI message the engine or host might produce
        uint8_t st = static_cast<uint8_t>(r.below(256));
        const midi::Message msg = midi::make(st, static_cast<uint8_t>(r.below(128)), static_cast<uint8_t>(r.below(128)), static_cast<uint8_t>(r.below(4)));
        n.link->forwardMidi(msg);
        n.link->relayHostMidi(msg);
        break;
      }
      case 6: {
        TransportMsg t;
        t.state = static_cast<proto::TransportState>(r.below(3));
        t.bpmX10 = static_cast<uint16_t>(200 + r.below(2801));
        t.atUs = r.next();
        n.link->sendTransport(t, n.clock(sim.nowUs));
        break;
      }
      case 7: {  // hostile bytes straight into a link
        uint8_t buf[300];
        const size_t len = r.below(sizeof buf);
        for (size_t k = 0; k < len; ++k) buf[k] = static_cast<uint8_t>(r.next() >> 24);
        n.link->onPacket(buf, len, -50, n.clock(sim.nowUs), n.clock(sim.nowUs));
        break;
      }
      case 8: {  // a real packet from the air, damaged, with the CRC repaired so it reaches the decoders
        if (sim.air.empty()) break;
        auto it = sim.air.begin();
        std::advance(it, r.below(static_cast<uint32_t>(std::min<size_t>(sim.air.size(), 8))));
        std::vector<uint8_t> pkt = it->second.bytes;
        if (pkt.size() > kHeaderBytes + kCrcBytes) {
          for (int k = static_cast<int>(r.below(3)); k >= 0; --k) pkt[kHeaderBytes + r.below(static_cast<uint32_t>(pkt.size() - kHeaderBytes - kCrcBytes))] = static_cast<uint8_t>(r.next() >> 24);
          const uint16_t c = crc16(pkt.data(), pkt.size() - 2);
          pkt[pkt.size() - 2] = static_cast<uint8_t>(c & 0xFF);
          pkt[pkt.size() - 1] = static_cast<uint8_t>(c >> 8);
        }
        n.link->onPacket(pkt.data(), pkt.size(), -45, n.clock(sim.nowUs), n.clock(sim.nowUs));
        break;
      }
      default:
        break;
    }
    sim.step();
    if (ms % 50 == 0) {
      upSamples += a.link->linkUp() ? 1 : 0;
      bridgedSamples += (a.link->bridging() || b.link->bridging()) ? 1 : 0;
      for (auto& node : sim.nodes) {
        const Link& l = *node->link;
        CHECK(l.peers().count() <= kMaxPeers);
        if (l.routeOut() || l.routeIn()) CHECK(l.linkUp());
        proto::Peer p[kMaxPeers];
        CHECK_EQ(l.fillPeers(p, kMaxPeers, node->clock(sim.nowUs)), l.peers().count());
      }
    }
  }
  (void)m;
  CHECK(upSamples > 300);       // the units really were talking to each other for much of the run
  CHECK(bridgedSamples > 100);  // and the bridge was active for some of it
}
