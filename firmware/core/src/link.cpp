#include "epdj/link.h"

#include <cstring>

namespace epdj {
namespace link {

namespace {

inline void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFu);
  p[1] = static_cast<uint8_t>(v >> 8);
}
inline void put32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFu);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
  p[3] = static_cast<uint8_t>(v >> 24);
}
inline uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
inline void putMac(uint8_t* p, const Mac& m) { std::memcpy(p, m.b, 6); }
inline Mac getMac(const uint8_t* p) {
  Mac m;
  std::memcpy(m.b, p, 6);
  return m;
}

inline int32_t roundToI32(float v) { return static_cast<int32_t>(v + (v >= 0.0f ? 0.5f : -0.5f)); }
inline float absf(float v) { return v < 0.0f ? -v : v; }

}  // namespace

// ---- framing ------------------------------------------------------------------------------------

uint16_t crc16(const uint8_t* data, size_t n) {
  static const uint16_t kNibble[16] = {0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
                                       0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF};
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    crc = static_cast<uint16_t>((crc << 4) ^ kNibble[(crc >> 12) ^ (data[i] >> 4)]);
    crc = static_cast<uint16_t>((crc << 4) ^ kNibble[(crc >> 12) ^ (data[i] & 0x0Fu)]);
  }
  return crc;
}

size_t encodePacket(const Header& h, const uint8_t* payload, size_t payloadLen, uint8_t* out, size_t cap) {
  if (payloadLen > kMaxPayload || (payloadLen > 0 && payload == nullptr)) return 0;
  if (h.mac.isZero() || h.mac.isMulticast()) return 0;
  const size_t total = kHeaderBytes + payloadLen + kCrcBytes;
  if (cap < total) return 0;
  out[0] = kMagic0;
  out[1] = kMagic1;
  out[2] = kVersion;
  out[3] = static_cast<uint8_t>(h.type);
  out[4] = h.group;
  out[5] = h.deck;
  out[6] = h.flags;
  put16(out + 7, h.epoch);
  put16(out + 9, h.seq);
  putMac(out + 11, h.mac);
  out[17] = static_cast<uint8_t>(payloadLen);
  if (payloadLen > 0) std::memcpy(out + kHeaderBytes, payload, payloadLen);
  put16(out + kHeaderBytes + payloadLen, crc16(out, kHeaderBytes + payloadLen));
  return total;
}

DecodeError decodePacket(const uint8_t* in, size_t len, Header* h, const uint8_t** payload) {
  if (len < kHeaderBytes + kCrcBytes) return DecodeError::TooShort;
  if (in[0] != kMagic0 || in[1] != kMagic1) return DecodeError::BadMagic;
  if (in[2] != kVersion) return DecodeError::BadVersion;
  const size_t plen = in[17];
  if (len != kHeaderBytes + plen + kCrcBytes) return DecodeError::BadLength;
  if (get16(in + kHeaderBytes + plen) != crc16(in, kHeaderBytes + plen)) return DecodeError::BadCrc;
  if (in[3] < static_cast<uint8_t>(Type::Hello) || in[3] > static_cast<uint8_t>(Type::Ack)) {
    return DecodeError::BadType;
  }
  const Mac mac = getMac(in + 11);
  if (mac.isZero() || mac.isMulticast()) return DecodeError::BadSource;
  h->type = static_cast<Type>(in[3]);
  h->group = in[4];
  h->deck = in[5];
  h->flags = in[6];
  h->epoch = get16(in + 7);
  h->seq = get16(in + 9);
  h->mac = mac;
  h->length = static_cast<uint8_t>(plen);
  *payload = in + kHeaderBytes;
  return DecodeError::None;
}

// ---- payloads -----------------------------------------------------------------------------------

namespace {

constexpr size_t kEventBytes = 4;
constexpr size_t kLatchedBytes = 3;
constexpr size_t kStateFixedBytes = 4;
constexpr uint8_t kFlagEvents = 0x01;
constexpr uint8_t kFlagState = 0x02;

size_t stateBytes(const StateBlock& s) { return kStateFixedBytes + kLatchedBytes * s.nLatched; }

size_t putState(const StateBlock& s, uint8_t* out) {
  out[0] = s.jogChannel;
  put16(out + 1, s.jogCount);
  out[3] = s.nLatched;
  for (size_t i = 0; i < s.nLatched; ++i) {
    out[kStateFixedBytes + i * kLatchedBytes + 0] = s.latched[i].channel;
    out[kStateFixedBytes + i * kLatchedBytes + 1] = s.latched[i].cc;
    out[kStateFixedBytes + i * kLatchedBytes + 2] = s.latched[i].value;
  }
  return stateBytes(s);
}

// Exact-length parse of one state block; *used = bytes it occupies.
bool getState(const uint8_t* in, size_t len, StateBlock* s, size_t* used) {
  if (len < kStateFixedBytes) return false;
  const size_t n = in[3];
  if (n > kMaxLatched || len < kStateFixedBytes + kLatchedBytes * n) return false;
  if (in[0] > 15) return false;
  StateBlock v;
  v.jogChannel = in[0];
  v.jogCount = get16(in + 1);
  v.nLatched = static_cast<uint8_t>(n);
  for (size_t i = 0; i < n; ++i) {
    const uint8_t* e = in + kStateFixedBytes + i * kLatchedBytes;
    if (e[0] > 15 || e[1] > 127 || e[2] > 127) return false;
    v.latched[i].channel = e[0];
    v.latched[i].cc = e[1];
    v.latched[i].value = e[2];
  }
  *s = v;
  *used = kStateFixedBytes + kLatchedBytes * n;
  return true;
}

bool validEventStatus(uint8_t status) {
  const uint8_t t = static_cast<uint8_t>(status & 0xF0u);
  return t == midi::kNoteOff || t == midi::kNoteOn || t == midi::kControlChange;
}

}  // namespace

