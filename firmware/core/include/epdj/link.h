// Unit-to-unit radio link over a broadcast, lossy, reordering datagram transport (ESP-NOW).
// Normative wire spec: docs/LINK.md. Everything here is a plain state machine driven by a monotonic
// microsecond clock passed in by the caller; nothing allocates, blocks or touches hardware, and every
// loop is bounded. One thread (the platform's link task) must own all calls into a Link.
#pragma once
#include <cstddef>
#include <cstdint>

#include "epdj/jog.h"
#include "epdj/midi.h"
#include "epdj/protocol.h"

namespace epdj {
namespace link {

// ---- constants ------------------------------------------------------------------------------------

constexpr uint8_t kMagic0 = 0x45;  // 'E'
constexpr uint8_t kMagic1 = 0x4A;  // 'J'
constexpr uint8_t kVersion = 1;
constexpr size_t kMaxPacket = 250;  // ESP-NOW payload limit
constexpr size_t kHeaderBytes = 18;
constexpr size_t kCrcBytes = 2;
constexpr size_t kMaxPayload = kMaxPacket - kHeaderBytes - kCrcBytes;
constexpr uint8_t kDeckNone = 0xFF;  // sender is not a deck (reserved for a mixer); never elected
constexpr uint8_t kMaxDeckId = 15;

constexpr size_t kMaxPeers = 3;
constexpr uint32_t kPeerTimeoutUs = 1500000;

constexpr size_t kMaxLatched = 16;         // last-value-wins controller slots
constexpr size_t kMaxEventsPerPacket = 8;  // reliable MIDI events per packet

// Serial-number arithmetic: `a` is strictly newer than `b` when a - b is in (0, 32768).
constexpr bool seqNewer(uint16_t a, uint16_t b) {
  const uint16_t d = static_cast<uint16_t>(a - b);
  return d != 0 && d < 0x8000;
}

struct Mac {
  uint8_t b[6] = {0, 0, 0, 0, 0, 0};

