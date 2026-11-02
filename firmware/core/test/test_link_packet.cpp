// Link framing and payload codecs: byte-exact vectors, round trips, rejection of corrupt and foreign
// packets, and fuzzing of every decoder (run under ASan/UBSan via -DEPDJ_SANITIZE=ON).
#include <cstring>
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
  uint32_t below(uint32_t n) { return next() % n; }
  uint8_t byte() { return static_cast<uint8_t>(next() >> 24); }
};

// Independent bit-by-bit CRC-16/CCITT-FALSE for cross-checking the table-driven one.
uint16_t refCrc16(const uint8_t* d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    crc = static_cast<uint16_t>(crc ^ (d[i] << 8));
    for (int b = 0; b < 8; ++b) crc = static_cast<uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
  }
  return crc;
}

Mac mac(uint8_t last) {
  Mac m;
  const uint8_t v[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, last};
  std::memcpy(m.b, v, 6);
  return m;
}

Header header(Type t, uint16_t seq = 1) {
  Header h;
  h.type = t;
  h.group = 7;
  h.deck = 1;
  h.flags = 0x05;
  h.epoch = 0xBEEF;
  h.seq = seq;
  h.mac = mac(0xEE);
  return h;
}

std::vector<uint8_t> pack(const Header& h, const std::vector<uint8_t>& payload) {
  uint8_t buf[kMaxPacket];
  const size_t n = encodePacket(h, payload.data(), payload.size(), buf, sizeof buf);
  return std::vector<uint8_t>(buf, buf + n);
}

// Re-stamps the CRC so a mutated packet reaches the payload decoders.
void fixCrc(std::vector<uint8_t>& p) {
  if (p.size() < 2) return;
  const uint16_t c = crc16(p.data(), p.size() - 2);
  p[p.size() - 2] = static_cast<uint8_t>(c & 0xFF);
  p[p.size() - 1] = static_cast<uint8_t>(c >> 8);
}

MidiPayload randomMidiPayload(Rng& r) {
  MidiPayload p;
  p.hasEvents = r.below(3) != 0;
  p.hasState = !p.hasEvents || r.below(2) != 0;
  if (p.hasEvents) {
    for (int i = 0; i < 6; ++i) p.events.dst.b[i] = r.byte();
    p.events.msgId = r.byte();
    p.events.count = static_cast<uint8_t>(1 + r.below(kMaxEventsPerPacket));
    for (size_t i = 0; i < p.events.count; ++i) {
      const uint8_t st = static_cast<uint8_t>((r.below(3) == 0 ? 0x80 : r.below(2) ? 0x90 : 0xB0) | r.below(16));
      p.events.ev[i].cable = static_cast<uint8_t>(r.below(16));
      p.events.ev[i].msg = midi::make(st, static_cast<uint8_t>(r.below(128)), static_cast<uint8_t>(r.below(128)), 3);
    }
  }
  if (p.hasState) {
    p.state.jogChannel = static_cast<uint8_t>(r.below(16));
    p.state.jogCount = static_cast<uint16_t>(r.next());
    p.state.nLatched = static_cast<uint8_t>(r.below(kMaxLatched + 1));
    for (size_t i = 0; i < p.state.nLatched; ++i) {
      p.state.latched[i].channel = static_cast<uint8_t>(r.below(16));
      p.state.latched[i].cc = static_cast<uint8_t>(r.below(128));
      p.state.latched[i].value = static_cast<uint8_t>(r.below(128));
    }
  }
  return p;
}

bool sameState(const StateBlock& a, const StateBlock& b) {
  if (a.jogChannel != b.jogChannel || a.jogCount != b.jogCount || a.nLatched != b.nLatched) return false;
  for (size_t i = 0; i < a.nLatched; ++i) {
    if (a.latched[i].channel != b.latched[i].channel || a.latched[i].cc != b.latched[i].cc ||
        a.latched[i].value != b.latched[i].value) {
      return false;
    }
  }
  return true;
}

bool sameMidi(const MidiPayload& a, const MidiPayload& b) {
  if (a.hasEvents != b.hasEvents || a.hasState != b.hasState) return false;
  if (a.hasEvents) {
    if (a.events.dst != b.events.dst || a.events.msgId != b.events.msgId || a.events.count != b.events.count) {
      return false;
    }
    for (size_t i = 0; i < a.events.count; ++i) {
      if (a.events.ev[i].cable != b.events.ev[i].cable || a.events.ev[i].msg != b.events.ev[i].msg) return false;
    }
  }
  return !a.hasState || sameState(a.state, b.state);
}

}  // namespace