size_t encodePayload(const Hello& v, uint8_t* out, size_t cap) {
  if (cap < Hello::kBytes) return 0;
  out[0] = static_cast<uint8_t>(v.bridge);
  return Hello::kBytes;
}

bool decodePayload(const uint8_t* in, size_t len, Hello* v) {
  if (len != Hello::kBytes || in[0] > static_cast<uint8_t>(proto::LinkBridge::Force)) return false;
  v->bridge = static_cast<proto::LinkBridge>(in[0]);
  return true;
}

size_t encodePayload(const StateBlock& v, uint8_t* out, size_t cap) {
  if (v.nLatched > kMaxLatched || cap < stateBytes(v)) return 0;
  return putState(v, out);
}

bool decodePayload(const uint8_t* in, size_t len, StateBlock* v) {
  size_t used = 0;
  return getState(in, len, v, &used) && used == len;
}

size_t encodePayload(const MidiPayload& v, uint8_t* out, size_t cap) {
  if (!v.hasEvents && !v.hasState) return 0;
  if (v.hasEvents && (v.events.count == 0 || v.events.count > kMaxEventsPerPacket)) return 0;
  if (v.hasState && v.state.nLatched > kMaxLatched) return 0;
  size_t need = 1;
  if (v.hasEvents) need += 6 + 2 + kEventBytes * v.events.count;
  if (v.hasState) need += stateBytes(v.state);
  if (cap < need) return 0;
  size_t o = 0;
  out[o++] = static_cast<uint8_t>((v.hasEvents ? kFlagEvents : 0) | (v.hasState ? kFlagState : 0));
  if (v.hasEvents) {
    putMac(out + o, v.events.dst);
    o += 6;
    out[o++] = v.events.msgId;
    out[o++] = v.events.count;
    for (size_t i = 0; i < v.events.count; ++i) {
      const MidiEvent& e = v.events.ev[i];
      out[o++] = e.cable;
      out[o++] = e.msg.b[0];
      out[o++] = e.msg.b[1];
      out[o++] = e.msg.b[2];
    }
  }
  if (v.hasState) o += putState(v.state, out + o);
  return o;
}

bool decodePayload(const uint8_t* in, size_t len, MidiPayload* v) {
  if (len < 1) return false;
  const uint8_t flags = in[0];
  if ((flags & ~(kFlagEvents | kFlagState)) != 0 || flags == 0) return false;
  MidiPayload p;
  size_t o = 1;
  if (flags & kFlagEvents) {
    if (len < o + 8) return false;
    p.hasEvents = true;
    p.events.dst = getMac(in + o);
    o += 6;
    p.events.msgId = in[o++];
    p.events.count = in[o++];
    if (p.events.count == 0 || p.events.count > kMaxEventsPerPacket) return false;
    if (len < o + kEventBytes * p.events.count) return false;
    for (size_t i = 0; i < p.events.count; ++i) {
      MidiEvent& e = p.events.ev[i];
      e.cable = in[o];
      const uint8_t st = in[o + 1], d1 = in[o + 2], d2 = in[o + 3];
      o += kEventBytes;
      if (e.cable > 15 || !validEventStatus(st) || d1 > 127 || d2 > 127) return false;
      e.msg = midi::make(st, d1, d2, 3);
    }
  }
  if (flags & kFlagState) {
    size_t used = 0;
    p.hasState = true;
    if (!getState(in + o, len - o, &p.state, &used)) return false;
    o += used;
  }
  if (o != len) return false;
  *v = p;
  return true;
}

size_t encodePayload(const TimeReq& v, uint8_t* out, size_t cap) {
  if (cap < TimeReq::kBytes) return 0;
  putMac(out, v.dst);
  out[6] = v.reqId;
  put32(out + 7, v.t1);
  return TimeReq::kBytes;
}

bool decodePayload(const uint8_t* in, size_t len, TimeReq* v) {
  if (len != TimeReq::kBytes) return false;
  v->dst = getMac(in);
  v->reqId = in[6];
  v->t1 = get32(in + 7);
  return true;
}

size_t encodePayload(const TimeResp& v, uint8_t* out, size_t cap) {
  if (cap < TimeResp::kBytes) return 0;
  putMac(out, v.dst);
  out[6] = v.reqId;
  put32(out + 7, v.t1);
  put32(out + 11, v.t2);
  put32(out + 15, v.t3);
  return TimeResp::kBytes;
}

bool decodePayload(const uint8_t* in, size_t len, TimeResp* v) {
  if (len != TimeResp::kBytes) return false;
  v->dst = getMac(in);
  v->reqId = in[6];
  v->t1 = get32(in + 7);
  v->t2 = get32(in + 11);
  v->t3 = get32(in + 15);
  return true;
}

size_t encodePayload(const TransportPayload& v, uint8_t* out, size_t cap) {
  if (cap < TransportPayload::kBytes) return 0;
  putMac(out, v.dst);
  out[6] = v.msgId;
  out[7] = static_cast<uint8_t>(v.msg.state);
  put16(out + 8, v.msg.bpmX10);
  put32(out + 10, v.msg.positionClocks);
  put32(out + 14, v.msg.atUs);
  return TransportPayload::kBytes;
}

bool decodePayload(const uint8_t* in, size_t len, TransportPayload* v) {
  if (len != TransportPayload::kBytes) return false;
  const uint16_t bpm = get16(in + 8);
  if (in[7] > static_cast<uint8_t>(proto::TransportState::Paused)) return false;
  if (bpm < proto::kBpmMinX10 || bpm > proto::kBpmMaxX10) return false;
  v->dst = getMac(in);
  v->msgId = in[6];
  v->msg.state = static_cast<proto::TransportState>(in[7]);
  v->msg.bpmX10 = bpm;
  v->msg.positionClocks = get32(in + 10);
  v->msg.atUs = get32(in + 14);
  return true;
}

