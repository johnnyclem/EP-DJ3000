// The engine: one deterministic object that turns raw sensor readings, host MIDI, SysEx requests and
// radio packets into MIDI/SysEx/radio output (PROTOCOL.md sections 2-4, docs/LINK.md).
//
// The platform layer owns the hardware and the clock; the engine owns every decision. Nothing in here
// reads a clock, blocks, allocates, throws or touches a peripheral: given the same calls with the same
// timestamps it produces the same OutEvents, byte for byte.
//
// Time: every call carries a monotonic microsecond timestamp. A timestamp older than one the engine has
// already seen is treated as that one, so unsigned interval arithmetic inside never wraps.
//
// Threading: NOT thread-safe and NOT ISR-safe. One task (the "engine task") makes every call below,
// including popOut(); other tasks and ISRs hand it data through their own queues. Rates:
//   step()          1 kHz (a late call is fine; calling faster tightens MIDI clock placement, see below)
//   onMidiIn() etc. whenever input arrived, from the same task, before the next step()
//   popOut()        after every step() and every input call, until it returns false
#pragma once
#include <cstddef>
#include <cstdint>

#include "epdj/analog.h"
#include "epdj/buttons.h"
#include "epdj/config.h"
#include "epdj/config_server.h"
#include "epdj/jog.h"
#include "epdj/link.h"
#include "epdj/midi.h"
#include "epdj/protocol.h"
#include "epdj/sync.h"
#include "epdj/touch_ring.h"

namespace epdj {

// One 1 kHz sample of the hardware. All booleans are "true = active"; the HAL undoes active-low wiring.
struct RawInputs {
  uint64_t nowUs = 0;  // monotonic microseconds (esp_timer_get_time)
  uint16_t faderRaw = 0;  // 12-bit ADC
  uint16_t knobRaw = 0;   // 12-bit ADC
  uint16_t angle12 = 0;   // AS5600 angle, 0..4095 (ignored unless platterSource = AS5600)
  bool angleValid = false;  // the AS5600 read succeeded this sample
  uint16_t touchRaw[8] = {};  // capacitive readings, LARGER = MORE touched (touch_ring.h)
  uint8_t touchCount = 0;     // how many of touchRaw are wired (0 = no touch hardware)
  bool cue = false;
  bool rev = false;
  bool usbMounted = false;      // a USB host has enumerated the device
  bool audioStreaming = false;  // the host has the audio interface open
  bool dacPresent = false;      // a DAC was found (or forced by dacMode)
};

// One thing for the HAL to send. Which port(s) and how:
//   Message  1..3 bytes, one complete non-SysEx message.  USB: midi::encodePacket(cable, ...)   TRS: write the bytes
//   SysEx    one complete F0 ... F7 message.             USB: midi::encodeSysex(cable, ...)     TRS: write the bytes
//   Bytes    1..3 bytes of a raw stream (USB cable 1 -> TRS pass-through). TRS only: write the bytes
//   Link     one radio packet (<= 250 bytes), portMask = kLink. Broadcast it.
// portMask is a proto::clockPorts mask; Message/SysEx events never carry kLink (system messages are not
// bridged over the radio; the radio gets TRANSPORT packets instead, see README).
// `cable` is the USB-MIDI cable for the kUsb port and means nothing for the others.
struct OutEvent {
  enum class Kind : uint8_t { Message, SysEx, Bytes, Link };
  static constexpr size_t kMaxBytes = link::kMaxPacket;

  Kind kind = Kind::Message;
  uint8_t portMask = 0;
  uint8_t cable = 0;
  uint16_t len = 0;
  uint8_t data[kMaxBytes] = {};
};

// USB-MIDI packets for an OutEvent's kUsb port. Returns the packet count: 0 if the event has no USB part
// or does not fit in maxPackets. A SysEx of up to syx::kMaxFrameBytes needs (len + 2) / 3 packets.
size_t encodeUsb(const OutEvent& ev, midi::UsbPacket* out, size_t maxPackets);

// What the status LED should show. Semantic, not RGB: the HAL owns colours, scaling and animation.
struct LedStatus {
  enum class LinkLed : uint8_t {
    Off,        // linkEnable = 0
    Searching,  // enabled, no peer heard
    Up,         // peer alive
    Bridging,   // peer alive and MIDI is being bridged either way
    Conflict,   // a peer claims our deck id
  };