EPDJ_TEST(link_crc16_matches_reference) {
  const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  CHECK_EQ(crc16(check, sizeof check), 0x29B1);
  CHECK_EQ(crc16(nullptr, 0), 0xFFFF);
  Rng r(11);
  for (int i = 0; i < 200; ++i) {
    uint8_t buf[64];
    const size_t n = r.below(65);
    for (size_t k = 0; k < n; ++k) buf[k] = r.byte();
    CHECK_EQ(crc16(buf, n), refCrc16(buf, n));
  }
}

// Vectors produced by an independent Python implementation of the format in docs/LINK.md.
EPDJ_TEST(link_known_vectors) {
  {
    Header h;
    h.type = Type::Hello;
    h.group = 0;
    h.deck = 1;
    h.flags = 0x05;
    h.epoch = 0x1234;
    h.seq = 7;
    h.mac = Mac{{0x02, 0x11, 0x22, 0x33, 0x44, 0x55}};
    const std::vector<uint8_t> want = {0x45, 0x4A, 0x01, 0x01, 0x00, 0x01, 0x05, 0x34, 0x12, 0x07,
                                       0x00, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0x01, 0x00, 0x35, 0x5E};
    Hello hello;
    uint8_t pl[1];
    CHECK_EQ(encodePayload(hello, pl, sizeof pl), 1);
    CHECK_BYTES(pack(h, std::vector<uint8_t>(pl, pl + 1)), want);
  }
  {
    MidiPayload p;
    p.hasEvents = true;
    p.events.dst = Mac{{0x02, 0x11, 0x22, 0x33, 0x44, 0x66}};
    p.events.msgId = 9;
    p.events.count = 2;
    p.events.ev[0].cable = 0;
    p.events.ev[0].msg = midi::noteOn(0, 36, 127);
    p.events.ev[1].cable = 2;
    p.events.ev[1].msg = midi::controlChange(0, 0x15, 0x41);
    p.hasState = true;
    p.state.jogChannel = 1;
    p.state.jogCount = 0x1234;
    p.state.nLatched = 1;
    p.state.latched[0].channel = 0;
    p.state.latched[0].cc = 23;
    p.state.latched[0].value = 100;
    uint8_t pl[kMaxPayload];
    const size_t n = encodePayload(p, pl, sizeof pl);
    CHECK_EQ(n, 24);
    Header h;
    h.type = Type::Midi;
    h.group = 7;
    h.deck = 0;
    h.flags = 0x02;
    h.epoch = 0xBEEF;
    h.seq = 0xFFFF;
    h.mac = mac(0xEE);
    const std::vector<uint8_t> want = {
        0x45, 0x4A, 0x01, 0x05, 0x07, 0x00, 0x02, 0xEF, 0xBE, 0xFF, 0xFF, 0x02, 0xAA, 0xBB, 0xCC, 0xDD,
        0xEE, 0x18, 0x03, 0x02, 0x11, 0x22, 0x33, 0x44, 0x66, 0x09, 0x02, 0x00, 0x90, 0x24, 0x7F, 0x02,
        0xB0, 0x15, 0x41, 0x01, 0x34, 0x12, 0x01, 0x00, 0x17, 0x64, 0xE1, 0x04};
    const std::vector<uint8_t> got = pack(h, std::vector<uint8_t>(pl, pl + n));
    CHECK_BYTES(got, want);
    Header d;
    const uint8_t* payload = nullptr;
    CHECK(decodePacket(want.data(), want.size(), &d, &payload) == DecodeError::None);
    CHECK_EQ(d.seq, 0xFFFF);
    CHECK_EQ(d.length, 24);
    MidiPayload back;
    CHECK(decodePayload(payload, d.length, &back));
    CHECK(sameMidi(back, p));
  }
}

