// SysEx framing: 8->7 packing, checksum, frames, reply builders. Oracle: protocol/test-vectors.json.
#include <vector>

#include "epdj/protocol.h"
#include "epdj/sysex.h"
#include "test_harness.h"
#include "vectors.generated.h"

using namespace epdj;
using namespace epdj::syx;
using Bytes = std::vector<uint8_t>;

namespace {

Bytes packed(const Bytes& raw) {
  Bytes out(packedSize(raw.size()) + 1);
  size_t n = 0;
  CHECK(pack8to7(raw.data(), raw.size(), out.data(), out.size(), &n));
  out.resize(n);
  return out;
}

Bytes frameOf(uint8_t deck, uint8_t cmd, const Bytes& payload) {
  Bytes out(kMaxFrameBytes);
  const size_t n = encodeFrame(deck, cmd, payload.data(), payload.size(), out.data(), out.size());
  out.resize(n);
  return out;
}

Bytes payloadOf(const Frame& f) { return Bytes(f.payload, f.payload + f.payloadLen); }

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  uint32_t below(uint32_t n) { return next() % n; }
};

}  // namespace

EPDJ_TEST(pack_matches_vectors_and_unpacks_back) {
  for (const auto& v : epdj_vec::packVectors()) {
    CHECK_BYTES(packed(v.raw), v.packed);
    Bytes back(v.raw.size() + 1);
    size_t n = 0;
    CHECK(unpack7to8(v.packed.data(), v.packed.size(), back.data(), back.size(), &n));
    back.resize(n);
    CHECK_BYTES(back, v.raw);
  }
}

EPDJ_TEST(size_helpers_are_inverse) {
  for (size_t n = 0; n <= 400; ++n) {
    CHECK_EQ(unpackedSize(packedSize(n)), n);
    CHECK(packedSize(n) % 8 != 1);
  }
  CHECK_EQ(unpackedSize(1), kInvalidSize);
  CHECK_EQ(unpackedSize(9), kInvalidSize);
  CHECK_EQ(unpackedSize(0), 0u);
  CHECK_EQ(unpackedSize(2), 1u);
  CHECK_EQ(unpackedSize(8), 7u);
}

