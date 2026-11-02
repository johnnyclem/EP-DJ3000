// MidiForwarder -> lossy, duplicating, reordering network -> MidiReceiver.
// The point: jog motion is conserved EXACTLY, fader/knob values are last-value-wins and never regress,
// and discrete events (notes) are applied exactly once and in order, whatever the radio does.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#include "epdj/link.h"
#include "test_harness.h"

using namespace epdj;
using namespace epdj::link;

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

Mac macOf(uint8_t last) {
  Mac m;
  m.b[0] = 2;
  m.b[5] = last;
  return m;
}

struct Pkt {
  uint16_t seq = 0;
  bool heartbeat = false;
  std::vector<uint8_t> payload;
};

// One direction of the radio. Times are in milliseconds.
template <typename T>
struct Net {
  Rng* rng;
  double loss, dup;
  uint32_t minDelay, maxDelay;
  std::multimap<uint64_t, T> q;
  size_t sent = 0, lost = 0;

  void send(uint64_t now, const T& v) {
    ++sent;
    const int copies = rng->uni() < dup ? 2 : 1;
    for (int i = 0; i < copies; ++i) {
      if (rng->uni() < loss) {
        ++lost;
        continue;
      }
      q.emplace(now + minDelay + rng->below(maxDelay - minDelay + 1), v);
    }
  }
  template <typename F>
  void deliver(uint64_t now, F&& f) {
    while (!q.empty() && q.begin()->first <= now) {
      T v = q.begin()->second;
      q.erase(q.begin());
      f(v);
    }
  }
};

// The two halves of the fader CC pair on channel 1; 0xFF = never set.
struct Latch {
  uint8_t value[2] = {0xFF, 0xFF};
  static size_t slot(uint8_t cc) { return cc == proto::cc::kCrossfaderMsb ? 0u : 1u; }
};

struct Params {
  double loss = 0.30;
  double dup = 0.10;
  uint32_t minDelay = 2, maxDelay = 12;  // ms; the spread reorders packets sent 2 ms apart
  uint32_t seconds = 60;
  uint32_t seed = 1;
};

struct Outcome {
  int64_t sentJog = 0, receivedJog = 0;
  std::vector<midi::Message> submittedEvents, deliveredEvents;
  size_t packets = 0, accepted = 0, staleRejected = 0, maxPayload = 0;
  uint32_t abandoned = 0, dropped = 0;
  bool invariantOk = true;
  uint8_t finalSender[2] = {0, 0}, finalReceiver[2] = {0, 0};
};

