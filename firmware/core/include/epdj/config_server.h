// Device side of the SysEx configuration protocol (PROTOCOL.md section 3).
//
// Pure logic: one complete SysEx message in, reply frames out through a ReplySink. Everything the
// platform has to do (reboot, persist, play a tone, read sensors) goes through DeviceActions and
// CalibrationHost, which the ESP-IDF layer / engine implement. Not thread-safe: call handle() and
// tick() from one task.
#pragma once
#include <cstddef>
#include <cstdint>

#include "epdj/config.h"
#include "epdj/protocol.h"
#include "epdj/sysex.h"

namespace epdj {

// Receives complete SysEx messages (F0 ... F7) ready to be sent out. A false return means the frame
// was not accepted (queue full) and is lost; the server counts it (ConfigServer::repliesDropped) and
// does not retry.
class ReplySink {
 public:
  virtual bool send(const uint8_t* frame, size_t len) = 0;

 protected:
  ~ReplySink() = default;  // never deleted through the interface: keeps operator delete out of the firmware
};

// Wire size of the frames a GET_CONFIG dump (key 0xFF) produces: one CONFIG frame per key, then the
// ACK[GET_CONFIG] end marker.
constexpr size_t kConfigFrameBytes = syx::kHeaderBytes + syx::packedSize(1 + 4) + 2;  // [key u8, value i32le]
constexpr size_t kAckFrameBytes = syx::kHeaderBytes + syx::packedSize(1) + 2;         // [cmd]
constexpr size_t kConfigDumpBytes = proto::kConfigCount * kConfigFrameBytes + kAckFrameBytes;

// Fixed-capacity FIFO of whole frames, for platforms that can only push a few bytes into the USB
// FIFO per call. The server writes (send); the USB task drains with pop(). Single producer, single
// consumer on the same task, or guard externally.
class ReplyQueue final : public ReplySink {
 public:
  static constexpr size_t kCapacityBytes = 1024;
  // Each frame is stored with a one-byte length.
  static constexpr size_t kDumpRecordBytes = kConfigDumpBytes + proto::kConfigCount + 1;
  // The contract only appends config keys: this fails the build when one more key no longer fits.
  static_assert(kDumpRecordBytes <= kCapacityBytes, "ReplyQueue cannot hold a full GET_CONFIG dump");

  bool send(const uint8_t* frame, size_t len) override;
  // Copies the oldest frame to out and returns its length; 0 if empty or cap < the frame (not popped).
  size_t pop(uint8_t* out, size_t cap);
  bool empty() const { return used_ == 0; }
  uint32_t dropped() const { return dropped_; }
  void clear() { head_ = tail_ = used_ = 0; }

 private:
  uint8_t buf_[kCapacityBytes] = {};  // [len][bytes...] records, circular
  size_t head_ = 0, tail_ = 0, used_ = 0;
  uint32_t dropped_ = 0;
};

// Calibration recorder behind CAL_BEGIN / CAL_END; the engine implements it with CalibrationTracker.
class CalibrationHost {
 public:
  enum class EndStatus : uint8_t { Ok, NotActive, Failed };
  // Start recording extremes of `kind`. False = this control cannot be calibrated (NOT_SUPPORTED).
  virtual bool begin(proto::CalKind kind) = 0;
  // Stop recording. On Ok, *rawMin/*rawMax hold the range to apply (any inset already included).
  // NotActive = nothing was being recorded for `kind`; Failed = the sweep was too small.
  virtual EndStatus end(proto::CalKind kind, uint16_t* rawMin, uint16_t* rawMax) = 0;

 protected:
  ~CalibrationHost() = default;
};

// Platform effects and state snapshots.
class DeviceActions {
 public:
  // Both reboot() and setDeck() are called only after their ACK has been handed to the sink. The
  // implementation should delay the actual restart long enough for the USB FIFO to flush.
  virtual void reboot(proto::RebootMode mode) = 0;
  // The Config already holds the new deckId. Persist it and re-enumerate USB (reboot).
  virtual void setDeck(proto::Deck deck) = 0;
  // Start (on) or stop the DAC test tone, auto-stopping after 10 s. False = no DAC (NOT_SUPPORTED).
  // Arguments are range-checked by the server (20..20000 Hz, attenDb <= 96).
  virtual bool testTone(bool on, uint16_t freqHz, uint8_t attenDb) = 0;
  virtual void identify(uint8_t seconds) = 0;
  // Persist the live config (typically Config::toBlob into NVS). False = flash error.
  virtual bool saveConfig(const Config& cfg) = 0;

