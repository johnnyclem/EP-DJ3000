// MIDI 1.0 message value type, builders, USB-MIDI 1.0 event packets and a streaming byte parser.
// No heap, no exceptions; every loop is bounded by its input.
#pragma once
#include <cstddef>
#include <cstdint>

namespace epdj {
namespace midi {

constexpr uint8_t kNoteOff = 0x80;
constexpr uint8_t kNoteOn = 0x90;
constexpr uint8_t kControlChange = 0xB0;
constexpr uint8_t kProgramChange = 0xC0;
constexpr uint8_t kSysExStart = 0xF0;
constexpr uint8_t kSysExEnd = 0xF7;

constexpr int32_t kMax14 = 16383;

// One MIDI message of 1..3 bytes (everything except SysEx). len == 0 is the empty/invalid message.
struct Message {
  uint8_t b[3] = {0, 0, 0};
  uint8_t len = 0;

  constexpr uint8_t status() const { return b[0]; }
  constexpr uint8_t data1() const { return b[1]; }
  constexpr uint8_t data2() const { return b[2]; }
  constexpr bool empty() const { return len == 0; }
  constexpr bool isChannelVoice() const { return len != 0 && b[0] >= 0x80 && b[0] < 0xF0; }
  constexpr bool isRealTime() const { return len == 1 && b[0] >= 0xF8; }
  // Channel 0..15 (meaningful for channel-voice messages only).
  constexpr uint8_t channel() const { return static_cast<uint8_t>(b[0] & 0x0F); }
  // Status with the channel stripped: 0x90 for any Note On, 0xF8 for Clock.
  constexpr uint8_t type() const { return b[0] < 0xF0 ? static_cast<uint8_t>(b[0] & 0xF0) : b[0]; }
  // Note On with velocity 0 is a Note Off (PROTOCOL.md 2.1: receivers MUST accept both).
  constexpr bool isNoteOn() const { return len == 3 && type() == kNoteOn && b[2] != 0; }
  constexpr bool isNoteOff() const {
    return len == 3 && (type() == kNoteOff || (type() == kNoteOn && b[2] == 0));
  }
  constexpr bool isControlChange() const { return len == 3 && type() == kControlChange; }

  constexpr bool operator==(const Message& o) const {
    if (len != o.len) return false;
    for (uint8_t i = 0; i < len; ++i) {
      if (b[i] != o.b[i]) return false;
    }
    return true;
  }
  constexpr bool operator!=(const Message& o) const { return !(*this == o); }
};

constexpr Message make(uint8_t status, uint8_t d1, uint8_t d2, uint8_t len) {
  Message m;
  m.b[0] = status;
  m.b[1] = static_cast<uint8_t>(d1 & 0x7F);
  m.b[2] = static_cast<uint8_t>(d2 & 0x7F);
  m.len = len;
  return m;
}

// ---- builders (channel is masked to 0..15, data bytes to 0..127) ----
constexpr Message noteOn(uint8_t ch, uint8_t note, uint8_t vel) {
  return make(static_cast<uint8_t>(kNoteOn | (ch & 0x0F)), note, vel, 3);
}
constexpr Message noteOff(uint8_t ch, uint8_t note, uint8_t vel = 0) {
  return make(static_cast<uint8_t>(kNoteOff | (ch & 0x0F)), note, vel, 3);
}
constexpr Message controlChange(uint8_t ch, uint8_t cc, uint8_t value) {
  return make(static_cast<uint8_t>(kControlChange | (ch & 0x0F)), cc, value, 3);
}
constexpr Message programChange(uint8_t ch, uint8_t program) {
  return make(static_cast<uint8_t>(kProgramChange | (ch & 0x0F)), program, 0, 2);
}
// System real-time (0xF8 Clock, 0xFA Start, 0xFB Continue, 0xFC Stop).
constexpr Message clock() { return make(0xF8, 0, 0, 1); }
constexpr Message start() { return make(0xFA, 0, 0, 1); }
constexpr Message cont() { return make(0xFB, 0, 0, 1); }
constexpr Message stop() { return make(0xFC, 0, 0, 1); }
// Song Position Pointer in MIDI beats (6 clocks); wraps at 14 bits. F2 lsb msb.
constexpr Message songPosition(uint16_t beats) {
  return make(0xF2, static_cast<uint8_t>(beats & 0x7F), static_cast<uint8_t>((beats >> 7) & 0x7F), 3);
}
// MTC quarter frame: `data` is the 0nnndddd byte.
constexpr Message mtcQuarterFrame(uint8_t data) { return make(0xF1, data, 0, 2); }

// ---- value encodings (PROTOCOL.md 2.1) ----
constexpr uint8_t msb14(uint16_t v14) { return static_cast<uint8_t>((v14 >> 7) & 0x7F); }
constexpr uint8_t lsb14(uint16_t v14) { return static_cast<uint8_t>(v14 & 0x7F); }
constexpr uint16_t join14(uint8_t msb, uint8_t lsb) {
  return static_cast<uint16_t>(((msb & 0x7F) << 7) | (lsb & 0x7F));
}

// Offset-64 relative value: 64 + clamp(delta, -63, +63). 64 = no change; 0 is never produced.
constexpr uint8_t encodeRelative(int32_t delta) {
  return static_cast<uint8_t>(64 + (delta > 63 ? 63 : (delta < -63 ? -63 : delta)));
}
// Inverse for received values (a received 0 decodes to -64 literally).
constexpr int32_t decodeRelative(uint8_t value) { return static_cast<int32_t>(value & 0x7F) - 64; }

// 14-bit CC pair, MSB first then LSB. `prev14` is the value the receiver already has (-1 = unknown):
// a half equal to the same half of prev14 is skipped (PROTOCOL.md 2.1), so 0..2 messages are written
// to out. Pass -1 to force both halves. value14 is clamped to 0..16383. Returns the count.
size_t cc14(uint8_t ch, uint8_t msbCc, uint8_t lsbCc, uint16_t value14, int32_t prev14, Message out[2]);

// Reassembles a 14-bit value from the separate MSB and LSB control changes of a pair. Each half
// updates the value as it arrives (MSB with the last LSB, LSB with the last MSB), which is what
// senders that skip an unchanged half rely on. A sender that changes both halves therefore produces
// one intermediate value between its two messages; consumers should apply values once per engine tick.
class Cc14Receiver {
 public:
  constexpr Cc14Receiver(uint8_t msbCc, uint8_t lsbCc) : msbCc_(msbCc), lsbCc_(lsbCc) {}