// Runs the full pipeline and checks the per-packet invariant: after a state block with sequence s is
// applied, the receiver has produced exactly the jog ticks the sender had produced when it built packet s,
// and holds exactly the latched values the sender held then.
Outcome simulate(const Params& prm) {
  Rng rng(prm.seed);
  Net<Pkt> fwd{&rng, prm.loss, prm.dup, prm.minDelay, prm.maxDelay, {}, 0, 0};
  Net<uint8_t> back{&rng, prm.loss, prm.dup, prm.minDelay, prm.maxDelay, {}, 0, 0};
  MidiForwarder tx;
  MidiReceiver rx;
  Outcome out;
  const Mac dst = macOf(2);

  struct Truth {
    int64_t jog;
    Latch latch;
  };
  std::map<uint16_t, Truth> truth;  // by packet seq
  Latch senderLatch, receiverLatch;
  int64_t senderTotal = 0, receiverTotal = 0, baseline = 0;
  bool haveBaseline = false;
  uint16_t seq = 0, lastAccepted = 0;
  uint16_t faderPrev = 0xFFFF;

  const uint64_t ticks = static_cast<uint64_t>(prm.seconds) * 1000 + 2000;  // 2 s of quiet at the end
  uint64_t nextHeartbeat = 100;
  for (uint64_t ms = 0; ms < ticks; ++ms) {
    const uint64_t nowUs = ms * 1000;
    // ---- the engine produces MIDI (stops 2 s before the end so everything can settle) ----
    if (ms < ticks - 2000) {
      if (rng.below(2)) {  // jog, biased clockwise so the 16-bit counter wraps many times
        const int d = rng.range(-20, 63);
        if (d != 0) {
          CHECK(tx.submit(0, midi::controlChange(1, proto::cc::kJog, static_cast<uint8_t>(64 + d))));
          senderTotal += d;
        }
      }
      if (rng.below(8) == 0) {  // 14-bit fader on CC 23/55; an unchanged half is skipped as on the wire
        const uint16_t v = static_cast<uint16_t>(rng.below(16384));
        midi::Message m[2];
        const size_t n = midi::cc14(1, proto::cc::kCrossfaderMsb, proto::cc::kCrossfaderLsb, v,
                                    faderPrev == 0xFFFF ? -1 : faderPrev, m);
        for (size_t i = 0; i < n; ++i) {
          CHECK(tx.submit(0, m[i]));
          senderLatch.value[Latch::slot(m[i].data1())] = m[i].data2();
        }
        faderPrev = v;
      }
      if (rng.below(40) == 0) {  // a note or a tempo nudge: must be delivered once, in order
        midi::Message m = rng.below(5) == 0 ? midi::controlChange(1, proto::cc::kBpmNudge, static_cast<uint8_t>(64 + rng.range(-3, 3)))
                                           : midi::noteOn(1, static_cast<uint8_t>(36 + rng.below(6)), rng.below(2) ? 127 : 0);
        CHECK(tx.submit(0, m));
        out.submittedEvents.push_back(m);
      }
    }
    // ---- sender emits packets ----
    uint8_t buf[kMaxPayload];
    if (ms >= nextHeartbeat) {
      nextHeartbeat += 100;
      const size_t n = tx.buildHeartbeat(buf, sizeof buf);
      CHECK(n > 0);
      Pkt p;
      p.seq = seq++;
      p.heartbeat = true;
      p.payload.assign(buf, buf + n);
      truth[p.seq] = Truth{senderTotal, senderLatch};
      fwd.send(ms, p);
      ++out.packets;
    }
    const size_t n = tx.buildMidi(nowUs, dst, buf, sizeof buf);
    if (n > 0) {
      CHECK(n <= kMaxPayload);
      if (n > out.maxPayload) out.maxPayload = n;
      Pkt p;
      p.seq = seq++;
      p.payload.assign(buf, buf + n);
      truth[p.seq] = Truth{senderTotal, senderLatch};
      fwd.send(ms, p);
      ++out.packets;
    }
    // ---- receiver ----
    fwd.deliver(ms, [&](const Pkt& p) {
      StateBlock state;
      bool hasState = false;
      if (p.heartbeat) {
        CHECK(decodePayload(p.payload.data(), p.payload.size(), &state));
        hasState = true;
      } else {
        MidiPayload m;
        CHECK(decodePayload(p.payload.data(), p.payload.size(), &m));
        if (m.hasEvents) {
          back.send(ms, m.events.msgId);
          if (rx.acceptEvents(m.events.msgId) == MsgIdWindow::Verdict::New) {
            for (size_t i = 0; i < m.events.count; ++i) out.deliveredEvents.push_back(m.events.ev[i].msg);
          }
        }
        hasState = m.hasState;
        state = m.state;
      }
      if (!hasState) return;
      MidiReceiver::StateDelta d;
      if (!rx.applyState(p.seq, state, &d)) {
        ++out.staleRejected;
        return;
      }
      ++out.accepted;
      if (out.accepted > 1 && !seqNewer(p.seq, lastAccepted)) out.invariantOk = false;  // never goes back in time
      lastAccepted = p.seq;
      receiverTotal += d.jogTicks;
      for (size_t i = 0; i < d.ccCount; ++i) {
        CHECK_EQ(d.cc[i].channel(), 1);
        receiverLatch.value[Latch::slot(d.cc[i].data1())] = d.cc[i].data2();
      }
      const Truth& t = truth[p.seq];
      if (!haveBaseline) {  // the first state adopts the counter: ticks before it were never promised
        baseline = t.jog;
        haveBaseline = true;
      }
      if (baseline + receiverTotal != t.jog) out.invariantOk = false;
      if (std::memcmp(receiverLatch.value, t.latch.value, sizeof receiverLatch.value) != 0) out.invariantOk = false;
    });
    back.deliver(ms, [&](uint8_t id) { tx.onAck(id); });
  }
  out.sentJog = senderTotal - baseline;
  out.receivedJog = receiverTotal;
  out.abandoned = tx.abandonedEvents();
  out.dropped = tx.droppedEvents();
  out.finalSender[0] = senderLatch.value[0];
  out.finalSender[1] = senderLatch.value[1];
  out.finalReceiver[0] = receiverLatch.value[0];
  out.finalReceiver[1] = receiverLatch.value[1];
  return out;
}

