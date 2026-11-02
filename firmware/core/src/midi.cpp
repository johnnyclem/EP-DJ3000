#include "epdj/midi.h"

namespace epdj {
namespace midi {

size_t cc14(uint8_t ch, uint8_t msbCc, uint8_t lsbCc, uint16_t value14, int32_t prev14, Message out[2]) {
  const uint16_t v = value14 > kMax14 ? static_cast<uint16_t>(kMax14) : value14;
  const bool known = prev14 >= 0 && prev14 <= kMax14;
  const uint16_t p = known ? static_cast<uint16_t>(prev14) : 0;
  size_t n = 0;
  if (!known || msb14(v) != msb14(p)) out[n++] = controlChange(ch, msbCc, msb14(v));
  if (!known || lsb14(v) != lsb14(p)) out[n++] = controlChange(ch, lsbCc, lsb14(v));
  return n;
}

// ---- USB-MIDI ----

namespace {
// Valid MIDI bytes per Code Index Number (USB-MIDI 1.0 table 4-1). CIN 0 and 1 are reserved.
constexpr uint8_t kCinLength[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
}  // namespace

size_t UsbPacket::size() const { return kCinLength[cin & 0x0F]; }

void UsbPacket::toBytes(uint8_t out[4]) const {
  out[0] = static_cast<uint8_t>(((cable & 0x0F) << 4) | (cin & 0x0F));
  out[1] = data[0];
  out[2] = data[1];
  out[3] = data[2];
}

UsbPacket UsbPacket::fromBytes(const uint8_t in[4]) {
  UsbPacket p;
  p.cable = static_cast<uint8_t>(in[0] >> 4);
  p.cin = static_cast<uint8_t>(in[0] & 0x0F);
  p.data[0] = in[1];
  p.data[1] = in[2];
  p.data[2] = in[3];
  return p;
}

bool UsbPacket::operator==(const UsbPacket& o) const {
  return cable == o.cable && cin == o.cin && data[0] == o.data[0] && data[1] == o.data[1] &&
         data[2] == o.data[2];
}

bool encodePacket(uint8_t cable, const Message& m, UsbPacket* out) {
  if (m.len == 0) return false;
  uint8_t cin = 0;
  if (m.isChannelVoice()) {
    cin = static_cast<uint8_t>(m.b[0] >> 4);
  } else {
    switch (m.b[0]) {
      case 0xF1: case 0xF3: cin = 2; break;
      case 0xF2: cin = 3; break;
      case 0xF6: cin = 5; break;
      default:
        if (m.b[0] < 0xF8) return false;  // SysEx delimiters and undefined system common
        cin = 0x0F;
        break;
    }
  }
  out->cable = static_cast<uint8_t>(cable & 0x0F);
  out->cin = cin;
  const size_t n = out->size();
  for (size_t i = 0; i < 3; ++i) out->data[i] = i < n ? m.b[i] : 0;
  return true;
}

size_t encodeSysex(uint8_t cable, const uint8_t* msg, size_t len, UsbPacket* out, size_t maxPackets) {
  if (len < 2 || msg[0] != kSysExStart || msg[len - 1] != kSysExEnd) return 0;
  for (size_t i = 1; i + 1 < len; ++i) {
    if (msg[i] & 0x80) return 0;
  }
  const size_t packets = (len + 2) / 3;
  if (packets > maxPackets) return 0;
  for (size_t p = 0; p < packets; ++p) {
    const size_t off = p * 3;
    const size_t n = len - off < 3 ? len - off : 3;
    UsbPacket& pk = out[p];
    pk.cable = static_cast<uint8_t>(cable & 0x0F);
    pk.cin = (p + 1 < packets) ? 4 : static_cast<uint8_t>(4 + n);  // 5, 6 or 7 on the final packet
    for (size_t i = 0; i < 3; ++i) pk.data[i] = i < n ? msg[off + i] : 0;
  }
  return packets;
}

// ---- Parser ----

namespace {
// Data bytes that complete a message with this status (0 = nothing to wait for).
uint8_t dataBytesFor(uint8_t status) {
  if (status >= 0x80 && status < 0xF0) return (status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0 ? 1 : 2;
  switch (status) {
    case 0xF1: case 0xF3: return 1;
    case 0xF2: return 2;
    default: return 0;
  }
}
}  // namespace

void Parser::reset() {
  len_ = 0;
  state_ = State::Idle;
  status_ = 0;
  need_ = 0;
  have_ = 0;
}

bool Parser::feed(uint8_t byte, Event* ev) {
  ev->kind = Event::Kind::None;

  if (byte >= 0xF8) {  // real-time: never touches the parser state
    if (byte == 0xF9 || byte == 0xFD) return false;
    ev->kind = Event::Kind::Message;
    ev->message = make(byte, 0, 0, 1);
    return true;
  }

  if (byte & 0x80) {
    // Any status byte ends whatever was being assembled. A SysEx cut short here is dropped.
    const bool wasSysEx = state_ == State::SysEx;
    state_ = State::Idle;
    have_ = 0;
    if (byte == kSysExEnd) {
      status_ = 0;
      if (wasSysEx) {  // room for F7 is reserved while appending
        buf_[len_++] = kSysExEnd;
        ev->kind = Event::Kind::SysEx;
        ev->sysex = buf_;
        ev->sysexLen = len_;
        return true;
      }
      return false;  // lone F7, or the end of a dropped SysEx
    }
    if (byte == kSysExStart) {
      status_ = 0;
      len_ = 0;
      if (cap_ >= 2) {
        buf_[len_++] = kSysExStart;
        state_ = State::SysEx;
      } else {
        ++dropped_;
        state_ = State::SysExDrop;
      }
      return false;
    }
    if (byte == 0xF6) {  // tune request: complete on its own
      status_ = 0;
      ev->kind = Event::Kind::Message;
      ev->message = make(byte, 0, 0, 1);
      return true;
    }
    need_ = dataBytesFor(byte);
    if (need_ == 0) {  // F4, F5
      status_ = 0;
      return false;
    }
    status_ = byte;
    state_ = State::Data;
    return false;
  }

  switch (state_) {
    case State::SysEx:
      if (len_ + 2 > cap_) {  // this byte and the closing F7 would not fit
        ++dropped_;
        len_ = 0;
        state_ = State::SysExDrop;
      } else {
        buf_[len_++] = byte;
      }
      return false;
    case State::Data:
      data_[have_++] = byte;
      if (have_ < need_) return false;
      have_ = 0;
      ev->kind = Event::Kind::Message;
      ev->message = make(status_, data_[0], need_ == 2 ? data_[1] : 0, static_cast<uint8_t>(need_ + 1));
      if (status_ >= 0xF0) {  // system common: no running status
        status_ = 0;
        state_ = State::Idle;
      }
      return true;
    case State::Idle:
    case State::SysExDrop:
    default:
      return false;
  }
}

}  // namespace midi
}  // namespace epdj