size_t encodePayload(const Ack& v, uint8_t* out, size_t cap) {
  if (cap < Ack::kBytes) return 0;
  putMac(out, v.dst);
  out[6] = static_cast<uint8_t>(v.acked);
  out[7] = v.msgId;
  return Ack::kBytes;
}

bool decodePayload(const uint8_t* in, size_t len, Ack* v) {
  if (len != Ack::kBytes) return false;
  if (in[6] != static_cast<uint8_t>(Type::Midi) && in[6] != static_cast<uint8_t>(Type::Transport)) return false;
  v->dst = getMac(in);
  v->acked = static_cast<Type>(in[6]);
  v->msgId = in[7];
  return true;
}

// ---- PeerTable ----------------------------------------------------------------------------------

namespace {
// Election order: lower deck id first, then lower MAC.
bool keyLess(uint8_t deckA, const Mac& macA, uint8_t deckB, const Mac& macB) {
  if (deckA != deckB) return deckA < deckB;
  return macA < macB;
}
}  // namespace

PeerEntry* PeerTable::findMut(const Mac& mac) {
  for (size_t i = 0; i < kMaxPeers; ++i) {
    if (peers_[i].used && peers_[i].mac == mac) return &peers_[i];
  }
  return nullptr;
}

const PeerEntry* PeerTable::find(const Mac& mac) const {
  for (size_t i = 0; i < kMaxPeers; ++i) {
    if (peers_[i].used && peers_[i].mac == mac) return &peers_[i];
  }
  return nullptr;
}

PeerTable::Heard PeerTable::onHeard(const Header& h, int8_t rssi, uint64_t nowUs) {
  if (h.mac == selfMac_) return Heard::Self;
  PeerEntry* e = findMut(h.mac);
  Heard result = Heard::Refreshed;
  if (e == nullptr) {
    for (size_t i = 0; i < kMaxPeers && e == nullptr; ++i) {
      if (!peers_[i].used) e = &peers_[i];
    }
    if (e == nullptr) {
      ++tableFull_;
      return Heard::TableFull;
    }
    *e = PeerEntry();
    e->used = true;
    e->mac = h.mac;
    result = Heard::Added;
  } else if (e->epoch != h.epoch) {
    e->bridge = proto::LinkBridge::Auto;  // announced again in the new boot's HELLO
    result = Heard::Restarted;
  }
  e->deck = h.deck;
  e->flags = h.flags;
  e->epoch = h.epoch;
  e->rssi = rssi;
  e->lastHeardUs = nowUs;
  return result;
}

void PeerTable::setBridge(const Mac& mac, proto::LinkBridge bridge) {
  PeerEntry* e = findMut(mac);
  if (e != nullptr) e->bridge = bridge;
}

size_t PeerTable::expire(uint64_t nowUs) {
  size_t removed = 0;
  for (size_t i = 0; i < kMaxPeers; ++i) {
    PeerEntry& e = peers_[i];
    if (e.used && nowUs >= e.lastHeardUs && nowUs - e.lastHeardUs >= kPeerTimeoutUs) {
      e.used = false;
      ++removed;
    }
  }
  return removed;
}

void PeerTable::clear() {
  for (size_t i = 0; i < kMaxPeers; ++i) peers_[i] = PeerEntry();
}

size_t PeerTable::count() const {
  size_t n = 0;
  for (size_t i = 0; i < kMaxPeers; ++i) n += peers_[i].used ? 1u : 0u;
  return n;
}

const PeerEntry* PeerTable::partner() const {
  const PeerEntry* best = nullptr;
  for (size_t i = 0; i < kMaxPeers; ++i) {
    const PeerEntry& e = peers_[i];
    if (!e.used || !e.isDeck()) continue;
    if (best == nullptr || keyLess(e.deck, e.mac, best->deck, best->mac)) best = &e;
  }
  return best;
}

bool PeerTable::selfIsMaster() const {
  if (selfDeck_ > kMaxDeckId) return false;
  const PeerEntry* p = partner();
  return p == nullptr || keyLess(selfDeck_, selfMac_, p->deck, p->mac);
}

const PeerEntry* PeerTable::master() const { return selfIsMaster() ? nullptr : partner(); }

bool PeerTable::deckConflict() const {
  for (size_t i = 0; i < kMaxPeers; ++i) {
    if (peers_[i].used && inConflict(peers_[i])) return true;
  }
  return false;
}

bool PeerTable::inConflict(const PeerEntry& p) const {
  if (!p.isDeck()) return false;
  if (p.deck == selfDeck_) return true;
  for (size_t i = 0; i < kMaxPeers; ++i) {
    const PeerEntry& o = peers_[i];
    if (o.used && o.mac != p.mac && o.deck == p.deck) return true;
  }
  return false;
}

// ---- TimeSync -----------------------------------------------------------------------------------

void TimeSync::reset() { *this = TimeSync(); }

uint32_t TimeSync::modelOffset32(uint32_t t) const {
  const int32_t dt = static_cast<int32_t>(t - tRef_);
  return base_ + static_cast<uint32_t>(roundToI32(skew_ * static_cast<float>(dt)));
}

int32_t TimeSync::correction(uint32_t t) const {
  if (corr0_ == 0.0f) return 0;
  const int32_t elapsed = static_cast<int32_t>(t - corrT_);
  if (elapsed <= 0) return roundToI32(corr0_);
  const float mag = absf(corr0_) - kMaxSlew * static_cast<float>(elapsed);
  if (mag <= 0.0f) return 0;
  return corr0_ > 0.0f ? roundToI32(mag) : -roundToI32(mag);
}

uint32_t TimeSync::offset32(uint32_t t) const {
  if (!valid_) return 0;
  return modelOffset32(t) + static_cast<uint32_t>(correction(t));
}