  proto::Deck deck = proto::Deck::A;  // the deck colour
  proto::Mode mode = proto::Mode::Dj;
  LinkLed link = LinkLed::Off;
  bool beatFlash = false;  // true for the first 60 ms of every beat (SYNC_MASTER / SYNC_FOLLOW)
  bool identify = false;   // the "on" half of the 2 Hz blink while IDENTIFY is active
  bool cueLit = false;     // the host says the cue point is active (note 36 feedback)
  bool touched = false;
  // The play indicator. DJ / SYNC_FOLLOW: the mirrored transport (the host's play note, transport CC, external
  // clock or linked master). SYNC_MASTER: the unit's own transport, shown as Playing also while the host says its
  // deck plays (the inbound play note). That feedback never moves the master's transport, clock or MTC.
  proto::TransportState transport = proto::TransportState::Stopped;
  uint8_t chordStage = 0;  // CUE+REV held: 1 = releasing now cycles the mode, 2 = releasing now swaps decks
  uint8_t brightness = 64; // ledBrightness
};

// Effects only the platform can perform. Called from inside step()/onSysexIn()/onMidiIn() on the engine task.
class EnginePlatform {
 public:
  virtual void reboot(proto::RebootMode mode) = 0;
  // The Config already holds the new deckId. Persist it and restart (re-enumerate USB). Called after the
  // SET_DECK ACK was queued and for the CUE+REV >= 5 s gesture; give the USB FIFO time to flush first.
  virtual void setDeck(proto::Deck deck) = 0;
  // False = cannot (the engine already refuses when no DAC is present). The engine stops the tone itself
  // 10 s after it was started by calling testTone(false, 0, 0).
  virtual bool testTone(bool on, uint16_t freqHz, uint8_t attenDb) = 0;
  // Persist the live config (Config::toBlob into NVS). False = flash error.
  virtual bool saveConfig(const Config& cfg) = 0;

 protected:
  ~EnginePlatform() = default;
};

class Engine {
 public:
  // Fixed per unit/boot, supplied by the platform.
  struct Identity {
    uint8_t hwRev = 0;
    uint32_t serial = 0;
    uint16_t caps = 0;      // proto::caps hardware capabilities; the engine adds kDac while a DAC is present
    link::Mac mac;          // station MAC from efuse
    uint16_t epoch = 1;     // random per boot (hardware RNG)
    // Build-time rig id, used while ConfigKey::LinkGroup is 0. A non-zero config value wins over it and
    // applies live (docs/LINK.md section 11).
    uint8_t linkGroup = 0;
  };

  // USB-MIDI cables 0..2 and the 3.5 mm TRS jack.
  enum class MidiPort : uint8_t { Usb0 = 0, Usb1 = 1, Usb2 = 2, Trs = 3 };

  struct Stats {
    uint32_t outPushed = 0;
    uint32_t outPopped = 0;
    uint32_t outDropped = 0;  // OutEvents lost because the queue was full (HAL not draining)
    uint32_t outHighWaterBytes = 0;
  };

  static constexpr size_t kOutQueueBytes = 4096;

  // `config` is the live config, loaded by the platform (loadConfig); the engine and its ConfigServer
  // mutate it. All state that needs the clock is initialised by the first call of any kind.
  Engine(Config& config, EnginePlatform& platform, const Identity& id);
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // ---- inputs ----