EPDJ_TEST(link_header_round_trip_all_types) {
  Rng r(3);
  for (uint8_t t = 1; t <= 7; ++t) {
    for (size_t len : {size_t(0), size_t(1), size_t(19), kMaxPayload}) {
      Header h = header(static_cast<Type>(t), static_cast<uint16_t>(r.next()));
      h.group = r.byte();
      h.deck = r.byte();
      h.flags = r.byte();
      h.epoch = static_cast<uint16_t>(r.next());
      std::vector<uint8_t> pl(len);
      for (auto& b : pl) b = r.byte();
      const std::vector<uint8_t> pkt = pack(h, pl);
      CHECK_EQ(pkt.size(), kHeaderBytes + len + kCrcBytes);
      CHECK(pkt.size() <= kMaxPacket);
      Header d;
      const uint8_t* p = nullptr;
      CHECK(decodePacket(pkt.data(), pkt.size(), &d, &p) == DecodeError::None);
      CHECK(d.type == h.type);
      CHECK_EQ(d.group, h.group);
      CHECK_EQ(d.deck, h.deck);
      CHECK_EQ(d.flags, h.flags);
      CHECK_EQ(d.epoch, h.epoch);
      CHECK_EQ(d.seq, h.seq);
      CHECK(d.mac == h.mac);
      CHECK_EQ(d.length, len);
      CHECK(len == 0 || std::memcmp(p, pl.data(), len) == 0);
    }
  }
  CHECK_EQ(kMaxPayload + kHeaderBytes + kCrcBytes, kMaxPacket);
}