void expectExact(const Outcome& o) {
  CHECK(o.invariantOk);                    // at every accepted packet
  CHECK_EQ(o.receivedJog, o.sentJog);      // and in the end, nothing lost, nothing counted twice
  CHECK_EQ(o.deliveredEvents.size(), o.submittedEvents.size());
  CHECK(o.deliveredEvents == o.submittedEvents);  // exactly once, in order
  CHECK_EQ(o.dropped, 0);
  CHECK_EQ(o.finalReceiver[0], o.finalSender[0]);
  CHECK_EQ(o.finalReceiver[1], o.finalSender[1]);
  CHECK(o.maxPayload <= kMaxPayload);
}

}  // namespace

EPDJ_TEST(forwarder_30pct_loss_reorder_duplication_conserves_jog_exactly) {
  Params p;
  const Outcome o = simulate(p);
  std::printf("    (%zu packets, %zu state blocks applied, %zu stale/duplicate rejected, jog %lld ticks, %zu events, "
              "%u batches abandoned)\n",
              o.packets, o.accepted, o.staleRejected, static_cast<long long>(o.sentJog), o.submittedEvents.size(),
              o.abandoned);
  expectExact(o);
  CHECK(o.staleRejected > 100);  // the network really did reorder/duplicate
  CHECK(std::llabs(o.sentJog) > 65536 || o.sentJog > 100000);  // the 16-bit counter wrapped repeatedly
}

EPDJ_TEST(forwarder_many_seeds_and_profiles) {
  for (uint32_t seed = 2; seed <= 9; ++seed) {
    Params p;
    p.seed = seed;
    p.seconds = 20;
    expectExact(simulate(p));
  }
  Params hard;  // brutal: 60% loss, half the survivors duplicated, wide reordering
  hard.loss = 0.60;
  hard.dup = 0.5;
  hard.minDelay = 1;
  hard.maxDelay = 40;
  hard.seconds = 20;
  hard.seed = 77;
  const Outcome o = simulate(hard);
  CHECK(o.invariantOk);
  CHECK_EQ(o.receivedJog, o.sentJog);
  // Events may exceed the retry budget at 60% loss; whatever arrives is still in order, never twice.
  size_t j = 0;
  for (const auto& m : o.deliveredEvents) {
    while (j < o.submittedEvents.size() && o.submittedEvents[j] != m) ++j;
    CHECK(j < o.submittedEvents.size());
    ++j;
  }
  Params clean;  // no loss at all: the same exactness, and no stale state either
  clean.loss = 0.0;
  clean.dup = 0.0;
  clean.minDelay = clean.maxDelay = 2;
  clean.seconds = 10;
  const Outcome c = simulate(clean);
  expectExact(c);
  CHECK_EQ(c.staleRejected, 0);
}