  // One tick of everything periodic. Output order within a step: config changes, buttons, platter,
  // jog, fader, knob, tempo, clock/MTC, radio.
  //
  // MIDI clock placement: a tick leaves in the first step at or after its ideal time, so the error is
  // 0..one call interval (< 1 ms at 1 kHz) and never accumulates: the schedule itself is the exact
  // ClockGenerator one. Call step() faster to tighten it.
  void step(const RawInputs& in);

  // Bytes received on a MIDI port. For USB: the data bytes of one USB-MIDI event (UsbPacket::size()
  // bytes) per call, or any chunking; SysEx may span calls and be interleaved with real-time bytes. For
  // TRS: any chunk of the UART byte stream. nowUs is the receive timestamp (clock estimation uses it).
  void onMidiIn(MidiPort port, const uint8_t* bytes, size_t len, uint64_t nowUs);
  // A USB-MIDI event from the host. Cables above 2 are ignored.
  void onUsbPacket(const midi::UsbPacket& p, uint64_t nowUs);
  // One complete, already assembled SysEx message (F0 ... F7) for this unit. onMidiIn assembles SysEx
  // from cable 0 itself and ends up here; call this directly only if the HAL has its own assembler.
  void onSysexIn(const uint8_t* frame, size_t len, uint64_t nowUs);
  // A radio packet. rxUs is the radio's receive timestamp (take it in the receive callback, time sync
  // accuracy depends on it), nowUs the current time.
  void onLinkPacket(const uint8_t* data, size_t len, int8_t rssi, uint64_t rxUs, uint64_t nowUs);
  void onLinkPacket(const uint8_t* data, size_t len, uint64_t nowUs) { onLinkPacket(data, len, 0, nowUs, nowUs); }

  // ---- outputs ----

  // Oldest event, or false if the queue is empty. Never blocks.
  bool popOut(OutEvent* ev);
  const LedStatus& led() const { return led_; }
  const Stats& stats() const { return stats_; }

  // Replaces Identity::caps (the engine still adds kDac while a DAC is present). For a capability that appears after
  // boot, such as an AS5600 that was not answering at power-up and is found by the platform's re-probe, so Info.caps
  // does not go on saying it is absent.
  void setCaps(uint16_t caps) { id_.caps = caps; }

  // ---- state, for the platform's own use and for tests ----
  const Config& config() const { return cfg_; }
  const link::Link& link() const { return link_; }
  proto::Mode mode() const { return mode_; }
  proto::TransportState transport() const;
  uint16_t tempoX10() const { return bpm_; }         // the tempo this unit would run as clock master
  uint16_t displayTempoX10() const;                  // what telemetry shows: followed tempo when there is one
  int32_t jogTotal() const { return jog_.total(); }
  size_t queuedBytes() const { return outUsed_; }

 private:
  enum class TempoSource : uint8_t { Config, Knob, Nudge, Tap, Host };

  struct HeldNote {
    uint8_t note = 0xFF;  // 0xFF = none
    uint8_t channel = 0;
  };

  // Interface adapters (ConfigServer, Link and CalibrationHost want interfaces, and Link's and
  // ReplySink's send() would clash in one class).
  class Radio final : public link::LinkTransport, public link::LinkSink {
   public:
    explicit Radio(Engine& e) : e_(e) {}
    bool send(const uint8_t* data, size_t len) override;
    void onBridgedMidi(const midi::Message& m) override;
    void onFeedbackMidi(const midi::Message& m) override;
    void onTransport(const link::TransportEvent& ev) override;

   private:
    Engine& e_;
  };
  class SysexOut final : public ReplySink {
   public:
    explicit SysexOut(Engine& e) : e_(e) {}
    bool send(const uint8_t* frame, size_t len) override;

   private:
    Engine& e_;
  };
  class Device final : public DeviceActions, public CalibrationHost {
   public:
    explicit Device(Engine& e) : e_(e) {}
    void reboot(proto::RebootMode mode) override;
    void setDeck(proto::Deck deck) override;
    bool testTone(bool on, uint16_t freqHz, uint8_t attenDb) override;
    void identify(uint8_t seconds) override;
    bool saveConfig(const Config& cfg) override;
    void getInfo(proto::Info* out) override;
    void getTelemetry(proto::Telemetry* out) override;
    size_t getPeers(proto::Peer* out, size_t max) override;
    bool begin(proto::CalKind kind) override;
    EndStatus end(proto::CalKind kind, Range* out) override;