  // True if `cc` is one of the pair; *out then holds the pair's new 14-bit value.
  bool onControlChange(uint8_t cc, uint8_t value, uint16_t* out) {
    if (cc == msbCc_) {
      msb_ = static_cast<uint8_t>(value & 0x7F);
    } else if (cc == lsbCc_) {
      lsb_ = static_cast<uint8_t>(value & 0x7F);
    } else {
      return false;
    }
    *out = join14(msb_, lsb_);
    return true;
  }

 private:
  uint8_t msbCc_, lsbCc_;
  uint8_t msb_ = 0, lsb_ = 0;
};

// ---- USB-MIDI 1.0 event packets (4 bytes: cable<<4|CIN, 3 data bytes) ----
struct UsbPacket {
  uint8_t cable = 0;  // 0..15
  uint8_t cin = 0;    // code index number 0..15
  uint8_t data[3] = {0, 0, 0};

  // Number of valid MIDI bytes in data[] implied by the CIN (0 for the reserved CINs 0 and 1).
  size_t size() const;
  void toBytes(uint8_t out[4]) const;
  static UsbPacket fromBytes(const uint8_t in[4]);
  bool operator==(const UsbPacket& o) const;
};

// Packet for any non-SysEx message. False for an empty message, SysEx delimiters and bytes with no CIN.
bool encodePacket(uint8_t cable, const Message& m, UsbPacket* out);

// Packets for one complete SysEx message (F0 ... F7, interior bytes < 0x80): CIN 4 for every full
// 3-byte chunk, 5/6/7 for the final chunk of 1/2/3 bytes. Returns the packet count, or 0 if the
// message is malformed or does not fit in maxPackets.
size_t encodeSysex(uint8_t cable, const uint8_t* msg, size_t len, UsbPacket* out, size_t maxPackets);

// Streaming parser for MIDI byte streams (UART MIDI, or the bytes of USB packets).
//
//  * Running status for channel-voice messages; system common clears it; real-time bytes never do.
//  * Real-time bytes are delivered immediately, also in the middle of a message or SysEx.
//  * SysEx is assembled into the caller's buffer (F0 and F7 included). A message that does not fit
//    is dropped whole and counted; the parser resyncs on the next status byte. A SysEx cut short by
//    any other status byte is dropped silently.
//  * Stray data bytes, F4/F5/F9/FD and a lone F7 are ignored. Any byte sequence is safe.
//  * Keep one Parser (and one SysEx buffer) per input stream: per USB cable, per UART.
class Parser {
 public:
  struct Event {
    enum class Kind : uint8_t { None, Message, SysEx };
    Kind kind = Kind::None;
    Message message;                 // Kind::Message
    const uint8_t* sysex = nullptr;  // Kind::SysEx: F0 ... F7, valid until the next feed()
    size_t sysexLen = 0;
  };

  // sysexBuf may be null with sysexCap 0 (every SysEx is then dropped).
  Parser(uint8_t* sysexBuf, size_t sysexCap) : buf_(sysexBuf), cap_(sysexCap) {}

  // At most one event per byte. Returns true if *ev was filled.
  bool feed(uint8_t byte, Event* ev);

  // Feeds the valid bytes of a USB-MIDI packet; onEvent(const Event&) runs before the next byte is
  // consumed, so a SysEx pointer is always still valid inside the callback.
  template <typename F>
  void feed(const UsbPacket& p, F&& onEvent) {
    const size_t n = p.size();
    for (size_t i = 0; i < n; ++i) {
      Event ev;
      if (feed(p.data[i], &ev)) onEvent(ev);
    }
  }

  uint32_t droppedSysex() const { return dropped_; }
  void reset();

 private:
  enum class State : uint8_t { Idle, Data, SysEx, SysExDrop };

  uint8_t* buf_;
  size_t cap_;
  size_t len_ = 0;
  uint32_t dropped_ = 0;
  State state_ = State::Idle;
  uint8_t status_ = 0;  // status of the message being assembled; kept for channel voice = running status
  uint8_t need_ = 0;
  uint8_t have_ = 0;
  uint8_t data_[2] = {0, 0};
};

}  // namespace midi
}  // namespace epdj
