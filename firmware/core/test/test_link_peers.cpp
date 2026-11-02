// PeerTable (liveness, election, conflicts), the bridge decision and the stop-and-wait bookkeeping.
#include <cstring>

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
  uint32_t below(uint32_t n) { return next() % n; }
};

Mac mac(uint8_t last, uint8_t first = 0x02) {
  Mac m;
  m.b[0] = first;
  m.b[5] = last;
  return m;
}

Header hdr(const Mac& m, uint8_t deck, uint16_t epoch = 1, uint8_t flags = 0) {
  Header h;
  h.mac = m;
  h.deck = deck;
  h.epoch = epoch;
  h.flags = flags;
  return h;
}

constexpr uint64_t kSec = 1000000;

}  // namespace

EPDJ_TEST(peers_add_refresh_and_self) {
  PeerTable t;
  t.setSelf(mac(1), 0);
  CHECK(t.onHeard(hdr(mac(1), 1), -40, 0) == PeerTable::Heard::Self);
  CHECK_EQ(t.count(), 0);
  CHECK(t.onHeard(hdr(mac(2), 1, 9, proto::peerFlags::kUsbHost), -42, 100) == PeerTable::Heard::Added);
  CHECK_EQ(t.count(), 1);
  const PeerEntry* e = t.find(mac(2));
  CHECK(e != nullptr);
  CHECK_EQ(e->deck, 1);
  CHECK_EQ(e->rssi, -42);
  CHECK_EQ(e->epoch, 9);
  CHECK(e->usbHost());
  CHECK(t.onHeard(hdr(mac(2), 1, 9, 0), -50, 200) == PeerTable::Heard::Refreshed);
  CHECK(!t.find(mac(2))->usbHost());  // flags follow the latest packet
  CHECK_EQ(t.find(mac(2))->rssi, -50);
  CHECK(t.find(mac(3)) == nullptr);
}

EPDJ_TEST(peers_timeout_is_exactly_1500_ms) {
  PeerTable t;
  t.setSelf(mac(1), 0);
  t.onHeard(hdr(mac(2), 1), 0, 1000);
  CHECK_EQ(t.expire(1000 + kPeerTimeoutUs - 1), 0);
  CHECK_EQ(t.count(), 1);
  CHECK_EQ(t.expire(1000 + kPeerTimeoutUs), 1);
  CHECK_EQ(t.count(), 0);
  CHECK_EQ(kPeerTimeoutUs, 1500000);

  // Any packet resets the clock.
  t.onHeard(hdr(mac(2), 1), 0, 10 * kSec);
  t.onHeard(hdr(mac(2), 1), 0, 10 * kSec + 1400000);
  CHECK_EQ(t.expire(10 * kSec + 2000000), 0);
  CHECK_EQ(t.expire(10 * kSec + 1400000 + kPeerTimeoutUs), 1);

  // A clock that reads earlier than lastHeard (never expected) must not underflow into "expired".
  t.onHeard(hdr(mac(2), 1), 0, 5 * kSec);
  CHECK_EQ(t.expire(4 * kSec), 0);
}

EPDJ_TEST(peers_capacity_is_three_and_slots_are_reused) {
  PeerTable t;
  t.setSelf(mac(1), 0);
  CHECK(t.onHeard(hdr(mac(2), 1), 0, 0) == PeerTable::Heard::Added);
  CHECK(t.onHeard(hdr(mac(3), kDeckNone), 0, 100) == PeerTable::Heard::Added);
  CHECK(t.onHeard(hdr(mac(4), 1), 0, 200) == PeerTable::Heard::Added);
  CHECK(t.onHeard(hdr(mac(5), 1), 0, 300) == PeerTable::Heard::TableFull);
  CHECK_EQ(t.count(), 3);
  CHECK_EQ(t.tableFullCount(), 1);
  CHECK(t.find(mac(5)) == nullptr);
  // Known peers keep being served while the table is full.
  CHECK(t.onHeard(hdr(mac(3), kDeckNone), 0, 400) == PeerTable::Heard::Refreshed);
  // mac(2) times out; the newcomer gets its slot.
  CHECK_EQ(t.expire(kPeerTimeoutUs + 50), 1);  // only mac(2) (heard at 0) is old enough
  CHECK(t.onHeard(hdr(mac(5), 1), 0, kPeerTimeoutUs + 60) == PeerTable::Heard::Added);
}