EPDJ_TEST(pack_unpack_reject_bad_arguments) {
  uint8_t out[32];
  size_t n = 0;
  const uint8_t raw[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  CHECK(!pack8to7(raw, 8, out, 9, &n));  // needs 10
  CHECK(pack8to7(raw, 8, out, 10, &n));
  CHECK_EQ(n, 10u);

  const uint8_t good[2] = {0, 5};
  CHECK(unpack7to8(good, 2, out, 1, &n));
  CHECK(!unpack7to8(good, 2, out, 0, &n));       // output too small
  const uint8_t dangling[1] = {3};               // header septet without data
  CHECK(!unpack7to8(dangling, 1, out, 32, &n));
  const uint8_t highBit[2] = {0, 0x85};
  CHECK(!unpack7to8(highBit, 2, out, 32, &n));
}

EPDJ_TEST(checksum_definition) {
  const uint8_t a[] = {1, 0, 1};
  CHECK_EQ(checksum(a, 3), 126);  // getInfo vector
  CHECK_EQ(checksum(a, 0), 0);
  const uint8_t b[] = {0x7F, 0x7F, 0x7F};
  CHECK_EQ(checksum(b, 3), 3);  // -(381) & 0x7F
  // data + checksum always sums to 0 mod 128
  Rng rng(5);
  for (int i = 0; i < 200; ++i) {
    uint8_t d[20];
    const size_t n = rng.below(20);
    unsigned sum = 0;
    for (size_t k = 0; k < n; ++k) { d[k] = static_cast<uint8_t>(rng.below(128)); sum += d[k]; }
    CHECK_EQ((sum + checksum(d, n)) & 0x7F, 0);
  }
}

EPDJ_TEST(frames_match_vectors_encode_and_decode) {
  for (const auto& v : epdj_vec::frameVectors()) {
    CHECK_BYTES(frameOf(v.deck, v.cmd, v.payload), v.frame);

    Frame f;
    CHECK(decodeFrame(v.frame.data(), v.frame.size(), kNoDeckFilter, &f) == DecodeStatus::Ok);
    CHECK_EQ(f.deck, v.deck);
    CHECK_EQ(f.cmd, v.cmd);
    CHECK_BYTES(payloadOf(f), v.payload);

    // Addressed to its own deck, or to everyone: accepted. Another deck: filtered.
    CHECK(decodeFrame(v.frame.data(), v.frame.size(), v.deck == 127 ? 0 : v.deck, &f) == DecodeStatus::Ok);
    if (v.deck != 127) {
      const uint8_t other = v.deck == 0 ? 1 : 0;
      CHECK(decodeFrame(v.frame.data(), v.frame.size(), other, &f) == DecodeStatus::NotForUs);
      CHECK_EQ(f.deck, v.deck);  // still reported so a caller can log it
    }
  }
}

EPDJ_TEST(deck_filter_rules) {
  const Bytes forB = frameOf(1, 1, {});
  const Bytes any = frameOf(127, 1, {});
  const Bytes forFifteen = frameOf(15, 1, {});
  Frame f;
  CHECK(decodeFrame(forB.data(), forB.size(), 1, &f) == DecodeStatus::Ok);
  CHECK(decodeFrame(forB.data(), forB.size(), 0, &f) == DecodeStatus::NotForUs);
  CHECK(decodeFrame(any.data(), any.size(), 0, &f) == DecodeStatus::Ok);
  CHECK(decodeFrame(any.data(), any.size(), 15, &f) == DecodeStatus::Ok);
  CHECK(decodeFrame(forFifteen.data(), forFifteen.size(), 15, &f) == DecodeStatus::Ok);
  CHECK(decodeFrame(forFifteen.data(), forFifteen.size(), 0, &f) == DecodeStatus::NotForUs);

  Bytes bad = forB;
  bad[5] = 50;  // deck byte outside 0..15 / 127
  bad[bad.size() - 2] = checksum(bad.data() + 4, bad.size() - 6);
  CHECK(decodeFrame(bad.data(), bad.size(), kNoDeckFilter, &f) == DecodeStatus::BadDeck);
  CHECK(decodeFrame(bad.data(), bad.size(), 1, &f) == DecodeStatus::BadDeck);
}

EPDJ_TEST(decode_rejects_malformed_frames) {
  const Bytes good = frameOf(0, 3, {3, 1, 0, 0, 0});
  Frame f;
  CHECK(decodeFrame(good.data(), good.size(), 0, &f) == DecodeStatus::Ok);

  // Too short.
  for (size_t n = 0; n < kHeaderBytes + 2; ++n) {
    CHECK(decodeFrame(good.data(), n, 0, &f) == DecodeStatus::TooShort);
  }
  // Header bytes, one at a time.
  for (size_t i : {size_t{0}, size_t{1}, size_t{2}, size_t{3}}) {
    Bytes b = good;
    b[i] = static_cast<uint8_t>(b[i] ^ 0x01);
    CHECK(decodeFrame(b.data(), b.size(), 0, &f) == DecodeStatus::Malformed);
  }
  { Bytes b = good; b.back() = 0x7E; CHECK(decodeFrame(b.data(), b.size(), 0, &f) == DecodeStatus::Malformed); }
  // Version.
  { Bytes b = good; b[4] = 2; CHECK(decodeFrame(b.data(), b.size(), 0, &f) == DecodeStatus::BadVersion); }
  // A flipped bit anywhere between version and checksum is caught (or flagged malformed).
  for (size_t i = 4; i + 1 < good.size(); ++i) {
    Bytes b = good;
    b[i] = static_cast<uint8_t>(b[i] ^ 0x01);
    const DecodeStatus s = decodeFrame(b.data(), b.size(), kNoDeckFilter, &f);
    CHECK(s != DecodeStatus::Ok);
  }
  // Status byte inside the frame.
  { Bytes b = good; b[8] = 0x90; CHECK(decodeFrame(b.data(), b.size(), 0, &f) == DecodeStatus::Malformed); }
  // Checksum that is simply wrong.
  { Bytes b = good; b[b.size() - 2] = static_cast<uint8_t>((b[b.size() - 2] + 1) & 0x7F);
    CHECK(decodeFrame(b.data(), b.size(), 0, &f) == DecodeStatus::BadChecksum); }
}

EPDJ_TEST(decode_reports_bad_payload_and_too_long_with_header_filled_in) {
  // 9 payload septets = a header septet with no data after the 8th: cannot come from packing.
  Bytes m = {0xF0, 0x7D, 0x45, 0x44, 1, 0, 3};
  for (int i = 0; i < 9; ++i) m.push_back(1);
  m.push_back(checksum(m.data() + 4, m.size() - 4));
  m.push_back(0xF7);
  Frame f;
  CHECK(decodeFrame(m.data(), m.size(), 0, &f) == DecodeStatus::BadPayload);
  CHECK_EQ(f.cmd, 3);
  CHECK_EQ(f.deck, 0);

  // 97 raw bytes: one more than we accept.
  Bytes big(kMaxPayload + 1, 0x55);
  Bytes frame(kMaxFrameBytes + 16);
  const size_t n = encodeFrame(0, 3, big.data(), 96, frame.data(), frame.size());
  CHECK(n != 0);
  // Build the oversize frame by hand since encodeFrame refuses it.
  Bytes p(packedSize(big.size()) + 1);
  size_t pn = 0;
  CHECK(pack8to7(big.data(), big.size(), p.data(), p.size(), &pn));
  Bytes over = {0xF0, 0x7D, 0x45, 0x44, 1, 0, 9};
  over.insert(over.end(), p.begin(), p.begin() + static_cast<long>(pn));
  over.push_back(checksum(over.data() + 4, over.size() - 4));
  over.push_back(0xF7);
  CHECK(decodeFrame(over.data(), over.size(), 0, &f) == DecodeStatus::TooLong);
  CHECK_EQ(f.cmd, 9);
}

EPDJ_TEST(encode_frame_limits) {
  uint8_t out[kMaxFrameBytes];
  uint8_t payload[kMaxPayload + 1] = {};
  // The largest accepted payload exactly fills kMaxFrameBytes.
  const size_t n = encodeFrame(0, 1, payload, kMaxPayload, out, sizeof(out));
  CHECK_EQ(n, kMaxFrameBytes);
  CHECK_EQ(encodeFrame(0, 1, payload, kMaxPayload + 1, out, sizeof(out)), 0u);
  CHECK_EQ(encodeFrame(0, 1, payload, kMaxPayload, out, kMaxFrameBytes - 1), 0u);
  CHECK_EQ(encodeFrame(0, 1, nullptr, 0, out, 9), 9u);
  CHECK_EQ(encodeFrame(0, 1, nullptr, 0, out, 8), 0u);
  // Deck and cmd are forced to 7 bits so a stray high bit cannot corrupt the framing.
  encodeFrame(0xFF, 0xFF, nullptr, 0, out, sizeof(out));
  CHECK_EQ(out[5], 0x7F);
  CHECK_EQ(out[6], 0x7F);
  Frame f;
  CHECK(decodeFrame(out, 9, kNoDeckFilter, &f) == DecodeStatus::Ok);
}

EPDJ_TEST(reply_builders_match_vectors) {
  uint8_t out[kMaxFrameBytes];
  for (const auto& v : epdj_vec::frameVectors()) {
    size_t n = 0;
    if (v.cmd == 0x7E) n = encodeAck(v.deck, v.payload[0], out, sizeof(out));
    else if (v.cmd == 0x7F) n = encodeNak(v.deck, v.payload[0], static_cast<proto::sysex::Err>(v.payload[1]), out, sizeof(out));
    else if (v.cmd == 0x41) {
      proto::Info info;
      CHECK(proto::deserialize(v.payload.data(), v.payload.size(), &info));
      n = encodeInfo(v.deck, info, out, sizeof(out));
    } else if (v.cmd == 0x47) {
      proto::Telemetry t;
      CHECK(proto::deserialize(v.payload.data(), v.payload.size(), &t));
      n = encodeTelemetry(v.deck, t, out, sizeof(out));
    } else {
      continue;
    }
    CHECK_BYTES(Bytes(out, out + n), v.frame);
  }
}

EPDJ_TEST(typed_replies_round_trip) {
  uint8_t out[kMaxFrameBytes];
  Frame f;

  size_t n = encodeConfigValue(1, 23, -2, out, sizeof(out));
  CHECK(decodeFrame(out, n, 1, &f) == DecodeStatus::Ok);
  CHECK_EQ(f.cmd, 0x42);
  CHECK_EQ(f.payloadLen, 5u);
  CHECK_EQ(f.payload[0], 23);
  CHECK_EQ(readI32le(f.payload + 1), -2);

  proto::CalResult cr;
  CHECK(proto::deserialize(epdj_vec::bytesCalResult().data(), epdj_vec::bytesCalResult().size(), &cr));
  n = encodeCalResult(0, cr, out, sizeof(out));
  CHECK(decodeFrame(out, n, 0, &f) == DecodeStatus::Ok);
  CHECK_EQ(f.cmd, 0x48);
  CHECK_BYTES(payloadOf(f), epdj_vec::bytesCalResult());

  proto::Peer peers[kMaxPeers + 3];
  for (size_t i = 0; i < kMaxPeers + 3; ++i) {
    peers[i].deck = static_cast<uint8_t>(i & 1);
    peers[i].rssi = static_cast<int8_t>(-40 - static_cast<int>(i));
    peers[i].offsetUs = -250 * static_cast<int32_t>(i);
  }
  n = encodeLink(0, peers, 2, out, sizeof(out));
  CHECK(decodeFrame(out, n, 0, &f) == DecodeStatus::Ok);
  CHECK_EQ(f.cmd, 0x4B);
  CHECK_EQ(f.payload[0], 2);
  CHECK_EQ(f.payloadLen, 1u + 2 * proto::Peer::kBytes);
  proto::Peer p1;
  CHECK(proto::deserialize(f.payload + 1 + proto::Peer::kBytes, proto::Peer::kBytes, &p1));
  CHECK_EQ(p1.rssi, -41);
  CHECK_EQ(p1.offsetUs, -250);

  n = encodeLink(0, peers, 0, out, sizeof(out));
  CHECK(decodeFrame(out, n, 0, &f) == DecodeStatus::Ok);
  CHECK_EQ(f.payloadLen, 1u);
  CHECK_EQ(f.payload[0], 0);

  // More peers than fit are clamped, never overflowing the buffer.
  n = encodeLink(0, peers, kMaxPeers + 3, out, sizeof(out));
  CHECK(n != 0);
  CHECK(decodeFrame(out, n, 0, &f) == DecodeStatus::Ok);
  CHECK_EQ(f.payload[0], kMaxPeers);
}

EPDJ_TEST(little_endian_helpers) {
  uint8_t b[4];
  writeI32le(-70000, b);
  CHECK_EQ(readI32le(b), -70000);
  writeI32le(0x01020304, b);
  CHECK_EQ(b[0], 4);
  CHECK_EQ(b[3], 1);
  writeI32le(INT32_MIN, b);
  CHECK_EQ(readI32le(b), INT32_MIN);
  const uint8_t u[2] = {0x34, 0x12};
  CHECK_EQ(readU16le(u), 0x1234);
  const uint8_t hi[2] = {0xFF, 0xFF};
  CHECK_EQ(readU16le(hi), 0xFFFF);
}

EPDJ_TEST(random_frames_round_trip) {
  Rng rng(2024);
  for (int i = 0; i < 3000; ++i) {
    Bytes payload(rng.below(kMaxPayload + 1));
    for (auto& b : payload) b = static_cast<uint8_t>(rng.next() >> 9);
    const uint8_t deck = rng.below(2) ? 127 : static_cast<uint8_t>(rng.below(16));
    const uint8_t cmd = static_cast<uint8_t>(rng.below(128));
    const Bytes fr = frameOf(deck, cmd, payload);
    CHECK(!fr.empty());
    Frame f;
    CHECK(decodeFrame(fr.data(), fr.size(), kNoDeckFilter, &f) == DecodeStatus::Ok);
    CHECK_EQ(f.deck, deck);
    CHECK_EQ(f.cmd, cmd);
    CHECK_BYTES(payloadOf(f), payload);
    for (size_t k = 1; k + 1 < fr.size(); ++k) CHECK(fr[k] < 0x80);
  }
}

EPDJ_TEST(decode_never_crashes_on_garbage) {
  Rng rng(77);
  uint32_t ok = 0;
  for (int i = 0; i < 100000; ++i) {
    Bytes m(rng.below(140));
    for (auto& b : m) b = static_cast<uint8_t>(rng.next() >> 7);
    if (!m.empty() && rng.below(2)) m.front() = 0xF0;
    if (m.size() > 3 && rng.below(2)) { m[1] = 0x7D; m[2] = 0x45; m[3] = 0x44; }
    if (m.size() > 4 && rng.below(2)) m[4] = 1;
    if (m.size() > 1 && rng.below(2)) m.back() = 0xF7;
    Frame f;
    if (decodeFrame(m.data(), m.size(), static_cast<uint8_t>(rng.below(2)), &f) == DecodeStatus::Ok) ++ok;
  }
  CHECK(ok < 100000);
}