  // Snapshots. The server overwrites protocolVersion, deck, mode and platterSource of Info from the
  // live Config, and the seq of Telemetry with its own frame counter.
  virtual void getInfo(proto::Info* out) = 0;
  virtual void getTelemetry(proto::Telemetry* out) = 0;
  // Fills up to max peers and returns how many.
  virtual size_t getPeers(proto::Peer* out, size_t max) = 0;

 protected:
  ~DeviceActions() = default;
};

class ConfigServer {
 public:
  static constexpr uint32_t kStreamTimeoutUs = 5000000;
  static constexpr uint8_t kMaxStreamHz = 100;

  enum class Result : uint8_t {
    Replied,   // a request for this unit; it was answered (a full sink may still have refused the frame)
    NotForUs,  // valid frame addressed to another deck
    Ignored,   // not ours: bad header/version/checksum/deck, or a reply/ACK/NAK from another unit
  };

  // `calibration` may be null (CAL_BEGIN then answers NOT_SUPPORTED, CAL_END CAL_NOT_ACTIVE).
  ConfigServer(Config& config, DeviceActions& actions, ReplySink& sink, CalibrationHost* calibration = nullptr)
      : config_(config), actions_(actions), sink_(sink), cal_(calibration) {}

  // One complete SysEx message including F0 and F7. nowUs is a monotonic microsecond clock.
  //
  // Frames with a bad checksum, header or version are dropped silently (the spec makes a NAK
  // optional). Frames whose command has the reply bit (>= 0x40), including ACK and NAK, are never
  // answered, so two linked units cannot ping-pong NAKs. A valid request is answered with a reply,
  // ACK, or NAK. The deck byte of every reply is this unit's deck when the request arrived (so
  // SET_DECK is acknowledged under the old deck).
  //
  // GET_CONFIG with key 0xFF answers one CONFIG per key and then ACK[GET_CONFIG] as the end marker. If
  // the sink refuses a frame the dump stops there and the ACK is not sent, so a dump that ends with
  // the ACK is complete. SET_CONFIG of deckId is NAK NOT_SUPPORTED (SET_DECK is the only way), and
  // LOAD_DEFAULTS keeps deckId, so the deck this unit answers to only changes through SET_DECK.
  Result handle(const uint8_t* msg, size_t len, uint64_t nowUs);

  // Call regularly (>= 200 Hz is plenty): emits TELEMETRY frames while streaming and expires the
  // stream 5 s after the last STREAM request. At most one frame per call, and a late call never
  // produces a burst.
  void tick(uint64_t nowUs);

  bool streaming() const { return streaming_; }
  uint8_t streamRateHz() const { return streaming_ ? rateHz_ : 0; }
  // Reply frames the sink refused. Nothing is silent: a host that never sees its answer can be
  // matched against this.
  uint32_t repliesDropped() const { return repliesDropped_; }

 private:
  uint8_t deck() const { return static_cast<uint8_t>(config_.get(proto::ConfigKey::DeckId)); }
  bool reply(const uint8_t* frame, size_t len);
  bool ack(uint8_t cmd);
  void nak(uint8_t cmd, proto::sysex::Err err);
  bool sendConfigValue(uint8_t key);

  void getInfo(const syx::Frame& f);
  void getConfig(const syx::Frame& f);
  void setConfig(const syx::Frame& f);
  void saveConfig(const syx::Frame& f);
  void loadDefaults(const syx::Frame& f);
  void stream(const syx::Frame& f, uint64_t nowUs);
  void calBegin(const syx::Frame& f);
  void calEnd(const syx::Frame& f);
  void setDeck(const syx::Frame& f);
  void rebootCmd(const syx::Frame& f);
  void linkStatus(const syx::Frame& f);
  void testTone(const syx::Frame& f);
  void identify(const syx::Frame& f);

  Config& config_;
  DeviceActions& actions_;
  ReplySink& sink_;
  CalibrationHost* cal_;
  uint8_t replyDeck_ = 0;  // deck byte for the reply being built (fixed at request arrival)
  uint32_t repliesDropped_ = 0;

  bool streaming_ = false;
  uint8_t rateHz_ = 0;
  uint8_t seq_ = 0;
  uint64_t nextDueUs_ = 0;
  uint64_t expiresUs_ = 0;
};

}  // namespace epdj
