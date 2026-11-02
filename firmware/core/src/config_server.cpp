#include "epdj/config_server.h"

namespace epdj {

using proto::sysex::Cmd;
using proto::sysex::Err;

// ---- ReplyQueue ----

bool ReplyQueue::send(const uint8_t* frame, size_t len) {
  if (len == 0 || len > 255 || used_ + len + 1 > kCapacityBytes) {
    ++dropped_;
    return false;
  }
  buf_[tail_] = static_cast<uint8_t>(len);
  tail_ = (tail_ + 1) % kCapacityBytes;
  for (size_t i = 0; i < len; ++i) {
    buf_[tail_] = frame[i];
    tail_ = (tail_ + 1) % kCapacityBytes;
  }
  used_ += len + 1;
  return true;
}

size_t ReplyQueue::pop(uint8_t* out, size_t cap) {
  if (used_ == 0) return 0;
  const size_t len = buf_[head_];
  if (cap < len) return 0;
  head_ = (head_ + 1) % kCapacityBytes;
  for (size_t i = 0; i < len; ++i) {
    out[i] = buf_[head_];
    head_ = (head_ + 1) % kCapacityBytes;
  }
  used_ -= len + 1;
  return len;
}

// ---- ConfigServer ----

namespace {
constexpr uint8_t kAllKeys = 0xFF;
constexpr uint16_t kToneMinHz = 20, kToneMaxHz = 20000;
constexpr uint8_t kToneMaxAttenDb = 96;
}  // namespace

void ConfigServer::ack(uint8_t cmd) {
  uint8_t buf[syx::kMaxFrameBytes];
  reply(buf, syx::encodeAck(replyDeck_, cmd, buf, sizeof(buf)));
}

void ConfigServer::nak(uint8_t cmd, Err err) {
  uint8_t buf[syx::kMaxFrameBytes];
  reply(buf, syx::encodeNak(replyDeck_, cmd, err, buf, sizeof(buf)));
}

void ConfigServer::sendConfigValue(uint8_t key) {
  uint8_t buf[syx::kMaxFrameBytes];
  reply(buf, syx::encodeConfigValue(replyDeck_, key, config_.get(static_cast<proto::ConfigKey>(key)), buf,
                                    sizeof(buf)));
}

ConfigServer::Result ConfigServer::handle(const uint8_t* msg, size_t len, uint64_t nowUs) {
  replyDeck_ = deck();
  syx::Frame f;
  switch (syx::decodeFrame(msg, len, replyDeck_, &f)) {
    case syx::DecodeStatus::Ok:
      break;
    case syx::DecodeStatus::NotForUs:
      return Result::NotForUs;
    case syx::DecodeStatus::BadPayload:
    case syx::DecodeStatus::TooLong:
      if (f.cmd >= proto::sysex::kReplyFlag) return Result::Ignored;
      nak(f.cmd, Err::BadLength);
      return Result::Replied;
    default:
      return Result::Ignored;
  }
  if (f.cmd >= proto::sysex::kReplyFlag) return Result::Ignored;

  switch (static_cast<Cmd>(f.cmd)) {
    case Cmd::GetInfo: getInfo(f); break;
    case Cmd::GetConfig: getConfig(f); break;
    case Cmd::SetConfig: setConfig(f); break;
    case Cmd::SaveConfig: saveConfig(f); break;
    case Cmd::LoadDefaults: loadDefaults(f); break;
    case Cmd::Stream: stream(f, nowUs); break;
    case Cmd::CalBegin: calBegin(f); break;
    case Cmd::CalEnd: calEnd(f); break;
    case Cmd::SetDeck: setDeck(f); break;
    case Cmd::Reboot: rebootCmd(f); break;
    case Cmd::LinkStatus: linkStatus(f); break;
    case Cmd::TestTone: testTone(f); break;
    case Cmd::Identify: identify(f); break;
    default: nak(f.cmd, Err::UnknownCmd); break;
  }
  return Result::Replied;
}

// Every command checks its exact payload length first, so a malformed request never has side effects.
#define EPDJ_REQUIRE_LEN(f, n)                         \
  do {                                                 \
    if ((f).payloadLen != (n)) {                       \
      nak((f).cmd, Err::BadLength);                    \
      return;                                          \
    }                                                  \
  } while (0)

void ConfigServer::getInfo(const syx::Frame& f) {
  EPDJ_REQUIRE_LEN(f, 0u);
  proto::Info info;
  actions_.getInfo(&info);
  info.protocolVersion = proto::kProtocolVersion;
  info.deck = static_cast<uint8_t>(config_.get(proto::ConfigKey::DeckId));
  info.mode = static_cast<uint8_t>(config_.get(proto::ConfigKey::Mode));
  info.platterSource = static_cast<uint8_t>(config_.get(proto::ConfigKey::PlatterSource));
  uint8_t buf[syx::kMaxFrameBytes];
  reply(buf, syx::encodeInfo(replyDeck_, info, buf, sizeof(buf)));
}

void ConfigServer::getConfig(const syx::Frame& f) {
  EPDJ_REQUIRE_LEN(f, 1u);
  const uint8_t key = f.payload[0];
  if (key == kAllKeys) {
    for (size_t i = 0; i < Config::kCount; ++i) sendConfigValue(static_cast<uint8_t>(i));
  } else if (key < Config::kCount) {
    sendConfigValue(key);
  } else {
    nak(f.cmd, Err::BadKey);
  }
}

void ConfigServer::setConfig(const syx::Frame& f) {
  EPDJ_REQUIRE_LEN(f, 5u);
  const uint8_t key = f.payload[0];
  switch (config_.set(static_cast<proto::ConfigKey>(key), syx::readI32le(f.payload + 1))) {
    case ConfigStatus::Ok: ack(f.cmd); break;
    case ConfigStatus::BadKey: nak(f.cmd, Err::BadKey); break;
    case ConfigStatus::OutOfRange: nak(f.cmd, Err::OutOfRange); break;
  }
}

void ConfigServer::saveConfig(const syx::Frame& f) {
  EPDJ_REQUIRE_LEN(f, 0u);
  if (actions_.saveConfig(config_)) {
    ack(f.cmd);
  } else {
    nak(f.cmd, Err::FlashError);
  }
}

void ConfigServer::loadDefaults(const syx::Frame& f) {
  EPDJ_REQUIRE_LEN(f, 0u);
  config_.setDefaults();
  ack(f.cmd);
}

void ConfigServer::stream(const syx::Frame& f, uint64_t nowUs) {
  EPDJ_REQUIRE_LEN(f, 1u);
  const uint8_t requested = f.payload[0];
  if (requested == 0) {
    streaming_ = false;
  } else {
    const uint8_t rate = requested > kMaxStreamHz ? kMaxStreamHz : requested;
    if (!streaming_ || rate != rateHz_) nextDueUs_ = nowUs;  // a plain refresh keeps the phase
    streaming_ = true;
    rateHz_ = rate;
    expiresUs_ = nowUs + kStreamTimeoutUs;
  }
  ack(f.cmd);
}

void ConfigServer::tick(uint64_t nowUs) {
  if (!streaming_) return;
  if (nowUs >= expiresUs_) {
    streaming_ = false;
    return;
  }
  if (nowUs < nextDueUs_) return;
  const uint64_t period = 1000000u / rateHz_;
  nextDueUs_ += period;
  if (nextDueUs_ <= nowUs) nextDueUs_ = nowUs + period;  // late by a period or more: skip, never burst

  replyDeck_ = deck();
  proto::Telemetry t;
  actions_.getTelemetry(&t);
  t.seq = seq_++;
  uint8_t buf[syx::kMaxFrameBytes];
  reply(buf, syx::encodeTelemetry(replyDeck_, t, buf, sizeof(buf)));
}

void ConfigServer::calBegin(const syx::Frame& f) {
  EPDJ_REQUIRE_LEN(f, 1u);
  const uint8_t kind = f.payload[0];
  if (cal_ == nullptr || kind > static_cast<uint8_t>(proto::CalKind::Knob) ||
      !cal_->begin(static_cast<proto::CalKind>(kind))) {
    nak(f.cmd, Err::NotSupported);
    return;
  }
  ack(f.cmd);
}

void ConfigServer::calEnd(const syx::Frame& f) {
  EPDJ_REQUIRE_LEN(f, 2u);
  const uint8_t kind = f.payload[0];
  const uint8_t apply = f.payload[1];
  if (apply > 1) {
    nak(f.cmd, Err::OutOfRange);
    return;
  }
  if (cal_ == nullptr || kind > static_cast<uint8_t>(proto::CalKind::Knob)) {
    nak(f.cmd, Err::CalNotActive);
    return;
  }
  uint16_t rawMin = 0, rawMax = 0;
  switch (cal_->end(static_cast<proto::CalKind>(kind), &rawMin, &rawMax)) {
    case CalibrationHost::EndStatus::Ok: break;
    case CalibrationHost::EndStatus::NotActive: nak(f.cmd, Err::CalNotActive); return;
    case CalibrationHost::EndStatus::Failed: nak(f.cmd, Err::CalFailed); return;
  }
  const bool isFader = kind == static_cast<uint8_t>(proto::CalKind::Fader);
  const proto::ConfigKey minKey = isFader ? proto::ConfigKey::FaderRawMin : proto::ConfigKey::KnobRawMin;
  const proto::ConfigKey maxKey = isFader ? proto::ConfigKey::FaderRawMax : proto::ConfigKey::KnobRawMax;
  if (apply == 1) {
    // Both values are checked before either is written, so a bad measurement changes nothing.
    if (Config::validate(static_cast<uint8_t>(minKey), rawMin) != ConfigStatus::Ok ||
        Config::validate(static_cast<uint8_t>(maxKey), rawMax) != ConfigStatus::Ok) {
      nak(f.cmd, Err::CalFailed);
      return;
    }
    config_.set(minKey, rawMin);
    config_.set(maxKey, rawMax);
  }
  proto::CalResult r;
  r.kind = kind;
  r.applied = apply;
  r.rawMin = rawMin;
  r.rawMax = rawMax;
  uint8_t buf[syx::kMaxFrameBytes];
  reply(buf, syx::encodeCalResult(replyDeck_, r, buf, sizeof(buf)));
}

void ConfigServer::setDeck(const syx::Frame& f) {
  EPDJ_REQUIRE_LEN(f, 1u);
  const int32_t d = f.payload[0];
  if (config_.set(proto::ConfigKey::DeckId, d) != ConfigStatus::Ok) {
    nak(f.cmd, Err::OutOfRange);
    return;
  }
  ack(f.cmd);
  actions_.setDeck(static_cast<proto::Deck>(d));
}

void ConfigServer::rebootCmd(const syx::Frame& f) {
  EPDJ_REQUIRE_LEN(f, 1u);
  const uint8_t mode = f.payload[0];
  if (mode > static_cast<uint8_t>(proto::RebootMode::Bootloader)) {
    nak(f.cmd, Err::OutOfRange);
    return;
  }
  ack(f.cmd);
  actions_.reboot(static_cast<proto::RebootMode>(mode));
}

void ConfigServer::linkStatus(const syx::Frame& f) {
  EPDJ_REQUIRE_LEN(f, 0u);
  proto::Peer peers[syx::kMaxPeers];
  size_t n = actions_.getPeers(peers, syx::kMaxPeers);
  if (n > syx::kMaxPeers) n = syx::kMaxPeers;
  uint8_t buf[syx::kMaxFrameBytes];
  reply(buf, syx::encodeLink(replyDeck_, peers, n, buf, sizeof(buf)));
}

void ConfigServer::testTone(const syx::Frame& f) {
  EPDJ_REQUIRE_LEN(f, 4u);
  const uint8_t on = f.payload[0];
  const uint16_t freq = syx::readU16le(f.payload + 1);
  const uint8_t atten = f.payload[3];
  if (on > 1 || (on == 1 && (freq < kToneMinHz || freq > kToneMaxHz || atten > kToneMaxAttenDb))) {
    nak(f.cmd, Err::OutOfRange);
    return;
  }
  if (!actions_.testTone(on == 1, freq, atten)) {
    nak(f.cmd, Err::NotSupported);
    return;
  }
  ack(f.cmd);
}

void ConfigServer::identify(const syx::Frame& f) {
  EPDJ_REQUIRE_LEN(f, 1u);
  actions_.identify(f.payload[0]);
  ack(f.cmd);
}

#undef EPDJ_REQUIRE_LEN

}  // namespace epdj