uint32_t TimeSync::localTime(uint32_t peerUs) const {
  if (!valid_) return peerUs;
  // peer = local + base + skew * (local - tRef)  =>  local - tRef = d / (1 + skew) ~ d - skew * d
  const int32_t d = static_cast<int32_t>(peerUs - (tRef_ + base_));
  const int32_t dl = d - roundToI32(skew_ * static_cast<float>(d));
  const uint32_t local = tRef_ + static_cast<uint32_t>(dl);
  return local - static_cast<uint32_t>(correction(local));
}

TimeSync::Result TimeSync::addExchange(uint32_t t1, uint32_t t2, uint32_t t3, uint32_t t4) {
  const uint32_t dl = t4 - t1;  // local elapsed
  const uint32_t dp = t3 - t2;  // peer elapsed: its processing time
  if (dl >= 0x80000000u || dp > dl) return Result::Invalid;
  const uint32_t rtt = dl - dp;
  if (rtt > kMaxRttUs) return Result::Invalid;

  // (t2 - t1) = theta + d1 and (t3 - t4) = theta - d2, so theta = (t3 - t4) + rtt/2 up to (d1 - d2)/2.
  const uint32_t theta = (t3 - t4) + rtt / 2;
  const uint32_t tMid = t1 + dl / 2;

  if (valid_ && rtt <= static_cast<uint32_t>(rtt_) + kGoodExcessUs) {
    const int32_t innovation = static_cast<int32_t>(theta - modelOffset32(tMid));
    if (innovation > static_cast<int32_t>(kStepUs) || innovation < -static_cast<int32_t>(kStepUs)) {
      if (++offCount_ < kStepConfirm) return Result::Outlier;
      reset();  // the peer's clock stepped: start over from this sample
    } else {
      offCount_ = 0;
    }
  }

  Sample& s = win_[head_];
  s.tMid = tMid;
  s.theta = theta;
  s.rtt = static_cast<uint16_t>(rtt);
  head_ = (head_ + 1) % kWindow;
  if (count_ < kWindow) ++count_;
  ++total_;
  refit();
  return rtt <= static_cast<uint32_t>(rtt_) + kGoodExcessUs ? Result::Used : Result::Slow;
}

void TimeSync::refit() {
  uint16_t best = 0xFFFF;
  for (size_t i = 0; i < count_; ++i) {
    if (chrono(i).rtt < best) best = chrono(i).rtt;
  }
  rtt_ = best;

  // Precision weight of a sample: its error is bounded by half its excess round trip, so w ~ 1/(excess/2 + floor)^2.
  uint32_t w[kWindow];
  const uint32_t tNewest = chrono(count_ - 1).tMid;
  const uint32_t thetaNewest = chrono(count_ - 1).theta;
  int64_t sw = 0, swx = 0, swy = 0;
  int32_t minX = 0;
  for (size_t i = 0; i < count_; ++i) {
    const Sample& s = chrono(i);
    const uint32_t e = (static_cast<uint32_t>(s.rtt - best) >> 1) + kWeightFloorUs;
    const uint32_t wi = (1u << 20) / (e * e);
    w[i] = wi != 0 ? wi : 1u;
    const int64_t x = static_cast<int32_t>(s.tMid - tNewest);
    const int64_t y = static_cast<int32_t>(s.theta - thetaNewest);
    sw += w[i];
    swx += static_cast<int64_t>(w[i]) * x;
    swy += static_cast<int64_t>(w[i]) * y;
    if (x < minX) minX = static_cast<int32_t>(x);
  }
  const int32_t xbar = static_cast<int32_t>(swx / sw);
  const int32_t ybar = static_cast<int32_t>(swy / sw);

  // Weighted least squares about the weighted centroid, slope shrunk toward 0 by a prior of +/-100 ppm so
  // that a handful of samples a fraction of a second apart cannot invent a large skew.
  float sxx = 0.0f, sxy = 0.0f;
  for (size_t i = 0; i < count_; ++i) {
    const Sample& s = chrono(i);
    const float dx = static_cast<float>(static_cast<int32_t>(s.tMid - tNewest) - xbar);
    const float dy = static_cast<float>(static_cast<int32_t>(s.theta - thetaNewest) - ybar);
    const float wi = static_cast<float>(w[i]);
    sxx += wi * dx * dx;
    sxy += wi * dx * dy;
  }
  float slope = sxy / (sxx + kSlopePrior);
  slope = slope > kMaxSkew ? kMaxSkew : (slope < -kMaxSkew ? -kMaxSkew : slope);
  const bool haveSlope = count_ >= kMinFitPoints && -static_cast<int64_t>(minX) >= static_cast<int64_t>(kMinSlopeSpanUs);
  if (!haveSlope) slope = 0.0f;

  const uint32_t tNow = tNewest;
  const bool hadModel = valid_;
  const uint32_t appliedBefore = hadModel ? offset32(tNow) : 0;

  tRef_ = tNewest + static_cast<uint32_t>(xbar);
  base_ = thetaNewest + static_cast<uint32_t>(ybar);
  skew_ = slope;
  valid_ = true;
  locked_ = haveSlope;

  // Bleed the change out at kMaxSlew instead of stepping, unless it is large (initial lock, clock step).
  corr0_ = 0.0f;
  if (hadModel) {
    const int32_t jump = static_cast<int32_t>(appliedBefore - modelOffset32(tNow));
    if (jump <= static_cast<int32_t>(kStepUs) && jump >= -static_cast<int32_t>(kStepUs)) {
      corr0_ = static_cast<float>(jump);
      corrT_ = tNow;
    }
  }
}

// ---- RetryTimer ---------------------------------------------------------------------------------