EPDJ_TEST(peers_epoch_change_means_the_peer_rebooted) {
  PeerTable t;
  t.setSelf(mac(1), 0);
  t.onHeard(hdr(mac(2), 1, 100), 0, 0);
  t.setBridge(mac(2), LinkBridge::Force);
  CHECK(t.find(mac(2))->bridge == LinkBridge::Force);
  CHECK(t.onHeard(hdr(mac(2), 1, 100), 0, 10) == PeerTable::Heard::Refreshed);
  CHECK(t.find(mac(2))->bridge == LinkBridge::Force);
  CHECK(t.onHeard(hdr(mac(2), 1, 101), 0, 20) == PeerTable::Heard::Restarted);
  CHECK(t.find(mac(2))->bridge == LinkBridge::Auto);  // re-announced by the new boot's HELLO
  t.setBridge(mac(9), LinkBridge::Off);                 // unknown MAC: ignored
  CHECK_EQ(t.count(), 1);
}

EPDJ_TEST(peers_master_election) {
  {  // alone: master by default
    PeerTable t;
    t.setSelf(mac(5), 1);
    CHECK(t.selfIsMaster());
    CHECK(t.master() == nullptr);
    CHECK(t.partner() == nullptr);
  }
  {  // lower deck id wins, whatever the MACs
    PeerTable a;
    a.setSelf(mac(0xF0), 0);
    a.onHeard(hdr(mac(0x01), 1), 0, 0);
    CHECK(a.selfIsMaster());
    PeerTable b;
    b.setSelf(mac(0x01), 1);
    b.onHeard(hdr(mac(0xF0), 0), 0, 0);
    CHECK(!b.selfIsMaster());
    CHECK(b.master() != nullptr && b.master()->mac == mac(0xF0));
    CHECK(b.partner() == b.master());
  }
  {  // same deck id: lower MAC wins, and both sides agree
    PeerTable a, b;
    a.setSelf(mac(0x10), 0);
    a.onHeard(hdr(mac(0x20), 0), 0, 0);
    b.setSelf(mac(0x20), 0);
    b.onHeard(hdr(mac(0x10), 0), 0, 0);
    CHECK(a.selfIsMaster());
    CHECK(!b.selfIsMaster());
  }
  {  // a non-deck device never takes mastership, and cannot be elected
    PeerTable t;
    t.setSelf(mac(5), 1);
    t.onHeard(hdr(mac(1), kDeckNone), 0, 0);
    CHECK(t.selfIsMaster());
    CHECK(t.partner() == nullptr);
    PeerTable mixer;
    mixer.setSelf(mac(1), kDeckNone);
    mixer.onHeard(hdr(mac(5), 1), 0, 0);
    CHECK(!mixer.selfIsMaster());
  }
  {  // the master leaves: the survivor takes over
    PeerTable t;
    t.setSelf(mac(5), 1);
    t.onHeard(hdr(mac(9), 0), 0, 0);
    CHECK(!t.selfIsMaster());
    t.expire(kPeerTimeoutUs);
    CHECK(t.selfIsMaster());
  }
}