  constexpr bool operator==(const Mac& o) const {
    for (size_t i = 0; i < 6; ++i) {
      if (b[i] != o.b[i]) return false;
    }
    return true;
  }
  constexpr bool operator!=(const Mac& o) const { return !(*this == o); }
  // Lexicographic; the election tie-break.
  constexpr bool operator<(const Mac& o) const {
    for (size_t i = 0; i < 6; ++i) {
      if (b[i] != o.b[i]) return b[i] < o.b[i];
    }
    return false;
  }
  constexpr bool isZero() const {
    for (size_t i = 0; i < 6; ++i) {
      if (b[i] != 0) return false;
    }
    return true;
  }
  // Group bit; also covers ff:ff:ff:ff:ff:ff. Never a valid source address.
  constexpr bool isMulticast() const { return (b[0] & 1u) != 0; }
};

// ---- packet framing -------------------------------------------------------------------------------

enum class Type : uint8_t {
  Hello = 1,
  Heartbeat = 2,
  TimeReq = 3,
  TimeResp = 4,
  Midi = 5,
  Transport = 6,
  Ack = 7,
};

struct Header {
  Type type = Type::Hello;
  uint8_t group = 0;        // rig id: packets of another group are ignored
  uint8_t deck = kDeckNone;
  uint8_t flags = 0;        // sender's proto::peerFlags
  uint16_t epoch = 0;       // changes on every sender boot
  uint16_t seq = 0;         // per-sender, +1 per packet sent, wraps
  Mac mac;                  // sender
  uint8_t length = 0;       // payload bytes; set by decodePacket, ignored by encodePacket
};

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout); crc16("123456789") == 0x29B1.
uint16_t crc16(const uint8_t* data, size_t n);

// Header + payload + CRC into out. Returns the packet size, or 0 if the payload exceeds kMaxPayload, the
// source MAC is zero/multicast, or cap is too small.
size_t encodePacket(const Header& h, const uint8_t* payload, size_t payloadLen, uint8_t* out, size_t cap);

enum class DecodeError : uint8_t {
  None = 0,
  TooShort,
  BadMagic,
  BadVersion,
  BadLength,  // length field disagrees with the packet size
  BadCrc,
  BadType,
  BadSource,  // zero or multicast MAC
};

// Validates framing only; *payload points into `in`. Never reads outside [in, in + len).
DecodeError decodePacket(const uint8_t* in, size_t len, Header* h, const uint8_t** payload);

// ---- payloads -------------------------------------------------------------------------------------
//
// encodePayload returns the byte count (0 if cap is too small); decodePayload is strict: exact length,
// every field in range, so a payload that decodes always re-encodes to the same bytes.

struct Hello {
  proto::LinkBridge bridge = proto::LinkBridge::Auto;
  static constexpr size_t kBytes = 1;
};

struct LatchedValue {
  uint8_t channel = 0;  // 0..15
  uint8_t cc = 0;       // 0..127
  uint8_t value = 0;    // 0..127
};

// Continuous controller state of the sender. Every packet carrying it carries all of it, so any one
// packet that arrives repairs every earlier loss.
struct StateBlock {
  uint8_t jogChannel = 0;
  uint16_t jogCount = 0;  // cumulative signed ticks, mod 2^16
  uint8_t nLatched = 0;
  LatchedValue latched[kMaxLatched];
};

struct MidiEvent {
  uint8_t cable = 0;  // USB-MIDI cable on the sending unit: 0 = its own deck, 2 = host feedback relayed back
  midi::Message msg;  // Note On/Off or Control Change
};

struct EventsBlock {
  Mac dst;
  uint8_t msgId = 0;
  uint8_t count = 0;  // 1..kMaxEventsPerPacket
  MidiEvent ev[kMaxEventsPerPacket];
};

struct MidiPayload {
  bool hasEvents = false;
  EventsBlock events;
  bool hasState = false;
  StateBlock state;
};

struct TimeReq {
  Mac dst;
  uint8_t reqId = 0;
  uint32_t t1 = 0;  // requester clock, modular microseconds
  static constexpr size_t kBytes = 11;
};

struct TimeResp {
  Mac dst;
  uint8_t reqId = 0;
  uint32_t t1 = 0, t2 = 0, t3 = 0;  // t1 echoed; t2/t3 responder clock
  static constexpr size_t kBytes = 19;
};

// "At responder time atUs the transport was `state`, `positionClocks` MIDI clocks (24 PPQN) from the start,
// at `bpmX10`." Enough for a follower to phase-align its own clock generator.
struct TransportMsg {
  proto::TransportState state = proto::TransportState::Stopped;
  uint16_t bpmX10 = 1200;
  uint32_t positionClocks = 0;
  uint32_t atUs = 0;
};

struct TransportPayload {
  Mac dst;
  uint8_t msgId = 0;
  TransportMsg msg;
  static constexpr size_t kBytes = 18;
};

struct Ack {
  Mac dst;
  Type acked = Type::Midi;  // Midi or Transport
  uint8_t msgId = 0;
  static constexpr size_t kBytes = 8;
};

size_t encodePayload(const Hello& v, uint8_t* out, size_t cap);
size_t encodePayload(const StateBlock& v, uint8_t* out, size_t cap);  // the HEARTBEAT payload
size_t encodePayload(const MidiPayload& v, uint8_t* out, size_t cap);
size_t encodePayload(const TimeReq& v, uint8_t* out, size_t cap);
size_t encodePayload(const TimeResp& v, uint8_t* out, size_t cap);
size_t encodePayload(const TransportPayload& v, uint8_t* out, size_t cap);
size_t encodePayload(const Ack& v, uint8_t* out, size_t cap);

bool decodePayload(const uint8_t* in, size_t len, Hello* v);
bool decodePayload(const uint8_t* in, size_t len, StateBlock* v);
bool decodePayload(const uint8_t* in, size_t len, MidiPayload* v);
bool decodePayload(const uint8_t* in, size_t len, TimeReq* v);
bool decodePayload(const uint8_t* in, size_t len, TimeResp* v);
bool decodePayload(const uint8_t* in, size_t len, TransportPayload* v);
bool decodePayload(const uint8_t* in, size_t len, Ack* v);

// ---- bridge decision ------------------------------------------------------------------------------

struct BridgeEnd {
  bool usbHost = false;  // a USB host has enumerated this unit
  proto::LinkBridge mode = proto::LinkBridge::Auto;
};

// Does `src` forward its cable-0 MIDI to `dst`, which emits it on its USB cable 2? linkBridge is read from
// the receiving unit ("forward the peer deck's MIDI over THIS unit's USB"): OFF on either end disables the
// route, FORCE on the receiver enables it regardless of host presence, AUTO needs a host on the receiver
// and none on the sender. The same pure function runs on both units, so they always agree.
constexpr bool bridgeRoute(const BridgeEnd& src, const BridgeEnd& dst) {
  if (src.mode == proto::LinkBridge::Off || dst.mode == proto::LinkBridge::Off) return false;
  if (dst.mode == proto::LinkBridge::Force) return true;
  return dst.usbHost && !src.usbHost;
}

// ---- peer table -----------------------------------------------------------------------------------

struct PeerEntry {
  bool used = false;
  Mac mac;
  uint8_t deck = kDeckNone;
  uint8_t flags = 0;  // proto::peerFlags as announced by the peer
  proto::LinkBridge bridge = proto::LinkBridge::Auto;  // from its HELLO; Auto until one arrives
  uint16_t epoch = 0;
  int8_t rssi = 0;
  uint64_t lastHeardUs = 0;