namespace {
uint32_t rtoFor(uint8_t attempt) {
  uint32_t v = RetryTimer::kFirstRtoUs;
  for (uint8_t i = 1; i < attempt && v < RetryTimer::kMaxRtoUs; ++i) v *= 2;
  return v < RetryTimer::kMaxRtoUs ? v : RetryTimer::kMaxRtoUs;
}
}  // namespace

void RetryTimer::begin(uint8_t id, uint64_t nowUs) {
  active_ = true;
  id_ = id;
  attempts_ = 1;
  nextUs_ = nowUs + rtoFor(1);
}

bool RetryTimer::ack(uint8_t id) {
  if (!active_ || id != id_) return false;
  active_ = false;
  return true;
}

RetryTimer::Poll RetryTimer::poll(uint64_t nowUs) {
  if (!active_) return Poll::Idle;
  if (nowUs < nextUs_) return Poll::Wait;
  if (attempts_ >= kMaxAttempts) {
    active_ = false;
    return Poll::GiveUp;
  }
  ++attempts_;
  nextUs_ = nowUs + rtoFor(attempts_);
  return Poll::Resend;
}

// ---- MidiForwarder ------------------------------------------------------------------------------

bool MidiForwarder::latch(uint8_t channel, uint8_t cc, uint8_t value) {
  Slot* free = nullptr;
  for (size_t i = 0; i < kMaxLatched; ++i) {
    Slot& s = slots_[i];
    if (s.used && s.channel == channel && s.cc == cc) {
      if (s.value != value) {
        s.value = value;
        stateDirty_ = true;
      }
      return true;
    }
    if (!s.used && free == nullptr) free = &s;
  }
  if (free == nullptr) return false;
  free->used = true;
  free->channel = channel;
  free->cc = cc;
  free->value = value;
  stateDirty_ = true;
  return true;
}

bool MidiForwarder::enqueue(uint8_t cable, const midi::Message& m) {
  if (qCount_ == kQueueCap) {
    ++dropped_;
    return false;
  }
  MidiEvent& e = queue_[(qHead_ + qCount_) % kQueueCap];
  e.cable = cable;
  e.msg = m;
  ++qCount_;
  return true;
}

bool MidiForwarder::submit(uint8_t cable, const midi::Message& m) {
  if (m.len != 3 || !m.isChannelVoice()) return false;
  const uint8_t type = m.type();
  if (type == midi::kControlChange && cable == proto::cable::kDeck) {
    const uint8_t cc = m.data1();
    if (cc == proto::cc::kJog) {
      const int32_t d = midi::decodeRelative(m.data2());
      if (d != 0) {
        jog_ = static_cast<uint16_t>(jog_ + static_cast<uint16_t>(d));
        jogChannel_ = m.channel();
        stateDirty_ = true;
      }
      return true;
    }
    if (cc != proto::cc::kBpmNudge && latch(m.channel(), cc, m.data2())) return true;
  }
  if (type == midi::kNoteOn || type == midi::kNoteOff || type == midi::kControlChange) return enqueue(cable, m);
  return false;
}

void MidiForwarder::snapshot(StateBlock* s) const {
  s->jogChannel = jogChannel_;
  s->jogCount = jog_;
  s->nLatched = 0;
  for (size_t i = 0; i < kMaxLatched; ++i) {
    if (!slots_[i].used) continue;
    LatchedValue& v = s->latched[s->nLatched++];
    v.channel = slots_[i].channel;
    v.cc = slots_[i].cc;
    v.value = slots_[i].value;
  }
}

size_t MidiForwarder::buildHeartbeat(uint8_t* out, size_t cap) const {
  StateBlock s;
  snapshot(&s);
  return encodePayload(s, out, cap);
}

void MidiForwarder::clearEvents() {
  qHead_ = 0;
  qCount_ = 0;
  inFlightCount_ = 0;
  retry_.cancel();
}

bool MidiForwarder::onAck(uint8_t msgId) {
  if (inFlightCount_ == 0 || !retry_.ack(msgId)) return false;
  inFlightCount_ = 0;
  return true;
}

size_t MidiForwarder::buildMidi(uint64_t nowUs, const Mac& dst, uint8_t* out, size_t cap) {
  if (sentOnce_ && nowUs - lastSendUs_ < kMinIntervalUs) return 0;

  bool due = false;
  if (inFlightCount_ > 0) {
    switch (retry_.poll(nowUs)) {
      case RetryTimer::Poll::Resend:
        ++retried_;
        due = true;
        break;
      case RetryTimer::Poll::GiveUp:
        abandoned_ += inFlightCount_;
        inFlightCount_ = 0;
        break;
      default:
        break;
    }
  }
  if (inFlightCount_ == 0 && qCount_ > 0) {
    while (inFlightCount_ < kMaxEventsPerPacket && qCount_ > 0) {
      inFlight_[inFlightCount_++] = queue_[qHead_];
      qHead_ = (qHead_ + 1) % kQueueCap;
      --qCount_;
    }
    retry_.begin(++nextId_, nowUs);
    due = true;
  }
  if (!due && !stateDirty_) return 0;

  MidiPayload p;
  if (stateDirty_) {
    p.hasState = true;
    snapshot(&p.state);
    stateDirty_ = false;
  }
  // Events still unacknowledged ride along with every packet, so state never overtakes them.
  if (inFlightCount_ > 0) {
    p.hasEvents = true;
    p.events.dst = dst;
    p.events.msgId = retry_.id();
    p.events.count = inFlightCount_;
    for (size_t i = 0; i < inFlightCount_; ++i) p.events.ev[i] = inFlight_[i];
  }
  const size_t n = encodePayload(p, out, cap);
  if (n > 0) {
    lastSendUs_ = nowUs;
    sentOnce_ = true;
  }
  return n;
}

// ---- MidiReceiver -------------------------------------------------------------------------------

