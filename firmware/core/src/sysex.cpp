#include "epdj/sysex.h"

namespace epdj {
namespace syx {

bool pack8to7(const uint8_t* in, size_t n, uint8_t* out, size_t cap, size_t* written) {
  if (cap < packedSize(n)) return false;
  size_t o = 0;
  for (size_t i = 0; i < n; i += 7) {
    const size_t g = n - i < 7 ? n - i : 7;
    uint8_t header = 0;
    for (size_t k = 0; k < g; ++k) header = static_cast<uint8_t>(header | ((in[i + k] >> 7) << k));
    out[o++] = header;
    for (size_t k = 0; k < g; ++k) out[o++] = static_cast<uint8_t>(in[i + k] & 0x7F);
  }
  *written = o;
  return true;
}

bool unpack7to8(const uint8_t* in, size_t n, uint8_t* out, size_t cap, size_t* written) {
  const size_t rawLen = unpackedSize(n);
  if (rawLen == kInvalidSize || cap < rawLen) return false;
  for (size_t i = 0; i < n; ++i) {
    if (in[i] & 0x80) return false;
  }
  size_t o = 0;
  for (size_t i = 0; i < n; i += 8) {
    const uint8_t header = in[i];
    const size_t g = n - i - 1 < 7 ? n - i - 1 : 7;
    for (size_t k = 0; k < g; ++k) out[o++] = static_cast<uint8_t>(in[i + 1 + k] | (((header >> k) & 1) << 7));
  }
  *written = o;
  return true;
}

uint8_t checksum(const uint8_t* data, size_t n) {
  uint32_t sum = 0;
  for (size_t i = 0; i < n; ++i) sum += data[i];
  return static_cast<uint8_t>((0u - sum) & 0x7Fu);
}

size_t encodeFrame(uint8_t deck, uint8_t cmd, const uint8_t* payload, size_t payloadLen, uint8_t* out,
                   size_t cap) {
  if (payloadLen > kMaxPayload) return 0;
  const size_t total = kHeaderBytes + packedSize(payloadLen) + 2;
  if (cap < total) return 0;
  out[0] = 0xF0;
  out[1] = proto::sysex::kManufacturerId;
  out[2] = proto::sysex::kModelId0;
  out[3] = proto::sysex::kModelId1;
  out[4] = proto::sysex::kVersion;
  out[5] = static_cast<uint8_t>(deck & 0x7F);
  out[6] = static_cast<uint8_t>(cmd & 0x7F);
  size_t packed = 0;
  if (!pack8to7(payload, payloadLen, out + kHeaderBytes, cap - kHeaderBytes, &packed)) return 0;
  const size_t chkAt = kHeaderBytes + packed;
  out[chkAt] = checksum(out + 4, chkAt - 4);
  out[chkAt + 1] = 0xF7;
  return chkAt + 2;
}

DecodeStatus decodeFrame(const uint8_t* msg, size_t len, uint8_t myDeck, Frame* out) {
  if (len < kHeaderBytes + 2) return DecodeStatus::TooShort;
  if (msg[0] != 0xF0 || msg[len - 1] != 0xF7 || msg[1] != proto::sysex::kManufacturerId ||
      msg[2] != proto::sysex::kModelId0 || msg[3] != proto::sysex::kModelId1) {
    return DecodeStatus::Malformed;
  }
  for (size_t i = 1; i + 1 < len; ++i) {
    if (msg[i] & 0x80) return DecodeStatus::Malformed;
  }
  if (msg[4] != proto::sysex::kVersion) return DecodeStatus::BadVersion;
  if (checksum(msg + 4, len - 6) != msg[len - 2]) return DecodeStatus::BadChecksum;
  const uint8_t deck = msg[5];
  if (deck > 15 && deck != proto::sysex::kDeckAny) return DecodeStatus::BadDeck;

  out->deck = deck;
  out->cmd = msg[6];
  out->payloadLen = 0;
  if (myDeck != kNoDeckFilter && deck != proto::sysex::kDeckAny && deck != myDeck) {
    return DecodeStatus::NotForUs;
  }
  const size_t packedLen = len - 2 - kHeaderBytes;
  const size_t rawLen = unpackedSize(packedLen);
  if (rawLen == kInvalidSize) return DecodeStatus::BadPayload;
  if (rawLen > kMaxPayload) return DecodeStatus::TooLong;
  size_t n = 0;
  if (!unpack7to8(msg + kHeaderBytes, packedLen, out->payload, sizeof(out->payload), &n)) {
    return DecodeStatus::BadPayload;
  }
  out->payloadLen = n;
  return DecodeStatus::Ok;
}

size_t encodeAck(uint8_t deck, uint8_t cmd, uint8_t* out, size_t cap) {
  const uint8_t p[1] = {cmd};
  return encodeFrame(deck, proto::sysex::kAck, p, 1, out, cap);
}

size_t encodeNak(uint8_t deck, uint8_t cmd, proto::sysex::Err err, uint8_t* out, size_t cap) {
  const uint8_t p[2] = {cmd, static_cast<uint8_t>(err)};
  return encodeFrame(deck, proto::sysex::kNak, p, 2, out, cap);
}

size_t encodeInfo(uint8_t deck, const proto::Info& v, uint8_t* out, size_t cap) {
  uint8_t p[proto::Info::kBytes];
  proto::serialize(v, p);
  return encodeFrame(deck, static_cast<uint8_t>(proto::sysex::Reply::Info), p, sizeof(p), out, cap);
}

size_t encodeConfigValue(uint8_t deck, uint8_t key, int32_t value, uint8_t* out, size_t cap) {
  uint8_t p[5];
  p[0] = key;
  writeI32le(value, p + 1);
  return encodeFrame(deck, static_cast<uint8_t>(proto::sysex::Reply::Config), p, sizeof(p), out, cap);
}

size_t encodeTelemetry(uint8_t deck, const proto::Telemetry& v, uint8_t* out, size_t cap) {
  uint8_t p[proto::Telemetry::kBytes];
  proto::serialize(v, p);
  return encodeFrame(deck, static_cast<uint8_t>(proto::sysex::Reply::Telemetry), p, sizeof(p), out, cap);
}

size_t encodeCalResult(uint8_t deck, const proto::CalResult& v, uint8_t* out, size_t cap) {
  uint8_t p[proto::CalResult::kBytes];
  proto::serialize(v, p);
  return encodeFrame(deck, static_cast<uint8_t>(proto::sysex::Reply::CalResult), p, sizeof(p), out, cap);
}

size_t encodeLink(uint8_t deck, const proto::Peer* peers, size_t count, uint8_t* out, size_t cap) {
  if (count > kMaxPeers) count = kMaxPeers;
  uint8_t p[1 + kMaxPeers * proto::Peer::kBytes];
  p[0] = static_cast<uint8_t>(count);
  for (size_t i = 0; i < count; ++i) proto::serialize(peers[i], p + 1 + i * proto::Peer::kBytes);
  return encodeFrame(deck, static_cast<uint8_t>(proto::sysex::Reply::Link), p,
                     1 + count * proto::Peer::kBytes, out, cap);
}

}  // namespace syx
}  // namespace epdj