  bool usbHost() const { return (flags & proto::peerFlags::kUsbHost) != 0; }
  bool isDeck() const { return deck <= kMaxDeckId; }
};

// Who is on the air. Any valid packet from a MAC keeps its entry alive. Master election: lowest deck
// id, ties (a duplicate-deck conflict) broken by lowest MAC, so every unit that sees the same set picks
// the same master even when two units claim the same deck.
class PeerTable {
 public:
  enum class Heard : uint8_t {
    Self,       // our own MAC: ignored
    TableFull,  // unknown MAC and no free slot: ignored
    Refreshed,
    Added,
    Restarted,  // known MAC with a new epoch: the peer rebooted
  };

  void setSelf(const Mac& mac, uint8_t deck) {
    selfMac_ = mac;
    selfDeck_ = deck;
  }
  const Mac& selfMac() const { return selfMac_; }
  uint8_t selfDeck() const { return selfDeck_; }

  Heard onHeard(const Header& h, int8_t rssi, uint64_t nowUs);
  void setBridge(const Mac& mac, proto::LinkBridge bridge);
  // Drops peers silent for >= kPeerTimeoutUs; returns how many.
  size_t expire(uint64_t nowUs);
  void clear();

  size_t count() const;
  const PeerEntry& slot(size_t i) const { return peers_[i]; }
  const PeerEntry* find(const Mac& mac) const;

  bool selfIsMaster() const;
  // The elected master when it is a peer; nullptr when this unit is the master.
  const PeerEntry* master() const;
  // The peer that data flows (time sync, bridging, transport) go to: the live deck peer with the lowest
  // (deck, MAC). nullptr when alone.
  const PeerEntry* partner() const;

  // True if a live peer claims our deck id, or two live peers claim the same one.
  bool deckConflict() const;
  bool inConflict(const PeerEntry& p) const;

  uint32_t tableFullCount() const { return tableFull_; }

 private:
  PeerEntry* findMut(const Mac& mac);