   private:
    Engine& e_;
  };

  static constexpr size_t kPassSysexCap = 128;

  // output queue
  bool push(OutEvent::Kind kind, uint8_t portMask, uint8_t cable, const uint8_t* data, size_t len);
  bool pushMessage(uint8_t portMask, uint8_t cable, const midi::Message& m);
  void ringPut(uint8_t b);
  uint8_t ringGet();
  void emitDeck(const midi::Message& m);     // this unit's controls: USB cable 0 + radio bridge
  void emitSystem(const midi::Message& m);   // clock/transport/MTC: clockOutMask ports
  void emitSystemSysex(const uint8_t* msg, size_t len);
  uint8_t systemPorts() const;
  void noteOn(uint8_t note, HeldNote* held);
  void noteOff(HeldNote* held);

  // configuration
  void ensureStarted();
  void applyConfig(bool force);
  void setCfg(proto::ConfigKey key, int32_t value);
  // Like Config::effectiveChannel(), but "auto" follows the deck this unit booted as: SET_DECK (and the
  // CUE+REV deck swap) write the new deck into the config and then restart, so until the restart the
  // channel, the LED deck and the radio id must keep saying what the USB name says.
  uint8_t channel() const;
  uint8_t linkGroup() const;
  bool trsEnabled() const { return cfg_.get(proto::ConfigKey::TrsMidiEnable) != 0; }
  bool isMaster() const { return mode_ == proto::Mode::SyncMaster; }
  void setMode(proto::Mode m);
  void enterMaster();
  void leaveMaster();

  // sensors
  void runButtons();
  void onButtonDown(unsigned button);
  void onButtonUp(unsigned button);
  void runPlatter();
  bool sendsUntouchedJog() const;
  void runJog();
  void runFader();
  void runKnob();
  bool knobTakesOver(uint16_t v14);
  void sendFader(uint16_t v14);
  void sendKnob(uint16_t v14);
  void resendContinuous();

  // tempo / transport / clock
  void setTempo(uint16_t x10, TempoSource src);
  void applyHostBpm();
  void runTempoAnnounce();
  void doTransport(TransportFsm::Event e);
  void goTransport(proto::TransportState target);
  void afterTransport(const TransportFsm::Output& out, TransportFsm::Event e);
  void sendLinkTransport();
  void runClock();
  void runMtc();
  void playPulse();
  void runHousekeeping();

  // inbound
  void handleUsb0(const midi::Parser::Event& ev, uint64_t nowUs);
  void handleHostMessage(const midi::Message& m);
  void onSystemIn(const midi::Message& m, uint64_t nowUs);
  void onLinkTransport(const link::TransportEvent& ev);
  void feedTrs(const uint8_t* bytes, size_t len, uint64_t nowUs);
  void feedUsb2(const uint8_t* bytes, size_t len);

  // status
  bool linkBeatFlash() const;
  void updateLed();
  bool followingLink() const;

  Config& cfg_;
  EnginePlatform& host_;
  Identity id_;

  // adapters first: the members below keep references to them
  Radio radio_;
  SysexOut sysexOut_;
  Device device_;

  TouchRing ring_;
  TouchRing::Config ringCfg_;
  bool ringConfigured_ = false;
  JogTracker jog_;
  StickyTouch sticky_;  // the hold pad's motion-sticky release (AS5600 and hold-only platters)
  JogEmitter jogOut_;
  ButtonDebouncer deb_;
  ButtonGestures gestures_;
  AnalogControl fader_, knob_;
  CalibrationTracker calFader_, calKnob_;

  ClockGenerator clock_;
  TransportFsm fsm_;
  MtcGenerator mtc_;
  ClockFollower follower_;
  TapTempo tap_;
  midi::Cc14Receiver bpmRx_;