EPDJ_TEST(receiver_ignores_stale_and_duplicate_state_and_wraps) {
  MidiReceiver rx;
  MidiReceiver::StateDelta d;
  StateBlock s;
  s.jogChannel = 3;
  s.nLatched = 1;
  s.latched[0] = {3, 23, 10};
  s.jogCount = 65530;
  CHECK(rx.applyState(10, s, &d));  // first: adopts
  CHECK_EQ(d.jogTicks, 0);
  CHECK_EQ(d.ccCount, 1);           // initial latched value is announced
  CHECK_EQ(d.jogChannel, 3);
  CHECK(d.cc[0] == midi::controlChange(3, 23, 10));
  s.jogCount = 4;  // 65530 -> 4 across the wrap: +10
  s.latched[0].value = 11;
  CHECK(rx.applyState(12, s, &d));  // packet 11 was lost
  CHECK_EQ(d.jogTicks, 10);
  CHECK_EQ(d.ccCount, 1);
  s.jogCount = 2;  // packet 11, late: older than 12
  s.latched[0].value = 5;
  CHECK(!rx.applyState(11, s, &d));
  CHECK(!rx.applyState(12, s, &d));  // duplicate of 12
  s.jogCount = 4;
  s.latched[0].value = 11;
  CHECK(rx.applyState(13, s, &d));  // nothing changed: no ticks, no CC
  CHECK_EQ(d.jogTicks, 0);
  CHECK_EQ(d.ccCount, 0);
  s.jogCount = static_cast<uint16_t>(4 - 70);  // backwards
  CHECK(rx.applyState(14, s, &d));
  CHECK_EQ(d.jogTicks, -70);
  // sequence wrap
  rx.reset();
  CHECK(rx.applyState(65535, s, &d));
  CHECK(rx.applyState(0, s, &d));
  CHECK(!rx.applyState(65535, s, &d));
  // forgetState re-adopts: a long outage must not turn into a giant jump
  s.jogCount = 1000;
  rx.forgetState();
  CHECK(rx.applyState(5, s, &d));
  CHECK_EQ(d.jogTicks, 0);
}

EPDJ_TEST(receiver_emits_each_latched_change_once_in_packet_order) {
  MidiReceiver rx;
  MidiReceiver::StateDelta d;
  StateBlock s;
  s.nLatched = 3;
  s.latched[0] = {0, 23, 64};   // MSB first, as the engine sends it
  s.latched[1] = {0, 55, 1};
  s.latched[2] = {1, 24, 99};
  CHECK(rx.applyState(1, s, &d));
  CHECK_EQ(d.ccCount, 3);
  CHECK(d.cc[0] == midi::controlChange(0, 23, 64));
  CHECK(d.cc[1] == midi::controlChange(0, 55, 1));
  CHECK(d.cc[2] == midi::controlChange(1, 24, 99));
  s.latched[1].value = 2;
  CHECK(rx.applyState(2, s, &d));
  CHECK_EQ(d.ccCount, 1);  // only the LSB changed
  CHECK(d.cc[0] == midi::controlChange(0, 55, 2));
  // The same cc on another channel is another control.
  s.latched[2] = {0, 55, 9};
  CHECK(rx.applyState(3, s, &d));
  CHECK_EQ(d.ccCount, 1);
  CHECK(d.cc[0] == midi::controlChange(0, 55, 9));
}