  Mac selfMac_;
  uint8_t selfDeck_ = kDeckNone;
  PeerEntry peers_[kMaxPeers];
  uint32_t tableFull_ = 0;
};

// ---- time sync ------------------------------------------------------------------------------------
//
// Estimates the peer's clock as a function of ours from NTP-style 4-timestamp exchanges:
//   t1 local tx of the request, t2 peer rx, t3 peer tx of the response, t4 local rx.
// All times are modular 32-bit microseconds (the offset may exceed 2^31: only differences are formed).
//
//  * RTT = (t4 - t1) - (t3 - t2); the offset sample (t2 - t1 + t3 - t4) / 2 is exact only when both
//    directions took equally long and is off by at most half the round trip's excess over the best one.
//    So every sample of the last kWindow exchanges is weighted by 1 / (excess/2 + 20 us)^2: a sample
//    that rode a radio retry (milliseconds) counts for nothing, the fastest ones carry the fit. This is
//    the outlier rejection.
//  * Weighted least squares through (time, offset) gives offset AND skew (the two crystals differ by up
//    to +/-100 ppm each, 200 us per second, so a plain offset filter would lag), with the slope shrunk
//    toward 0 by a +/-100 ppm prior so a few samples close together cannot invent a large skew.
//  * Slewing: when a refit moves the estimate, the difference is bled out at kMaxSlew instead of
//    stepping, so clocks derived from it never jump. A discrepancy above kStepUs is stepped at once.
//  * Three consecutive fast samples disagreeing with the model by more than kStepUs mean the peer's
//    clock stepped: the estimator restarts.
//
// Accuracy is limited by the radio, not the algorithm: the residual error is roughly a third of the
// mean one-way delay jitter (docs/LINK.md section 7 has the measured figures).
class TimeSync {
 public:
  static constexpr size_t kWindow = 64;  // 16 s of exchanges at the steady-state pace
  static constexpr uint32_t kMaxRttUs = 50000;
  static constexpr uint32_t kStepUs = 2000;
  static constexpr uint8_t kStepConfirm = 3;
  static constexpr size_t kMinFitPoints = 3;
  static constexpr uint32_t kWeightFloorUs = 20;   // timestamping noise floor in the weight
  static constexpr uint32_t kGoodExcessUs = 250;   // excess RTT up to this counts as a good sample
  static constexpr float kSlopePrior = 1e14f;      // ridge on the slope: prior sigma 100 ppm in the weight units
  static constexpr uint32_t kMinSlopeSpanUs = 1000000;
  static constexpr float kMaxSkew = 300e-6f;
  static constexpr float kMaxSlew = 500e-6f;

  enum class Result : uint8_t {
    Used,     // round trip within kGoodExcessUs of the best: carries real weight in the fit
    Slow,     // kept in the window, but its weight is negligible next to the fast ones
    Outlier,  // disagrees with a confirmed model; held back pending confirmation
    Invalid,  // impossible timestamps or RTT out of range
  };

  void reset();
  Result addExchange(uint32_t t1, uint32_t t2, uint32_t t3, uint32_t t4);

  bool valid() const { return valid_; }
  // Offset and skew both estimated (>= 3 exchanges spanning >= 1 s). With the Link's pacing the error is
  // within the bounds of docs/LINK.md section 7 from about 5 s after the first exchange.
  bool locked() const { return locked_; }
  size_t exchanges() const { return total_; }

  uint32_t peerTime(uint32_t localUs) const { return localUs + offset32(localUs); }
  uint32_t localTime(uint32_t peerUs) const;
  // Peer clock minus ours at local time localUs.
  int32_t offsetUs(uint32_t localUs) const { return static_cast<int32_t>(offset32(localUs)); }
  float skewPpm() const { return skew_ * 1e6f; }
  // Fastest round trip in the window.
  uint16_t rttUs() const { return rtt_; }

 private:
  struct Sample {
    uint32_t tMid = 0;   // local time at the middle of the exchange
    uint32_t theta = 0;  // peer - local at tMid
    uint16_t rtt = 0;
  };

  uint32_t modelOffset32(uint32_t t) const;
  uint32_t offset32(uint32_t t) const;
  int32_t correction(uint32_t t) const;
  void refit();
  const Sample& chrono(size_t i) const { return win_[(head_ + kWindow - count_ + i) % kWindow]; }

  Sample win_[kWindow];
  size_t head_ = 0, count_ = 0;
  size_t total_ = 0;

  bool valid_ = false, locked_ = false;
  uint32_t tRef_ = 0, base_ = 0;
  float skew_ = 0.0f;
  uint16_t rtt_ = 0;
  uint8_t offCount_ = 0;

  float corr0_ = 0.0f;  // slew remainder at corrT_
  uint32_t corrT_ = 0;
};

// ---- reliable delivery bookkeeping ----------------------------------------------------------------

// Sender side of stop-and-wait: one message id in flight, exponential backoff, bounded attempts.
class RetryTimer {
 public:
  static constexpr uint32_t kFirstRtoUs = 6000;
  static constexpr uint32_t kMaxRtoUs = 40000;
  static constexpr uint8_t kMaxAttempts = 12;  // gives up ~400 ms after the first transmission
  enum class Poll : uint8_t { Idle, Wait, Resend, GiveUp };