bool MidiReceiver::applyState(uint16_t seq, const StateBlock& s, StateDelta* out) {
  if (haveSeq_ && !seqNewer(seq, lastSeq_)) return false;
  haveSeq_ = true;
  lastSeq_ = seq;

  out->jogTicks = 0;
  out->ccCount = 0;
  out->jogChannel = s.jogChannel;
  if (haveJog_) out->jogTicks = static_cast<int16_t>(static_cast<uint16_t>(s.jogCount - lastJog_));
  haveJog_ = true;
  lastJog_ = s.jogCount;

  for (size_t i = 0; i < s.nLatched; ++i) {
    const LatchedValue& v = s.latched[i];
    Latched* slot = nullptr;
    Latched* free = nullptr;
    for (size_t j = 0; j < kMaxLatched; ++j) {
      Latched& l = latched_[j];
      if (l.used && l.channel == v.channel && l.cc == v.cc) {
        slot = &l;
        break;
      }
      if (!l.used && free == nullptr) free = &l;
    }
    if (slot != nullptr) {
      if (slot->value == v.value) continue;
      slot->value = v.value;
    } else if (free != nullptr) {
      free->used = true;
      free->channel = v.channel;
      free->cc = v.cc;
      free->value = v.value;
    }
    out->cc[out->ccCount++] = midi::controlChange(v.channel, v.cc, v.value);
  }
  return true;
}

void MidiReceiver::noteHeld(const midi::Message& m) {
  if (m.len != 3) return;
  const bool on = m.isNoteOn();
  if (!on && !m.isNoteOff()) return;
  Held* free = nullptr;
  for (size_t i = 0; i < kMaxLatched; ++i) {
    Held& h = held_[i];
    if (h.used && h.channel == m.channel() && h.note == m.data1()) {
      if (!on) h.used = false;
      return;
    }
    if (!h.used && free == nullptr) free = &h;
  }
  if (on && free != nullptr) {
    free->used = true;
    free->channel = m.channel();
    free->note = m.data1();
  }
}

size_t MidiReceiver::releaseHeld(midi::Message* out, size_t cap) {
  size_t n = 0;
  for (size_t i = 0; i < kMaxLatched; ++i) {
    if (!held_[i].used) continue;
    if (n < cap) out[n++] = midi::noteOff(held_[i].channel, held_[i].note, 0);
    held_[i].used = false;
  }
  return n;
}

void MidiReceiver::forgetState() {
  haveSeq_ = false;
  haveJog_ = false;
  for (size_t i = 0; i < kMaxLatched; ++i) latched_[i].used = false;
}

void MidiReceiver::reset() {
  forgetState();
  for (size_t i = 0; i < kMaxLatched; ++i) held_[i].used = false;
  events_.reset();
}

// ---- Link ---------------------------------------------------------------------------------------

void Link::configure(const Config& c) {
  cfg_ = c;
  peers_.setSelf(c.mac, c.deck);
}

void Link::setUsbHost(bool mounted) {
  usbHost_ = mounted;
  refreshRoutes();
}

void Link::setBridgeMode(proto::LinkBridge mode) {
  bridgeMode_ = mode;
  refreshRoutes();
}

uint8_t Link::headerFlags() const {
  uint8_t f = 0;
  if (usbHost_) f |= proto::peerFlags::kUsbHost;
  if (routeOut_) f |= proto::peerFlags::kForwarding;
  if (peers_.selfIsMaster()) f |= proto::peerFlags::kIsMaster;
  return f;
}

bool Link::sendPacket(Type type, const uint8_t* payload, size_t len) {
  Header h;
  h.type = type;
  h.group = cfg_.group;
  h.deck = cfg_.deck;
  h.flags = headerFlags();
  h.epoch = cfg_.epoch;
  h.seq = seq_;
  h.mac = cfg_.mac;
  uint8_t buf[kMaxPacket];
  const size_t n = encodePacket(h, payload, len, buf, sizeof buf);
  if (n == 0) return false;
  ++seq_;
  const bool ok = tx_.send(buf, n);
  if (ok) {
    ++stats_.txPackets;
  } else {
    ++stats_.txFailed;
  }
  return ok;
}

void Link::sendHello() {
  Hello h;
  h.bridge = bridgeMode_;
  uint8_t buf[Hello::kBytes];
  sendPacket(Type::Hello, buf, encodePayload(h, buf, sizeof buf));
}

void Link::sendAck(const Mac& to, Type acked, uint8_t msgId) {
  Ack a;
  a.dst = to;
  a.acked = acked;
  a.msgId = msgId;
  uint8_t buf[Ack::kBytes];
  sendPacket(Type::Ack, buf, encodePayload(a, buf, sizeof buf));
}

void Link::sendTransportPacket() {
  TransportPayload t;
  t.dst = partner_;
  t.msgId = tNextId_;
  t.msg = tCur_;
  uint8_t buf[TransportPayload::kBytes];
  sendPacket(Type::Transport, buf, encodePayload(t, buf, sizeof buf));
}

void Link::releaseHeldNotes() {
  midi::Message off[kMaxLatched];
  const size_t n = rx_.releaseHeld(off, kMaxLatched);
  for (size_t i = 0; i < n; ++i) sink_.onBridgedMidi(off[i]);
}

void Link::refreshRoutes() {
  const PeerEntry* p = havePartner_ ? peers_.find(partner_) : nullptr;
  bool out = false, in = false;
  if (p != nullptr) {
    const BridgeEnd me{usbHost_, bridgeMode_};
    const BridgeEnd other{p->usbHost(), p->bridge};
    out = bridgeRoute(me, other);
    in = bridgeRoute(other, me);
  }
  if (out != routeOut_) {
    routeOut_ = out;
    if (out) {
      fwd_.markStateDirty();
      nextHeartbeatUs_ = nowUs_;
    }
  }
  if (in != routeIn_) {
    routeIn_ = in;
    if (!in) {
      releaseHeldNotes();
      jogOut_.clear();
    }
    rx_.forgetState();
  }
  if (!routeOut_ && !routeIn_) fwd_.clearEvents();  // nobody to deliver them to
}