EPDJ_TEST(forwarder_classifies_messages) {
  MidiForwarder f;
  uint8_t buf[kMaxPayload];
  const Mac dst = macOf(9);
  // refused
  CHECK(!f.submit(0, midi::clock()));
  CHECK(!f.submit(0, midi::start()));
  CHECK(!f.submit(0, midi::songPosition(5)));
  CHECK(!f.submit(0, midi::programChange(0, 3)));
  CHECK(!f.submit(0, midi::Message()));
  CHECK_EQ(f.buildMidi(0, dst, buf, sizeof buf), 0);  // nothing happened
  // jog: counter, no event
  CHECK(f.submit(0, midi::controlChange(4, proto::cc::kJog, 64 + 5)));
  CHECK(f.submit(0, midi::controlChange(4, proto::cc::kJog, 64 - 7)));
  CHECK(f.submit(0, midi::controlChange(4, proto::cc::kJog, 64)));  // "no change": nothing to send
  CHECK_EQ(f.jogCount(), static_cast<uint16_t>(-2));
  size_t n = f.buildMidi(1000000, dst, buf, sizeof buf);
  MidiPayload p;
  CHECK(n > 0 && decodePayload(buf, n, &p));
  CHECK(p.hasState && !p.hasEvents);
  CHECK_EQ(p.state.jogCount, 65534);
  CHECK_EQ(p.state.jogChannel, 4);
  CHECK_EQ(p.state.nLatched, 0);
  // fader halves are latched (not events); a repeat of the same value is not news
  CHECK(f.submit(0, midi::controlChange(0, proto::cc::kCrossfaderMsb, 100)));
  n = f.buildMidi(2000000, dst, buf, sizeof buf);
  CHECK(n > 0 && decodePayload(buf, n, &p) && p.hasState && !p.hasEvents && p.state.nLatched == 1);
  CHECK(f.submit(0, midi::controlChange(0, proto::cc::kCrossfaderMsb, 100)));
  CHECK_EQ(f.buildMidi(3000000, dst, buf, sizeof buf), 0);
  // notes and tempo nudges are events
  CHECK(f.submit(0, midi::noteOn(0, proto::note::kCue, 127)));
  CHECK(f.submit(0, midi::controlChange(0, proto::cc::kBpmNudge, 65)));
  n = f.buildMidi(4000000, dst, buf, sizeof buf);
  CHECK(n > 0 && decodePayload(buf, n, &p));
  CHECK(p.hasEvents && !p.hasState);
  CHECK_EQ(p.events.count, 2);
  CHECK(p.events.dst == dst);
  CHECK(p.events.ev[0].msg == midi::noteOn(0, proto::note::kCue, 127));
  CHECK_EQ(p.events.ev[0].cable, 0);
  // cable-2 traffic (host feedback going back) is always an event, even when it is a CC
  CHECK(f.onAck(p.events.msgId));
  CHECK(f.submit(2, midi::controlChange(0, proto::cc::kCrossfaderMsb, 5)));
  n = f.buildMidi(5000000, dst, buf, sizeof buf);
  CHECK(n > 0 && decodePayload(buf, n, &p) && p.hasEvents && p.events.ev[0].cable == 2);
  // heartbeat carries the whole state block even when nothing is new
  n = f.buildHeartbeat(buf, sizeof buf);
  StateBlock s;
  CHECK(n > 0 && decodePayload(buf, n, &s));
  CHECK_EQ(s.nLatched, 1);
  CHECK_EQ(s.jogCount, 65534);
}

EPDJ_TEST(forwarder_bounded_queues) {
  MidiForwarder f;
  size_t accepted = 0;
  for (size_t i = 0; i < MidiForwarder::kQueueCap + 10; ++i) {
    accepted += f.submit(0, midi::noteOn(0, static_cast<uint8_t>(i & 0x7F), 100)) ? 1u : 0u;
  }
  CHECK_EQ(accepted, MidiForwarder::kQueueCap);
  CHECK_EQ(f.droppedEvents(), 10);

  // More distinct (channel, cc) controls than latch slots: the surplus travels as events, nothing is lost.
  MidiForwarder g;
  size_t events = 0;
  for (uint8_t cc = 0; cc < kMaxLatched + 4; ++cc) CHECK(g.submit(0, midi::controlChange(0, cc, 7)));
  uint8_t buf[kMaxPayload];
  MidiPayload p;
  size_t n = g.buildMidi(1000000, macOf(1), buf, sizeof buf);
  CHECK(n > 0 && decodePayload(buf, n, &p));
  CHECK_EQ(p.state.nLatched, kMaxLatched);
  CHECK(p.hasEvents);
  events += p.events.count;
  CHECK_EQ(events, 4);
  CHECK_EQ(g.droppedEvents(), 0);
}