  link::Link link_;
  ConfigServer server_;

  uint8_t sysex0_[syx::kMaxFrameBytes] = {};
  uint8_t sysexTrs_[kPassSysexCap] = {};
  midi::Parser parser0_, parser2_, parserTrs_;

  // output ring: records of [kind, portMask, cable, len, bytes...]
  uint8_t outBuf_[kOutQueueBytes] = {};
  size_t outHead_ = 0, outTail_ = 0, outUsed_ = 0;
  Stats stats_;

  // time and last inputs
  bool started_ = false;
  uint64_t now_ = 0, bootUs_ = 0;
  proto::Deck bootDeck_ = proto::Deck::A;
  RawInputs in_;
  bool usbMounted_ = false, dacPresent_ = false, audioStreaming_ = false;
  bool linkEnabled_ = true, prevRouteOut_ = false;
  int8_t linkUsbHostSent_ = -1;  // what Link::setUsbHost last got (-1 = nothing yet)
  uint8_t buttonsDown_ = 0;

  // config bookkeeping
  int32_t applied_[proto::kConfigCount] = {};
  uint32_t appliedRev_ = 0;

  // mode / transport
  proto::Mode mode_ = proto::Mode::Dj;
  proto::TransportState mirror_ = proto::TransportState::Stopped;  // DJ / SYNC_FOLLOW view of the host's transport
  bool hostPlaying_ = false;    // the host's last play note (vel > 0): its deck is playing. Every mode; LED only
  uint32_t freeTick_ = 0;       // master clock ticks while not playing, for the beat flash
  uint32_t followTick_ = 0;     // ticks since Start in SYNC_FOLLOW
  uint64_t flashUntilUs_ = 0;
  uint64_t nextTransportRefreshUs_ = 0;

  // link transport as seen by a SYNC_FOLLOW unit
  struct LinkTransportView {
    bool valid = false;
    proto::TransportState state = proto::TransportState::Stopped;
    uint16_t bpmX10 = 1200;
    uint32_t positionClocks = 0;
    uint32_t atUs = 0;       // the sender's clock
    uint32_t arrivalUs = 0;  // our clock, for when time sync has no estimate
  } lt_;

  // tempo
  uint16_t bpm_ = 1200;
  int32_t bpmSent_ = -1;
  bool bpmAnnouncePending_ = false, linkTransportPending_ = false;
  uint64_t nextBpmAnnounceUs_ = 0;
  uint16_t bpmRxValue_ = 0;
  bool bpmRxPending_ = false;
  int32_t nudgeAcc_ = 0;

  // held notes (the channel at press time is remembered so a live channel change cannot strand a note)
  HeldNote heldBtn_[2];
  HeldNote heldTouch_, heldPulse_;
  // The hold pad has registered a touch since power-up (or since the platter source changed). jogUntouched = AUTO only
  // starts suppressing coasting after this, so a pad that is configured but not yet wired never silences the encoder.
  bool padProven_ = false;
  uint64_t pulseOffUs_ = 0;

  // continuous controls
  int32_t faderSent_ = -1, knobSent_ = -1;
  bool faderForce_ = false, knobForce_ = false;
  // knobRole = BPM: the knob does not set the tempo until it has moved away from where it was when it
  // was armed (boot, role or calibration change), so a boot never replaces the saved tempo.
  enum class KnobPickup : uint8_t { Arm, Waiting, Active };
  KnobPickup knobPickup_ = KnobPickup::Arm;
  uint16_t knobHome_ = 0;
  uint16_t faderOut_ = 0, knobOut_ = 0;
  uint16_t angleSeen_ = 0;
  bool angleValidSeen_ = false;

  // misc status
  bool cueLit_ = false;
  uint64_t identifyStartUs_ = 0, identifyUntilUs_ = 0;
  uint64_t toneUntilUs_ = 0;
  LedStatus led_;
};

}  // namespace epdj