void Link::onPartnerUp(uint64_t nowUs) {
  rx_.reset();
  sync_.reset();
  reqOutstanding_ = false;
  nextTimeReqUs_ = nowUs;
  tRx_.reset();
  if (routeOut_) fwd_.markStateDirty();
  nextHeartbeatUs_ = nowUs;
  if (tHave_) {  // the new peer learns the current transport state
    tRetry_.begin(++tNextId_, nowUs);
    sendTransportPacket();
  }
}

void Link::onPartnerLost() {
  releaseHeldNotes();
  rx_.reset();
  fwd_.clearEvents();
  sync_.reset();
  reqOutstanding_ = false;
  tRetry_.cancel();
  jogOut_.clear();
  havePartner_ = false;
}

void Link::updatePartner(uint64_t nowUs, const Mac* restarted) {
  const PeerEntry* p = peers_.partner();
  if (p == nullptr) {
    if (havePartner_) onPartnerLost();
  } else {
    const bool restart = restarted != nullptr && havePartner_ && *restarted == partner_ && p->mac == partner_;
    if (!havePartner_ || p->mac != partner_ || restart) {
      if (havePartner_) onPartnerLost();
      havePartner_ = true;
      partner_ = p->mac;
      refreshRoutes();  // before onPartnerUp, which may already send with the new flags
      onPartnerUp(nowUs);
    }
  }
  refreshRoutes();
}

bool Link::forwardMidi(const midi::Message& m) { return routeOut_ && fwd_.submit(proto::cable::kDeck, m); }

bool Link::relayHostMidi(const midi::Message& m) { return routeIn_ && fwd_.submit(proto::cable::kLinkedDeck, m); }

bool Link::sendTransport(const TransportMsg& msg, uint64_t nowUs) {
  tCur_ = msg;
  tHave_ = true;
  if (!havePartner_) return false;  // delivered when a peer appears
  tRetry_.begin(++tNextId_, nowUs);
  sendTransportPacket();
  return true;
}

void Link::handleState(uint16_t seq, const StateBlock& s) {
  if (!routeIn_) {
    rx_.forgetState();
    return;
  }
  MidiReceiver::StateDelta d;
  if (!rx_.applyState(seq, s, &d)) {
    ++stats_.rxStaleState;
    return;
  }
  jogOut_.add(d.jogTicks);
  jogChannel_ = d.jogChannel;
  for (size_t i = 0; i < d.ccCount; ++i) sink_.onBridgedMidi(d.cc[i]);
}

void Link::handleMidi(const Header& h, const uint8_t* p, size_t n) {
  MidiPayload m;
  if (!decodePayload(p, n, &m)) {
    ++stats_.rxMalformed;
    return;
  }
  if (m.hasEvents && m.events.dst == cfg_.mac) {
    sendAck(h.mac, Type::Midi, m.events.msgId);  // also for duplicates: the earlier ACK may have been lost
    if (rx_.acceptEvents(m.events.msgId) == MsgIdWindow::Verdict::New) {
      for (size_t i = 0; i < m.events.count; ++i) {
        const MidiEvent& e = m.events.ev[i];
        if (e.cable == proto::cable::kDeck && routeIn_) {
          rx_.noteHeld(e.msg);
          sink_.onBridgedMidi(e.msg);
        } else if (e.cable == proto::cable::kLinkedDeck && routeOut_) {
          sink_.onFeedbackMidi(e.msg);
        }
      }
    }
  }
  if (m.hasState) handleState(h.seq, m.state);
}

void Link::handleTransport(const Header& h, const uint8_t* p, size_t n) {
  TransportPayload t;
  if (!decodePayload(p, n, &t)) {
    ++stats_.rxMalformed;
    return;
  }
  if (t.dst != cfg_.mac) return;
  sendAck(h.mac, Type::Transport, t.msgId);
  if (tRx_.check(t.msgId) != MsgIdWindow::Verdict::New) return;
  TransportEvent e;
  e.msg = t.msg;
  const PeerEntry* master = peers_.master();
  e.fromMaster = master != nullptr && master->mac == h.mac;
  if (sync_.valid()) {
    e.atLocalValid = true;
    e.atLocalUs = sync_.localTime(t.msg.atUs);
  }
  sink_.onTransport(e);
}

void Link::handleTimeReq(const uint8_t* p, size_t n, uint64_t rxUs, uint64_t nowUs) {
  TimeReq q;
  if (!decodePayload(p, n, &q)) {
    ++stats_.rxMalformed;
    return;
  }
  if (q.dst != cfg_.mac) return;
  TimeResp r;
  r.dst = partner_;
  r.reqId = q.reqId;
  r.t1 = q.t1;
  r.t2 = static_cast<uint32_t>(rxUs);
  r.t3 = static_cast<uint32_t>(nowUs);
  uint8_t buf[TimeResp::kBytes];
  sendPacket(Type::TimeResp, buf, encodePayload(r, buf, sizeof buf));
}

void Link::handleTimeResp(const uint8_t* p, size_t n, uint64_t rxUs) {
  TimeResp r;
  if (!decodePayload(p, n, &r)) {
    ++stats_.rxMalformed;
    return;
  }
  if (r.dst != cfg_.mac || !reqOutstanding_ || r.reqId != reqId_ || r.t1 != reqT1_) return;
  reqOutstanding_ = false;
  sync_.addExchange(r.t1, r.t2, r.t3, static_cast<uint32_t>(rxUs));
}