EPDJ_TEST(peers_election_agrees_across_units) {
  // Random sets of up to 4 units that all hear each other: exactly one believes it is master, and it is
  // the one with the lowest (deck, MAC).
  Rng r(4);
  for (int trial = 0; trial < 500; ++trial) {
    const size_t n = 2 + r.below(3);
    Mac macs[4];
    uint8_t decks[4];
    for (size_t i = 0; i < n; ++i) {
      macs[i] = mac(static_cast<uint8_t>(1 + r.below(250)), static_cast<uint8_t>(2 + 4 * r.below(60)));
      for (size_t j = 0; j < i; ++j) {
        if (macs[j] == macs[i]) macs[i].b[4] = static_cast<uint8_t>(i + 1);
      }
      decks[i] = static_cast<uint8_t>(r.below(2));
    }
    size_t best = 0;
    for (size_t i = 1; i < n; ++i) {
      if (decks[i] < decks[best] || (decks[i] == decks[best] && macs[i] < macs[best])) best = i;
    }
    size_t masters = 0;
    for (size_t i = 0; i < n; ++i) {
      PeerTable t;
      t.setSelf(macs[i], decks[i]);
      size_t heard = 0;
      for (size_t j = 0; j < n; ++j) {
        if (j == i) continue;
        if (heard++ < kMaxPeers) t.onHeard(hdr(macs[j], decks[j]), 0, 0);
      }
      if (t.selfIsMaster()) {
        ++masters;
        CHECK_EQ(i, best);
      }
    }
    CHECK_EQ(masters, 1);
  }
}

EPDJ_TEST(peers_deck_conflict_is_detected_and_clears) {
  PeerTable t;
  t.setSelf(mac(1), 0);
  CHECK(!t.deckConflict());
  t.onHeard(hdr(mac(2), 1), 0, 0);
  CHECK(!t.deckConflict());
  t.onHeard(hdr(mac(3), 0), 0, 100);  // claims our deck id
  CHECK(t.deckConflict());
  CHECK(t.inConflict(*t.find(mac(3))));
  CHECK(!t.inConflict(*t.find(mac(2))));
  // The election still picks one master deterministically: us (deck 0, lower MAC).
  CHECK(t.selfIsMaster());
  t.expire(kPeerTimeoutUs + 50);  // mac(2) expires, mac(3) heard at 100 still alive
  CHECK(t.deckConflict());
  t.expire(100 + kPeerTimeoutUs);
  CHECK(!t.deckConflict());

  // Two peers claiming the same deck id between themselves.
  PeerTable u;
  u.setSelf(mac(1), 0);
  u.onHeard(hdr(mac(2), 1), 0, 0);
  u.onHeard(hdr(mac(3), 1), 0, 0);
  CHECK(u.deckConflict());
  CHECK(u.inConflict(*u.find(mac(2))) && u.inConflict(*u.find(mac(3))));

  // A non-deck device with whatever id byte is never a conflict.
  PeerTable v;
  v.setSelf(mac(1), 0);
  v.onHeard(hdr(mac(2), kDeckNone), 0, 0);
  v.onHeard(hdr(mac(3), kDeckNone), 0, 0);
  CHECK(!v.deckConflict());
}

namespace {
// Reference for the documented rule, written independently of bridgeRoute().
bool expectedRoute(bool srcHost, LinkBridge srcMode, bool dstHost, LinkBridge dstMode) {
  if (srcMode == LinkBridge::Off || dstMode == LinkBridge::Off) return false;
  if (dstMode == LinkBridge::Force) return true;
  // dst is Auto: bridge exactly when the receiver has a host and the sender does not
  return dstHost && !srcHost;
}
}  // namespace

EPDJ_TEST(bridge_route_truth_table) {
  const LinkBridge modes[3] = {LinkBridge::Auto, LinkBridge::Off, LinkBridge::Force};
  for (bool sh : {false, true})
    for (bool dh : {false, true})
      for (LinkBridge sm : modes)
        for (LinkBridge dm : modes) {
          const bool got = bridgeRoute(BridgeEnd{sh, sm}, BridgeEnd{dh, dm});
          CHECK(got == expectedRoute(sh, sm, dh, dm));
        }
  // The scenarios the product is about (defaults: both Auto).
  const BridgeEnd none{false, LinkBridge::Auto}, host{true, LinkBridge::Auto};
  CHECK(bridgeRoute(none, host));    // power-only deck -> deck on the Mac
  CHECK(!bridgeRoute(host, none));   // and not the other way round
  CHECK(!bridgeRoute(host, host));   // two cables to the Mac: each talks on its own
  CHECK(!bridgeRoute(none, none));   // nobody to bridge to
  // The receiving unit's setting decides; Force overrides the host test, Off beats everything.
  CHECK(bridgeRoute(host, BridgeEnd{false, LinkBridge::Force}));
  CHECK(!bridgeRoute(none, BridgeEnd{true, LinkBridge::Off}));
  CHECK(!bridgeRoute(BridgeEnd{false, LinkBridge::Off}, host));
}