  // The first transmission happens now.
  void begin(uint8_t id, uint64_t nowUs);
  // True if `id` is the message in flight (which is then complete).
  bool ack(uint8_t id);
  Poll poll(uint64_t nowUs);
  void cancel() { active_ = false; }
  bool active() const { return active_; }
  uint8_t id() const { return id_; }

 private:
  bool active_ = false;
  uint8_t id_ = 0;
  uint8_t attempts_ = 0;
  uint64_t nextUs_ = 0;
};

// Receiver side: applies each message id once. ids are 8-bit serial numbers; the sender may skip ids
// (it gave up on one), so anything newer than the last applied id is new.
class MsgIdWindow {
 public:
  enum class Verdict : uint8_t { New, Duplicate };
  Verdict check(uint8_t id) {
    if (have_) {
      const uint8_t d = static_cast<uint8_t>(id - last_);
      if (d == 0 || d >= 128) return Verdict::Duplicate;
    }
    have_ = true;
    last_ = id;
    return Verdict::New;
  }
  void reset() { have_ = false; }

 private:
  bool have_ = false;
  uint8_t last_ = 0;
};

// ---- MIDI forwarding ------------------------------------------------------------------------------

// Sender half. Turns a stream of MIDI messages into packet payloads without ever losing motion:
//
//  * jog (cable 0, CC 22 relative) is folded into a cumulative 16-bit counter; the receiver emits the
//    difference between consecutive counter values it sees, so lost, duplicated and reordered packets
//    can neither drop nor double-count a tick;
//  * every other cable-0 CC except bpmNudge is last-value-wins per (channel, cc) and travels as a full
//    snapshot, so a lost packet is repaired by the next one;
//  * notes, bpmNudge and everything on cable 2 are events: queued, batched, sent stop-and-wait with
//    retries and applied exactly once, in order, by the receiver;
//  * everything else (system messages, program change...) is refused.
//
// One jog counter per unit: the channel of the most recent jog message is the one the receiver emits on.
class MidiForwarder {
 public:
  static constexpr size_t kQueueCap = 64;
  static constexpr uint32_t kMinIntervalUs = 2000;  // at most 500 MIDI packets per second

  // False if this kind of message is not forwarded. Never blocks; a full event queue drops the newest.
  bool submit(uint8_t cable, const midi::Message& m);

  // Payload of a MIDI packet if one is due (new state, a new or retried event batch), else 0.
  size_t buildMidi(uint64_t nowUs, const Mac& dst, uint8_t* out, size_t cap);
  // Payload of a HEARTBEAT: the full state block.
  size_t buildHeartbeat(uint8_t* out, size_t cap) const;

  // An ACK for the batch in flight. True if it was.
  bool onAck(uint8_t msgId);

  void markStateDirty() { stateDirty_ = true; }
  // Forget queued and in-flight events (route down, peer gone). The jog counter keeps counting.
  void clearEvents();

  uint16_t jogCount() const { return jog_; }
  uint32_t droppedEvents() const { return dropped_; }    // queue full
  uint32_t abandonedEvents() const { return abandoned_; }  // retries exhausted
  uint32_t retriedBatches() const { return retried_; }

 private:
  struct Slot {
    bool used = false;
    uint8_t channel = 0, cc = 0, value = 0;
  };
  bool latch(uint8_t channel, uint8_t cc, uint8_t value);
  bool enqueue(uint8_t cable, const midi::Message& m);
  void snapshot(StateBlock* s) const;

  uint16_t jog_ = 0;
  uint8_t jogChannel_ = 0;
  Slot slots_[kMaxLatched];
  bool stateDirty_ = false;

  MidiEvent queue_[kQueueCap];
  size_t qHead_ = 0, qCount_ = 0;
  MidiEvent inFlight_[kMaxEventsPerPacket];
  uint8_t inFlightCount_ = 0;
  uint8_t nextId_ = 0;
  RetryTimer retry_;

