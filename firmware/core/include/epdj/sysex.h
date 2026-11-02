// SysEx framing for the configuration protocol (PROTOCOL.md section 3): 8->7 packing, checksum,
// frame encode/decode and reply builders. Pure functions over caller-provided buffers.
//
// Namespace is `syx` (not `sysex`) so it cannot be shadowed by proto::sysex under `using namespace`.
#pragma once
#include <cstddef>
#include <cstdint>

#include "epdj/protocol.h"

namespace epdj {
namespace syx {

// Largest raw (unpacked) payload we send or accept. LINK_STATUS with 8 peers is 81 bytes, the rest are <= 36.
constexpr size_t kMaxPayload = 96;
// F0 7D 45 44 ver deck cmd + packed payload + checksum + F7. Size buffers for sysex messages with this.
constexpr size_t kHeaderBytes = 7;
constexpr size_t kMaxFrameBytes = kHeaderBytes + (kMaxPayload + (kMaxPayload + 6) / 7) + 2;
// Maximum number of Peer structs a LINK reply can carry.
constexpr size_t kMaxPeers = (kMaxPayload - 1) / proto::Peer::kBytes;
// "Do not filter by deck" for decodeFrame.
constexpr uint8_t kNoDeckFilter = 0xFF;

constexpr size_t kInvalidSize = static_cast<size_t>(-1);

// Size of the 7-bit form of rawLen bytes: one header septet per started group of 7.
constexpr size_t packedSize(size_t rawLen) { return rawLen + (rawLen + 6) / 7; }
// Size of the raw form of packedLen septets, kInvalidSize if the length cannot come from packing
// (a header septet with no data byte after it).
constexpr size_t unpackedSize(size_t packedLen) {
  return packedLen % 8 == 1 ? kInvalidSize : packedLen - (packedLen + 7) / 8;
}

// Both return false if out is too small (pack) or the input is not valid 7-bit data of a valid
// length (unpack). *written receives the output length.
bool pack8to7(const uint8_t* in, size_t n, uint8_t* out, size_t cap, size_t* written);
bool unpack7to8(const uint8_t* in, size_t n, uint8_t* out, size_t cap, size_t* written);

// (-(sum of bytes)) & 0x7F, taken over every byte from <ver> through the last payload septet.
uint8_t checksum(const uint8_t* data, size_t n);

// Complete SysEx message F0 ... F7 for an arbitrary raw payload. `cmd` is a request, reply (0x40|cmd),
// ACK (0x7E) or NAK (0x7F) code. Returns the message length, or 0 if the payload exceeds
// kMaxPayload or out is too small.
size_t encodeFrame(uint8_t deck, uint8_t cmd, const uint8_t* payload, size_t payloadLen, uint8_t* out,
                   size_t cap);

struct Frame {
  uint8_t deck = 0;  // 0..15 or 127 (any)
  uint8_t cmd = 0;
  size_t payloadLen = 0;
  uint8_t payload[kMaxPayload] = {};
};

enum class DecodeStatus : uint8_t {
  Ok,
  TooShort,
  Malformed,    // missing F0/F7, wrong manufacturer/model ID, or a status byte inside
  BadVersion,
  BadChecksum,
  BadDeck,      // deck byte is neither 0..15 nor 127
  NotForUs,     // valid frame addressed to another deck
  BadPayload,   // checksum ok but the septets cannot be a packed payload (deck, cmd are filled in)
  TooLong,      // payload larger than kMaxPayload (deck, cmd are filled in)
};

// Validates and decodes one complete SysEx message. With myDeck != kNoDeckFilter, a frame whose deck
// is neither 127 nor myDeck yields NotForUs. `out` is written for every status except TooShort and
// Malformed; deck and cmd are valid from NotForUs onward.
DecodeStatus decodeFrame(const uint8_t* msg, size_t len, uint8_t myDeck, Frame* out);

// ---- reply builders; each returns the message length or 0 on failure (buffer too small) ----
size_t encodeAck(uint8_t deck, uint8_t cmd, uint8_t* out, size_t cap);
size_t encodeNak(uint8_t deck, uint8_t cmd, proto::sysex::Err err, uint8_t* out, size_t cap);
size_t encodeInfo(uint8_t deck, const proto::Info& v, uint8_t* out, size_t cap);
size_t encodeConfigValue(uint8_t deck, uint8_t key, int32_t value, uint8_t* out, size_t cap);
size_t encodeTelemetry(uint8_t deck, const proto::Telemetry& v, uint8_t* out, size_t cap);
size_t encodeCalResult(uint8_t deck, const proto::CalResult& v, uint8_t* out, size_t cap);
// count is clamped to kMaxPeers.
size_t encodeLink(uint8_t deck, const proto::Peer* peers, size_t count, uint8_t* out, size_t cap);

// ---- little-endian helpers for payloads ----
inline int32_t readI32le(const uint8_t* p) {
  return static_cast<int32_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                              (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
}
inline uint16_t readU16le(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline void writeI32le(int32_t v, uint8_t* p) {
  const uint32_t u = static_cast<uint32_t>(v);
  p[0] = static_cast<uint8_t>(u & 0xFF);
  p[1] = static_cast<uint8_t>((u >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((u >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((u >> 24) & 0xFF);
}

}  // namespace syx
}  // namespace epdj