EPDJ_TEST(retry_timer_schedule_and_give_up) {
  RetryTimer t;
  CHECK(t.poll(0) == RetryTimer::Poll::Idle);
  t.begin(7, 1000);
  CHECK(t.active());
  CHECK_EQ(t.id(), 7);
  CHECK(t.poll(1000 + 5999) == RetryTimer::Poll::Wait);
  CHECK(t.poll(1000 + 6000) == RetryTimer::Poll::Resend);  // attempt 2 after 6 ms
  uint64_t now = 1000 + 6000;
  const uint32_t rto[] = {12000, 24000, 40000, 40000, 40000, 40000, 40000, 40000, 40000, 40000};
  for (uint32_t gap : rto) {  // attempts 3..12
    CHECK(t.poll(now + gap - 1) == RetryTimer::Poll::Wait);
    now += gap;
    const RetryTimer::Poll p = t.poll(now);
    CHECK(p == RetryTimer::Poll::Resend);
    if (p != RetryTimer::Poll::Resend) return;
  }
  // 12 transmissions have happened (1 + 11 resends); the next expiry gives up.
  CHECK(t.poll(now + 39999) == RetryTimer::Poll::Wait);
  CHECK(t.poll(now + 40000) == RetryTimer::Poll::GiveUp);
  CHECK(!t.active());
  CHECK(t.poll(now + 1000000) == RetryTimer::Poll::Idle);
  CHECK(now - 1000 < 400000);  // gives up within about 0.4 s
}

EPDJ_TEST(retry_timer_ack_matches_id_only) {
  RetryTimer t;
  t.begin(3, 0);
  CHECK(!t.ack(2));
  CHECK(!t.ack(4));
  CHECK(t.active());
  CHECK(t.ack(3));
  CHECK(!t.active());
  CHECK(!t.ack(3));  // a duplicate ACK is harmless
  CHECK(t.poll(1000000) == RetryTimer::Poll::Idle);
  t.begin(200, 0);
  t.cancel();
  CHECK(!t.active());
}

EPDJ_TEST(msg_id_window_applies_each_id_once) {
  MsgIdWindow w;
  using V = MsgIdWindow::Verdict;
  CHECK(w.check(250) == V::New);  // first id seen, whatever its value
  CHECK(w.check(250) == V::Duplicate);
  CHECK(w.check(251) == V::New);
  CHECK(w.check(250) == V::Duplicate);  // late retransmission of an older message
  CHECK(w.check(251) == V::Duplicate);
  CHECK(w.check(255) == V::New);        // sender gave up on 252..254
  CHECK(w.check(0) == V::New);          // wraps
  CHECK(w.check(1) == V::New);
  CHECK(w.check(255) == V::Duplicate);
  CHECK(w.check(1) == V::Duplicate);
  w.reset();
  CHECK(w.check(1) == V::New);
  // A jump of exactly half the id space is treated as old, one less as new.
  MsgIdWindow x;
  x.check(0);
  CHECK(x.check(128) == V::Duplicate);
  CHECK(x.check(127) == V::New);
}

EPDJ_TEST(seq_newer_is_serial_number_arithmetic) {
  CHECK(seqNewer(1, 0));
  CHECK(!seqNewer(0, 1));
  CHECK(!seqNewer(5, 5));
  CHECK(seqNewer(0, 65535));
  CHECK(!seqNewer(65535, 0));
  CHECK(seqNewer(100, 65500));
  CHECK(seqNewer(0x8000, 1));
  CHECK(!seqNewer(0x8001, 1));  // more than half the space ahead counts as behind
  CHECK(seqNewer(0x7FFF + 1, 1));
}