  uint64_t lastSendUs_ = 0;
  bool sentOnce_ = false;
  uint32_t dropped_ = 0, abandoned_ = 0, retried_ = 0;
};

// Receiver half. State blocks older than the newest one seen are ignored, so a reordered or duplicated
// packet changes nothing; the jog counter difference is exact across any pattern of loss.
class MidiReceiver {
 public:
  struct StateDelta {
    int32_t jogTicks = 0;
    uint8_t jogChannel = 0;
    uint8_t ccCount = 0;
    midi::Message cc[kMaxLatched];  // latched values that changed, in packet order
  };

  // False if `seq` is not newer than the last applied state (stale, duplicate). The first state after
  // reset() adopts the jog counter without producing ticks.
  bool applyState(uint16_t seq, const StateBlock& s, StateDelta* out);
  MsgIdWindow::Verdict acceptEvents(uint8_t msgId) { return events_.check(msgId); }

  // Held-note tracking, so a lost link cannot leave a note stuck on at the host.
  void noteHeld(const midi::Message& m);
  size_t releaseHeld(midi::Message* out, size_t cap);  // Note Offs for every held note; clears the set

  void forgetState();  // next applyState adopts again
  void reset();        // everything, held notes included (call releaseHeld first)

 private:
  struct Latched {
    bool used = false;
    uint8_t channel = 0, cc = 0, value = 0;
  };
  struct Held {
    bool used = false;
    uint8_t channel = 0, note = 0;
  };

  bool haveSeq_ = false;
  uint16_t lastSeq_ = 0;
  bool haveJog_ = false;
  uint16_t lastJog_ = 0;
  Latched latched_[kMaxLatched];
  Held held_[kMaxLatched];
  MsgIdWindow events_;
};

// ---- the link -------------------------------------------------------------------------------------

class LinkTransport {
 public:
  // Broadcasts one packet of at most kMaxPacket bytes. False if the radio refused it; the link then
  // treats the packet as lost on the air, which every layer above already tolerates.
  virtual bool send(const uint8_t* data, size_t len) = 0;

 protected:
  ~LinkTransport() = default;
};

struct TransportEvent {
  TransportMsg msg;        // atUs is in the sender's clock
  bool fromMaster = false;
  bool atLocalValid = false;
  uint32_t atLocalUs = 0;  // msg.atUs in our clock, when time sync has an estimate
};

class LinkSink {
 public:
  // Deck MIDI from the peer that this unit must put on its USB cable 2 (jog, controls, notes; channel as
  // the peer produced it).
  virtual void onBridgedMidi(const midi::Message& m) = 0;
  // Host feedback (notes/CCs the host sent to the bridge's cable 2), relayed back: handle it exactly as
  // if it had arrived on this unit's own cable 0.
  virtual void onFeedbackMidi(const midi::Message& m) = 0;
  virtual void onTransport(const TransportEvent& e) = 0;

 protected:
  ~LinkSink() = default;
};

class Link {
 public:
  static constexpr uint32_t kHelloPeriodUs = 500000;
  static constexpr uint32_t kHeartbeatPeriodUs = 100000;
  static constexpr uint32_t kTimeReqFastUs = 100000;  // until kFastExchanges answers have arrived
  static constexpr uint32_t kTimeReqSlowUs = 250000;
  static constexpr size_t kFastExchanges = 32;

  struct Config {
    Mac mac;                // efuse MAC
    uint8_t deck = 0;       // proto::Deck
    uint8_t group = 0;      // rig id; units with different groups ignore each other
    uint16_t epoch = 1;     // random per boot
  };

  struct Stats {
    uint32_t txPackets = 0, txFailed = 0;
    uint32_t rxPackets = 0;    // valid, ours, accepted
    uint32_t rxCorrupt = 0;    // failed framing/CRC/version
    uint32_t rxForeign = 0;    // other group, or our own MAC
    uint32_t rxMalformed = 0;  // framed fine, payload invalid
    uint32_t rxStaleState = 0; // reordered/duplicate state ignored
    uint32_t rxTableFull = 0;
    uint32_t transportGiveUps = 0;  // retry budgets of TRANSPORT used up (it then starts over until the peer is lost)
  };

  Link(LinkTransport& tx, LinkSink& sink) : tx_(tx), sink_(sink) {}

  void configure(const Config& c);
  void setUsbHost(bool mounted);
  void setBridgeMode(proto::LinkBridge mode);