EPDJ_TEST(forwarder_event_batches_stop_and_wait_with_backoff) {
  MidiForwarder f;
  uint8_t buf[kMaxPayload];
  const Mac dst = macOf(1);
  for (int i = 0; i < 20; ++i) CHECK(f.submit(0, midi::noteOn(0, static_cast<uint8_t>(40 + i), 127)));
  uint64_t now = 1000000;
  size_t n = f.buildMidi(now, dst, buf, sizeof buf);
  MidiPayload p;
  CHECK(n > 0 && decodePayload(buf, n, &p) && p.hasEvents);
  CHECK_EQ(p.events.count, kMaxEventsPerPacket);  // one batch of 8 in flight, the rest waits
  const uint8_t id1 = p.events.msgId;
  // No ACK: the same batch (same id, same events) is repeated after the first RTO, not before.
  CHECK_EQ(f.buildMidi(now + 5000, dst, buf, sizeof buf), 0);
  n = f.buildMidi(now + 6000, dst, buf, sizeof buf);
  CHECK(n > 0 && decodePayload(buf, n, &p) && p.hasEvents);
  CHECK_EQ(p.events.msgId, id1);
  CHECK_EQ(p.events.count, kMaxEventsPerPacket);
  CHECK_EQ(p.events.ev[0].msg.data1(), 40);
  // A stale ACK (wrong id) does nothing; the right one releases the next batch immediately.
  CHECK(!f.onAck(static_cast<uint8_t>(id1 + 1)));
  CHECK(f.onAck(id1));
  CHECK(!f.onAck(id1));
  now += 20000;
  n = f.buildMidi(now, dst, buf, sizeof buf);
  CHECK(n > 0 && decodePayload(buf, n, &p) && p.hasEvents);
  CHECK_EQ(p.events.msgId, static_cast<uint8_t>(id1 + 1));
  CHECK_EQ(p.events.ev[0].msg.data1(), 48);
  // State sent while a batch is outstanding carries that batch too, so it can never overtake it.
  CHECK(f.submit(0, midi::controlChange(0, proto::cc::kJog, 70)));
  now += 3000;
  n = f.buildMidi(now, dst, buf, sizeof buf);
  CHECK(n > 0 && decodePayload(buf, n, &p));
  CHECK(p.hasState && p.hasEvents);
  CHECK_EQ(p.events.msgId, static_cast<uint8_t>(id1 + 1));
  // Never ACKed: after the retry budget the batch is abandoned and counted, and the queue moves on.
  for (int i = 0; i < 80; ++i) {
    now += 50000;
    f.buildMidi(now, dst, buf, sizeof buf);
  }
  CHECK_EQ(f.abandonedEvents(), 12);  // the batch of 8 and the final batch of 4; the first 8 were ACKed
  CHECK(f.retriedBatches() >= 10);
}

EPDJ_TEST(forwarder_packet_rate_is_limited_to_500_per_second) {
  MidiForwarder f;
  uint8_t buf[kMaxPayload];
  size_t packets = 0;
  for (uint64_t us = 1000000; us < 2000000; us += 100) {
    f.submit(0, midi::controlChange(0, proto::cc::kJog, 65));  // dirty on every call
    if (f.buildMidi(us, macOf(1), buf, sizeof buf) > 0) ++packets;
  }
  CHECK(packets <= 500);
  CHECK(packets >= 499);
  // ...but a change after a quiet period goes out at once (no batching delay on the first one).
  MidiForwarder g;
  g.submit(0, midi::controlChange(0, proto::cc::kJog, 65));
  CHECK(g.buildMidi(5000000, macOf(1), buf, sizeof buf) > 0);
}

EPDJ_TEST(receiver_tracks_held_notes_for_release) {
  MidiReceiver rx;
  rx.noteHeld(midi::noteOn(1, 36, 127));
  rx.noteHeld(midi::noteOn(1, 38, 127));
  rx.noteHeld(midi::noteOn(2, 36, 100));
  rx.noteHeld(midi::noteOn(1, 36, 127));   // repeated: still one
  rx.noteHeld(midi::noteOff(1, 38, 0));    // released normally
  rx.noteHeld(midi::noteOn(0, 40, 0));     // note-on velocity 0 is a release (of something not held)
  rx.noteHeld(midi::controlChange(1, 7, 7));
  midi::Message off[8];
  const size_t n = rx.releaseHeld(off, 8);
  CHECK_EQ(n, 2);
  bool a = false, b = false;
  for (size_t i = 0; i < n; ++i) {
    a = a || off[i] == midi::noteOff(1, 36, 0);
    b = b || off[i] == midi::noteOff(2, 36, 0);
  }
  CHECK(a && b);
  CHECK_EQ(rx.releaseHeld(off, 8), 0);  // cleared
}