EPDJ_TEST(link_encode_refuses_bad_input) {
  uint8_t buf[kMaxPacket];
  uint8_t big[kMaxPayload + 1] = {};
  CHECK_EQ(encodePacket(header(Type::Midi), big, kMaxPayload + 1, buf, sizeof buf), 0);
  CHECK_EQ(encodePacket(header(Type::Midi), nullptr, 3, buf, sizeof buf), 0);
  Header h = header(Type::Hello);
  h.mac = Mac();
  CHECK_EQ(encodePacket(h, big, 1, buf, sizeof buf), 0);
  h.mac = Mac{{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
  CHECK_EQ(encodePacket(h, big, 1, buf, sizeof buf), 0);
  h.mac = Mac{{0x03, 0x00, 0x00, 0x00, 0x00, 0x01}};  // group bit set
  CHECK_EQ(encodePacket(h, big, 1, buf, sizeof buf), 0);
  CHECK_EQ(encodePacket(header(Type::Hello), big, 4, buf, kHeaderBytes + 4 + kCrcBytes - 1), 0);
  CHECK_EQ(encodePacket(header(Type::Hello), big, 4, buf, kHeaderBytes + 4 + kCrcBytes), kHeaderBytes + 4 + kCrcBytes);
}

EPDJ_TEST(link_every_single_bit_flip_is_rejected) {
  Rng r(5);
  for (int trial = 0; trial < 6; ++trial) {
    std::vector<uint8_t> pl(trial * 7);
    for (auto& b : pl) b = r.byte();
    const std::vector<uint8_t> good = pack(header(Type::Midi, static_cast<uint16_t>(trial)), pl);
    Header d;
    const uint8_t* p = nullptr;
    CHECK(decodePacket(good.data(), good.size(), &d, &p) == DecodeError::None);
    for (size_t bit = 0; bit < good.size() * 8; ++bit) {
      std::vector<uint8_t> bad = good;
      bad[bit / 8] = static_cast<uint8_t>(bad[bit / 8] ^ (1u << (bit % 8)));
      if (decodePacket(bad.data(), bad.size(), &d, &p) == DecodeError::None) {
        CHECK(false);  // a single flipped bit got through
        return;
      }
    }
  }
}

EPDJ_TEST(link_truncation_and_trailing_bytes_are_rejected) {
  const std::vector<uint8_t> good = pack(header(Type::Heartbeat), std::vector<uint8_t>(30, 0x11));
  Header d;
  const uint8_t* p = nullptr;
  for (size_t n = 0; n < good.size(); ++n) {
    CHECK(decodePacket(good.data(), n, &d, &p) != DecodeError::None);
  }
  CHECK(decodePacket(good.data(), 0, &d, &p) == DecodeError::TooShort);
  CHECK(decodePacket(good.data(), kHeaderBytes + kCrcBytes - 1, &d, &p) == DecodeError::TooShort);
  std::vector<uint8_t> longer = good;
  longer.push_back(0);
  CHECK(decodePacket(longer.data(), longer.size(), &d, &p) == DecodeError::BadLength);
}

EPDJ_TEST(link_specific_rejections) {
  const std::vector<uint8_t> good = pack(header(Type::Ack), std::vector<uint8_t>(Ack::kBytes, 0));
  Header d;
  const uint8_t* p = nullptr;
  auto with = [&](size_t idx, uint8_t v, bool refix) {
    std::vector<uint8_t> x = good;
    x[idx] = v;
    if (refix) fixCrc(x);
    return decodePacket(x.data(), x.size(), &d, &p);
  };
  CHECK(with(0, 0x00, true) == DecodeError::BadMagic);
  CHECK(with(1, 0x00, true) == DecodeError::BadMagic);
  CHECK(with(2, 2, true) == DecodeError::BadVersion);  // a future version is not misparsed
  CHECK(with(3, 0, true) == DecodeError::BadType);
  CHECK(with(3, 8, true) == DecodeError::BadType);
  CHECK(with(3, 0xFF, true) == DecodeError::BadType);
  CHECK(with(11, 0x01, true) == DecodeError::BadSource);  // multicast bit
  CHECK(with(17, static_cast<uint8_t>(Ack::kBytes + 1), true) == DecodeError::BadLength);
  CHECK(with(6, 0x00, false) == DecodeError::BadCrc);
  std::vector<uint8_t> zeroMac = good;
  CHECK_EQ(zeroMac.size(), kHeaderBytes + Ack::kBytes + kCrcBytes);
  if (zeroMac.size() < 17) return;
  for (size_t i = 11; i < 17; ++i) zeroMac[i] = 0;
  fixCrc(zeroMac);
  CHECK(decodePacket(zeroMac.data(), zeroMac.size(), &d, &p) == DecodeError::BadSource);
}

EPDJ_TEST(link_random_garbage_never_decodes) {
  Rng r(77);
  Header d;
  const uint8_t* p = nullptr;
  uint8_t buf[300];
  for (int i = 0; i < 200000; ++i) {
    const size_t n = r.below(sizeof buf + 1);
    for (size_t k = 0; k < n; ++k) buf[k] = r.byte();
    if (n >= 2 && r.below(2)) {  // sometimes with a correct magic, to get past the first check
      buf[0] = kMagic0;
      buf[1] = kMagic1;
    }
    if (n >= 3 && r.below(2)) buf[2] = kVersion;
    CHECK(decodePacket(buf, n, &d, &p) != DecodeError::None);
  }
  // Frames that other software might broadcast on the same channel.
  const char* ascii = "ESP-NOW broadcast hello from somebody else's firmware";
  CHECK(decodePacket(reinterpret_cast<const uint8_t*>(ascii), std::strlen(ascii), &d, &p) != DecodeError::None);
}

EPDJ_TEST(link_midi_payload_round_trip_random) {
  Rng r(21);
  for (int i = 0; i < 5000; ++i) {
    const MidiPayload p = randomMidiPayload(r);
    uint8_t buf[kMaxPayload];
    const size_t n = encodePayload(p, buf, sizeof buf);
    CHECK(n > 0);
    CHECK(n <= kMaxPayload);
    MidiPayload q;
    CHECK(decodePayload(buf, n, &q));
    CHECK(sameMidi(p, q));
    // one byte short or long never decodes
    MidiPayload tmp;
    CHECK(!decodePayload(buf, n - 1, &tmp));
    uint8_t longer[kMaxPayload + 1];
    std::memcpy(longer, buf, n);
    longer[n] = 0;
    CHECK(!decodePayload(longer, n + 1, &tmp));
  }
}

EPDJ_TEST(link_fixed_payload_round_trips) {
  Rng r(31);
  for (int i = 0; i < 500; ++i) {
    uint8_t buf[kMaxPayload];
    TimeReq q;
    for (int k = 0; k < 6; ++k) q.dst.b[k] = r.byte();
    q.reqId = r.byte();
    q.t1 = r.next();
    TimeReq q2;
    CHECK_EQ(encodePayload(q, buf, sizeof buf), TimeReq::kBytes);
    CHECK(decodePayload(buf, TimeReq::kBytes, &q2));
    CHECK(q2.dst == q.dst && q2.reqId == q.reqId && q2.t1 == q.t1);

    TimeResp a;
    a.dst = q.dst;
    a.reqId = r.byte();
    a.t1 = r.next();
    a.t2 = r.next();
    a.t3 = r.next();
    TimeResp a2;
    CHECK_EQ(encodePayload(a, buf, sizeof buf), TimeResp::kBytes);
    CHECK(decodePayload(buf, TimeResp::kBytes, &a2));
    CHECK(a2.dst == a.dst && a2.reqId == a.reqId && a2.t1 == a.t1 && a2.t2 == a.t2 && a2.t3 == a.t3);

    TransportPayload t;
    t.dst = q.dst;
    t.msgId = r.byte();
    t.msg.state = static_cast<proto::TransportState>(r.below(3));
    t.msg.bpmX10 = static_cast<uint16_t>(proto::kBpmMinX10 + r.below(proto::kBpmMaxX10 - proto::kBpmMinX10 + 1));
    t.msg.positionClocks = r.next();
    t.msg.atUs = r.next();
    TransportPayload t2;
    CHECK_EQ(encodePayload(t, buf, sizeof buf), TransportPayload::kBytes);
    CHECK(decodePayload(buf, TransportPayload::kBytes, &t2));
    CHECK(t2.dst == t.dst && t2.msgId == t.msgId && t2.msg.state == t.msg.state && t2.msg.bpmX10 == t.msg.bpmX10 &&
          t2.msg.positionClocks == t.msg.positionClocks && t2.msg.atUs == t.msg.atUs);

    Ack k;
    k.dst = q.dst;
    k.acked = r.below(2) ? Type::Midi : Type::Transport;
    k.msgId = r.byte();
    Ack k2;
    CHECK_EQ(encodePayload(k, buf, sizeof buf), Ack::kBytes);
    CHECK(decodePayload(buf, Ack::kBytes, &k2));
    CHECK(k2.dst == k.dst && k2.acked == k.acked && k2.msgId == k.msgId);
  }
}

EPDJ_TEST(link_payload_decoders_are_strict) {
  uint8_t buf[kMaxPayload];
  {
    Hello h;
    uint8_t b[1] = {3};
    CHECK(!decodePayload(b, 1, &h));
    b[0] = 2;
    CHECK(decodePayload(b, 1, &h));
    CHECK(!decodePayload(b, 0, &h));
  }
  {
    TransportPayload t;
    t.dst = mac(1);
    t.msg.bpmX10 = proto::kBpmMinX10 - 1;
    encodePayload(t, buf, sizeof buf);
    TransportPayload o;
    CHECK(!decodePayload(buf, TransportPayload::kBytes, &o));
    t.msg.bpmX10 = proto::kBpmMaxX10 + 1;
    encodePayload(t, buf, sizeof buf);
    CHECK(!decodePayload(buf, TransportPayload::kBytes, &o));
    t.msg.bpmX10 = 1200;
    encodePayload(t, buf, sizeof buf);
    buf[7] = 3;  // no such transport state
    CHECK(!decodePayload(buf, TransportPayload::kBytes, &o));
  }
  {
    Ack a;
    a.dst = mac(1);
    a.acked = Type::Hello;
    encodePayload(a, buf, sizeof buf);
    Ack o;
    CHECK(!decodePayload(buf, Ack::kBytes, &o));
  }
  {
    StateBlock s;
    s.nLatched = 1;
    s.latched[0] = {0, 23, 100};
    uint8_t b[16];
    CHECK_EQ(encodePayload(s, b, sizeof b), 7);
    StateBlock o;
    CHECK(decodePayload(b, 7, &o));
    b[0] = 16;  // jog channel
    CHECK(!decodePayload(b, 7, &o));
    b[0] = 0;
    b[6] = 128;  // value
    CHECK(!decodePayload(b, 7, &o));
    b[6] = 100;
    b[3] = static_cast<uint8_t>(kMaxLatched + 1);
    CHECK(!decodePayload(b, 7, &o));
    CHECK(encodePayload(s, b, 6) == 0);  // output buffer too small
  }
  {
    MidiPayload p;
    p.hasEvents = true;
    p.events.dst = mac(1);
    p.events.count = 1;
    p.events.ev[0].msg = midi::noteOn(0, 36, 127);
    const size_t n = encodePayload(p, buf, sizeof buf);
    MidiPayload o;
    CHECK(decodePayload(buf, n, &o));
    uint8_t save = buf[0];
    buf[0] = 0x04;  // unknown flag
    CHECK(!decodePayload(buf, n, &o));
    buf[0] = 0x00;  // nothing in it
    CHECK(!decodePayload(buf, 1, &o));
    buf[0] = save;
    buf[8] = 0;  // zero events
    CHECK(!decodePayload(buf, n, &o));
    buf[8] = static_cast<uint8_t>(kMaxEventsPerPacket + 1);
    CHECK(!decodePayload(buf, n, &o));
    buf[8] = 1;
    buf[10] = 0xF8;  // a real-time byte is not a forwardable event
    CHECK(!decodePayload(buf, n, &o));
    buf[10] = 0x90;
    buf[11] = 0x80;  // data byte with the high bit
    CHECK(!decodePayload(buf, n, &o));
    buf[11] = 36;
    buf[9] = 16;  // cable
    CHECK(!decodePayload(buf, n, &o));
    CHECK(encodePayload(MidiPayload(), buf, sizeof buf) == 0);  // empty payload is not a packet
  }
}

// Whatever a decoder accepts must be canonical: re-encoding gives back the same bytes.
EPDJ_TEST(link_payload_decoders_fuzz) {
  Rng r(99);
  uint8_t buf[kMaxPayload], enc[kMaxPayload];
  int accepted = 0;
  auto check = [&](const uint8_t* in, size_t n) {
    Hello h;
    if (decodePayload(in, n, &h)) {
      CHECK_EQ(encodePayload(h, enc, sizeof enc), n);
      CHECK(std::memcmp(enc, in, n) == 0);
      ++accepted;
    }
    StateBlock s;
    if (decodePayload(in, n, &s)) {
      CHECK_EQ(encodePayload(s, enc, sizeof enc), n);
      CHECK(std::memcmp(enc, in, n) == 0);
      ++accepted;
    }
    MidiPayload m;
    if (decodePayload(in, n, &m)) {
      CHECK_EQ(encodePayload(m, enc, sizeof enc), n);
      CHECK(std::memcmp(enc, in, n) == 0);
      ++accepted;
    }
    TimeReq a;
    if (decodePayload(in, n, &a)) {
      CHECK_EQ(encodePayload(a, enc, sizeof enc), n);
      CHECK(std::memcmp(enc, in, n) == 0);
    }
    TimeResp b;
    if (decodePayload(in, n, &b)) {
      CHECK_EQ(encodePayload(b, enc, sizeof enc), n);
      CHECK(std::memcmp(enc, in, n) == 0);
    }
    TransportPayload t;
    if (decodePayload(in, n, &t)) {
      CHECK_EQ(encodePayload(t, enc, sizeof enc), n);
      CHECK(std::memcmp(enc, in, n) == 0);
      ++accepted;
    }
    Ack k;
    if (decodePayload(in, n, &k)) {
      CHECK_EQ(encodePayload(k, enc, sizeof enc), n);
      CHECK(std::memcmp(enc, in, n) == 0);
      ++accepted;
    }
  };
  for (int i = 0; i < 100000; ++i) {  // pure noise
    const size_t n = r.below(40);
    for (size_t k = 0; k < n; ++k) buf[k] = r.byte();
    check(buf, n);
  }
  for (int i = 0; i < 60000; ++i) {  // valid payloads with a few bytes damaged
    const MidiPayload p = randomMidiPayload(r);
    size_t n = encodePayload(p, buf, sizeof buf);
    for (int k = static_cast<int>(r.below(4)); k > 0; --k) buf[r.below(static_cast<uint32_t>(n))] = r.byte();
    if (r.below(4) == 0) n = r.below(static_cast<uint32_t>(n) + 1);
    check(buf, n);
  }
  CHECK(accepted > 1000);  // the damaged-but-valid cases really do reach the accept path

  // End to end: random bytes inside a correctly framed, correctly CRC'd packet.
  for (int i = 0; i < 30000; ++i) {
    std::vector<uint8_t> pl(r.below(64));
    for (auto& b : pl) b = r.byte();
    Header h = header(static_cast<Type>(1 + r.below(7)));
    const std::vector<uint8_t> pkt = pack(h, pl);
    Header d;
    const uint8_t* p = nullptr;
    CHECK(decodePacket(pkt.data(), pkt.size(), &d, &p) == DecodeError::None);
    check(p, d.length);
  }
}