  // A packet came off the air. rxUs is the radio's receive timestamp (time sync uses it), nowUs the
  // current time; replies are stamped with nowUs.
  void onPacket(const uint8_t* data, size_t len, int8_t rssi, uint64_t rxUs, uint64_t nowUs);
  // Call at least every 2 ms (1 ms is better): sends whatever is due and drains received jog.
  void poll(uint64_t nowUs);

  // Engine -> link. forwardMidi takes this unit's own cable-0 output; false (and nothing queued) if no
  // route is active or the message kind is not forwarded.
  bool forwardMidi(const midi::Message& m);
  // Bridge receiver: a Note/CC the host sent on cable 2, to be relayed to the unit it is bridging.
  bool relayHostMidi(const midi::Message& m);
  // Reliable; a newer call supersedes one still unacknowledged. msg.atUs is our clock.
  bool sendTransport(const TransportMsg& msg, uint64_t nowUs);

  bool linkUp() const { return peers_.count() > 0; }
  bool routeOut() const { return routeOut_; }  // we forward our MIDI (peerFlags::kForwarding)
  bool routeIn() const { return routeIn_; }    // we emit the peer's MIDI on cable 2
  bool bridging() const { return routeOut_ || routeIn_; }  // stateFlags::kBridging
  bool selfIsMaster() const { return peers_.selfIsMaster(); }
  bool deckConflict() const { return peers_.deckConflict(); }
  uint8_t headerFlags() const;

  // Converts between the master's clock and ours (identity when we are the master). False without an
  // estimate.
  bool masterTimeToLocal(uint32_t masterUs, uint32_t* localUs) const;
  bool localTimeToMaster(uint32_t localUs, uint32_t* masterUs) const;

  // LINK_STATUS reply entries. flags = USB_HOST/FORWARDING as announced + IS_MASTER from our election;
  // reserved bit 0 = deck-id conflict (kPeerConflict); rttUs/offsetUs describe the partner only.
  static constexpr uint8_t kPeerConflict = 1;
  size_t fillPeers(proto::Peer* out, size_t cap, uint64_t nowUs) const;

  const PeerTable& peers() const { return peers_; }
  const TimeSync& timeSync() const { return sync_; }
  const MidiForwarder& forwarder() const { return fwd_; }
  const Stats& stats() const { return stats_; }

 private:
  bool sendPacket(Type type, const uint8_t* payload, size_t len);
  void sendHello();
  void sendAck(const Mac& to, Type acked, uint8_t msgId);
  void sendTransportPacket();
  void updatePartner(uint64_t nowUs, const Mac* restarted);
  void refreshRoutes();
  void onPartnerUp(uint64_t nowUs);
  void onPartnerLost();
  void releaseHeldNotes();
  void handleState(uint16_t seq, const StateBlock& s);
  void handleMidi(const Header& h, const uint8_t* p, size_t n);
  void handleTransport(const Header& h, const uint8_t* p, size_t n);
  void handleTimeReq(const uint8_t* p, size_t n, uint64_t rxUs, uint64_t nowUs);
  void handleTimeResp(const uint8_t* p, size_t n, uint64_t rxUs);

  LinkTransport& tx_;
  LinkSink& sink_;
  Config cfg_;
  bool usbHost_ = false;
  proto::LinkBridge bridgeMode_ = proto::LinkBridge::Auto;
  PeerTable peers_;
  Stats stats_;
  uint64_t nowUs_ = 0;  // time of the call being processed; stamps outgoing packets
  uint16_t seq_ = 0;

  bool havePartner_ = false;
  Mac partner_;
  bool routeOut_ = false, routeIn_ = false;

  uint64_t nextHelloUs_ = 0, nextHeartbeatUs_ = 0, nextTimeReqUs_ = 0;

  MidiForwarder fwd_;
  MidiReceiver rx_;
  JogEmitter jogOut_;
  uint8_t jogChannel_ = 0;

  TimeSync sync_;
  bool reqOutstanding_ = false;
  uint8_t reqId_ = 0;
  uint32_t reqT1_ = 0;

  TransportMsg tCur_;
  bool tHave_ = false;
  uint8_t tNextId_ = 0;
  RetryTimer tRetry_;
  MsgIdWindow tRx_;
};

}  // namespace link
}  // namespace epdj