void Link::onPacket(const uint8_t* data, size_t len, int8_t rssi, uint64_t rxUs, uint64_t nowUs) {
  nowUs_ = nowUs;
  Header h;
  const uint8_t* p = nullptr;
  if (decodePacket(data, len, &h, &p) != DecodeError::None) {
    ++stats_.rxCorrupt;
    return;
  }
  if (h.group != cfg_.group) {
    ++stats_.rxForeign;
    return;
  }
  const PeerTable::Heard heard = peers_.onHeard(h, rssi, nowUs);
  if (heard == PeerTable::Heard::Self) {
    ++stats_.rxForeign;
    return;
  }
  if (heard == PeerTable::Heard::TableFull) {
    ++stats_.rxTableFull;
    return;
  }
  ++stats_.rxPackets;
  const size_t n = h.length;

  if (h.type == Type::Hello) {
    Hello hello;
    if (decodePayload(p, n, &hello)) {
      peers_.setBridge(h.mac, hello.bridge);
    } else {
      ++stats_.rxMalformed;
    }
  }
  updatePartner(nowUs, heard == PeerTable::Heard::Restarted ? &h.mac : nullptr);
  if (!havePartner_ || h.mac != partner_) return;  // data flows only with the partner

  switch (h.type) {
    case Type::Hello:
      break;
    case Type::Heartbeat: {
      StateBlock s;
      if (decodePayload(p, n, &s)) {
        handleState(h.seq, s);
      } else {
        ++stats_.rxMalformed;
      }
      break;
    }
    case Type::TimeReq:
      handleTimeReq(p, n, rxUs, nowUs);
      break;
    case Type::TimeResp:
      handleTimeResp(p, n, rxUs);
      break;
    case Type::Midi:
      handleMidi(h, p, n);
      break;
    case Type::Transport:
      handleTransport(h, p, n);
      break;
    case Type::Ack: {
      Ack a;
      if (!decodePayload(p, n, &a)) {
        ++stats_.rxMalformed;
      } else if (a.dst == cfg_.mac) {
        if (a.acked == Type::Midi) {
          fwd_.onAck(a.msgId);
        } else {
          tRetry_.ack(a.msgId);
        }
      }
      break;
    }
  }
}

void Link::poll(uint64_t nowUs) {
  nowUs_ = nowUs;
  if (peers_.expire(nowUs) > 0) updatePartner(nowUs, nullptr);

  if (nowUs >= nextHelloUs_) {
    sendHello();
    nextHelloUs_ = nowUs + kHelloPeriodUs;
  }
  if (!havePartner_) return;

  if (nowUs >= nextTimeReqUs_) {
    TimeReq q;
    q.dst = partner_;
    q.reqId = ++reqId_;
    q.t1 = static_cast<uint32_t>(nowUs);
    reqT1_ = q.t1;
    reqOutstanding_ = true;
    uint8_t buf[TimeReq::kBytes];
    sendPacket(Type::TimeReq, buf, encodePayload(q, buf, sizeof buf));
    nextTimeReqUs_ = nowUs + (sync_.exchanges() < kFastExchanges ? kTimeReqFastUs : kTimeReqSlowUs);
  }

  switch (tRetry_.poll(nowUs)) {
    case RetryTimer::Poll::Resend:
      sendTransportPacket();
      break;
    case RetryTimer::Poll::GiveUp:  // it is state: keep repeating (same id, so the peer still applies it once)
      ++stats_.transportGiveUps;     // for as long as the peer counts as alive
      tRetry_.begin(tNextId_, nowUs);
      sendTransportPacket();
      break;
    default:
      break;
  }

  uint8_t buf[kMaxPayload];
  if (routeOut_ && nowUs >= nextHeartbeatUs_) {
    sendPacket(Type::Heartbeat, buf, fwd_.buildHeartbeat(buf, sizeof buf));
    nextHeartbeatUs_ = nowUs + kHeartbeatPeriodUs;
  }
  if (routeOut_ || routeIn_) {  // the bridge receiver sends host feedback back as events
    const size_t n = fwd_.buildMidi(nowUs, partner_, buf, sizeof buf);
    if (n > 0) sendPacket(Type::Midi, buf, n);
  }

  uint8_t v = 0;
  if (routeIn_ && jogOut_.poll(nowUs, &v)) {
    sink_.onBridgedMidi(midi::controlChange(jogChannel_, proto::cc::kJog, v));
  }
}

bool Link::masterTimeToLocal(uint32_t masterUs, uint32_t* localUs) const {
  if (peers_.selfIsMaster()) {
    *localUs = masterUs;
    return true;
  }
  if (!sync_.valid()) return false;
  *localUs = sync_.localTime(masterUs);
  return true;
}

bool Link::localTimeToMaster(uint32_t localUs, uint32_t* masterUs) const {
  if (peers_.selfIsMaster()) {
    *masterUs = localUs;
    return true;
  }
  if (!sync_.valid()) return false;
  *masterUs = sync_.peerTime(localUs);
  return true;
}

size_t Link::fillPeers(proto::Peer* out, size_t cap, uint64_t nowUs) const {
  size_t n = 0;
  const PeerEntry* master = peers_.master();
  for (size_t i = 0; i < kMaxPeers && n < cap; ++i) {
    const PeerEntry& e = peers_.slot(i);
    if (!e.used) continue;
    proto::Peer& p = out[n++];
    p = proto::Peer();
    p.deck = e.deck;
    p.flags = static_cast<uint8_t>(e.flags & (proto::peerFlags::kUsbHost | proto::peerFlags::kForwarding));
    if (master != nullptr && master->mac == e.mac) p.flags |= proto::peerFlags::kIsMaster;
    p.rssi = e.rssi;
    p.status = peers_.inConflict(e) ? kPeerConflict : static_cast<uint8_t>(0);
    if (havePartner_ && e.mac == partner_ && sync_.valid()) {
      p.rttUs = sync_.rttUs();
      p.offsetUs = sync_.offsetUs(static_cast<uint32_t>(nowUs));
    }
  }
  return n;
}

}  // namespace link
}  // namespace epdj
