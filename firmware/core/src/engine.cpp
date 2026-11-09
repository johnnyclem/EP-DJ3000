#include "epdj/engine.h"

#include "epdj/util.h"
#include "epdj/version.h"

namespace epdj {

using proto::ConfigKey;
using proto::Mode;
using proto::TransportState;
namespace ports = proto::clockPorts;

namespace {

constexpr size_t kRecordHeader = 4;  // kind, portMask, cable, len
constexpr uint32_t kPlayPulseUs = 10000;           // PROTOCOL 2.3: note-off at least 10 ms after the note-on
constexpr uint32_t kBpmAnnounceIntervalUs = 50000;  // PROTOCOL 2.6: bpm CC at most 20 Hz
constexpr uint32_t kBeatFlashUs = 60000;
constexpr uint32_t kTransportRefreshUs = 10000000;  // keeps a follower's phase anchor fresh
constexpr uint32_t kToneMaxUs = 10000000;
constexpr uint32_t kIdentifyHalfPeriodUs = 250000;
constexpr int32_t kNudgeTicksPerStep = 32;  // PROTOCOL 2.5: +0.1 BPM per 32 ticks
constexpr int32_t kKnobPickup14 = AnalogControl::kMaxOut * 2 / 100;  // ~2% of the knob's travel
constexpr uint8_t kNoNote = 0xFF;
constexpr uint8_t kMaxTouch = 8;

// A GET_CONFIG dump (every key and the closing ACK) must fit the output queue in one go.
static_assert(Engine::kOutQueueBytes >= kConfigDumpBytes + (proto::kConfigCount + 1) * kRecordHeader,
              "the output queue cannot hold a full GET_CONFIG dump");

bool sameRing(const TouchRing::Config& a, const TouchRing::Config& b) {
  return a.sectors == b.sectors && a.onPermille == b.onPermille && a.offPermille == b.offPermille &&
         a.equalizeGains == b.equalizeGains;
}

}  // namespace

// ---- OutEvent helpers -------------------------------------------------------------------------

size_t encodeUsb(const OutEvent& ev, midi::UsbPacket* out, size_t maxPackets) {
  if ((ev.portMask & ports::kUsb) == 0 || maxPackets == 0) return 0;
  if (ev.kind == OutEvent::Kind::Message) {
    if (ev.len == 0 || ev.len > 3) return 0;
    midi::Message m;
    m.len = static_cast<uint8_t>(ev.len);
    for (uint8_t i = 0; i < m.len; ++i) m.b[i] = ev.data[i];
    return midi::encodePacket(ev.cable, m, &out[0]) ? 1u : 0u;
  }
  if (ev.kind == OutEvent::Kind::SysEx) return midi::encodeSysex(ev.cable, ev.data, ev.len, out, maxPackets);
  return 0;
}

// ---- adapters ---------------------------------------------------------------------------------

bool Engine::Radio::send(const uint8_t* data, size_t len) {
  return e_.push(OutEvent::Kind::Link, ports::kLink, 0, data, len);
}
void Engine::Radio::onBridgedMidi(const midi::Message& m) {
  if (e_.usbMounted_) e_.pushMessage(ports::kUsb, proto::cable::kLinkedDeck, m);
}
void Engine::Radio::onFeedbackMidi(const midi::Message& m) { e_.handleHostMessage(m); }
void Engine::Radio::onTransport(const link::TransportEvent& ev) { e_.onLinkTransport(ev); }

bool Engine::SysexOut::send(const uint8_t* frame, size_t len) {
  return e_.push(OutEvent::Kind::SysEx, ports::kUsb, proto::cable::kDeck, frame, len);
}

void Engine::Device::reboot(proto::RebootMode mode) { e_.host_.reboot(mode); }
void Engine::Device::setDeck(proto::Deck deck) { e_.host_.setDeck(deck); }
bool Engine::Device::saveConfig(const Config& cfg) { return e_.host_.saveConfig(cfg); }

bool Engine::Device::testTone(bool on, uint16_t freqHz, uint8_t attenDb) {
  if (!e_.dacPresent_ || !e_.host_.testTone(on, freqHz, attenDb)) return false;
  e_.toneUntilUs_ = on ? e_.now_ + kToneMaxUs : 0;
  return true;
}

void Engine::Device::identify(uint8_t seconds) {
  e_.identifyStartUs_ = e_.now_;
  e_.identifyUntilUs_ = seconds == 0 ? 0 : e_.now_ + static_cast<uint64_t>(seconds) * 1000000u;
}

void Engine::Device::getInfo(proto::Info* out) {
  *out = proto::Info();
  out->fwMajor = static_cast<uint8_t>(kVersionMajor);
  out->fwMinor = static_cast<uint8_t>(kVersionMinor);
  out->fwPatch = static_cast<uint8_t>(kVersionPatch);
  out->hwRev = e_.id_.hwRev;
  out->caps = static_cast<uint16_t>(e_.id_.caps | (e_.dacPresent_ ? proto::caps::kDac : 0));
  out->serial = e_.id_.serial;
  out->uptimeS = static_cast<uint32_t>((e_.now_ - e_.bootUs_) / 1000000u);
}

void Engine::Device::getTelemetry(proto::Telemetry* out) {
  using namespace proto::stateFlags;
  *out = proto::Telemetry();
  uint8_t f = 0;
  if (e_.jog_.touched()) f |= kTouched;
  if (e_.angleValidSeen_) f |= kAngleValid;
  if (e_.linkEnabled_ && e_.link_.linkUp()) f |= kLinkUp;
  if (e_.usbMounted_) f |= kUsbMounted;
  if (e_.audioStreaming_) f |= kAudioStreaming;
  if (e_.dacPresent_) f |= kDacPresent;
  if (e_.linkEnabled_ && e_.link_.bridging()) f |= kBridging;
  out->buttons = e_.buttonsDown_;
  out->flags = f;
  out->transport = static_cast<uint8_t>(e_.transport());
  out->faderRaw = e_.in_.faderRaw;
  out->knobRaw = e_.in_.knobRaw;
  out->faderOut = e_.faderOut_;
  out->knobOut = e_.knobOut_;
  out->angle = e_.angleSeen_;
  out->bpmX10 = e_.displayTempoX10();
  out->jogTotal = e_.jog_.total();
  for (uint8_t i = 0; i < e_.in_.touchCount; ++i) out->touchRaw[i] = e_.in_.touchRaw[i];
}

size_t Engine::Device::getPeers(proto::Peer* out, size_t max) {
  return e_.linkEnabled_ ? e_.link_.fillPeers(out, max, e_.now_) : 0;
}

bool Engine::Device::begin(proto::CalKind kind) {
  (kind == proto::CalKind::Fader ? e_.calFader_ : e_.calKnob_).begin();
  return true;
}

CalibrationHost::EndStatus Engine::Device::end(proto::CalKind kind, Range* out) {
  CalibrationTracker::Result r;
  switch ((kind == proto::CalKind::Fader ? e_.calFader_ : e_.calKnob_).end(&r)) {
    case CalibrationTracker::Status::Ok:
      out->measuredMin = r.measuredMin;
      out->measuredMax = r.measuredMax;
      out->rawMin = r.rawMin;
      out->rawMax = r.rawMax;
      return EndStatus::Ok;
    case CalibrationTracker::Status::NotActive:
      return EndStatus::NotActive;
    case CalibrationTracker::Status::SpanTooSmall:
      break;
  }
  return EndStatus::Failed;
}

// ---- construction -----------------------------------------------------------------------------

Engine::Engine(Config& config, EnginePlatform& platform, const Identity& id)
    : cfg_(config),
      host_(platform),
      id_(id),
      radio_(*this),
      sysexOut_(*this),
      device_(*this),
      bpmRx_(proto::cc::kBpmMsb, proto::cc::kBpmLsb),
      link_(radio_, radio_),
      server_(config, device_, sysexOut_, &device_),
      parser0_(sysex0_, sizeof sysex0_),
      parser2_(nullptr, 0),
      parserTrs_(sysexTrs_, sizeof sysexTrs_) {
  bpm_ = static_cast<uint16_t>(cfg_.get(ConfigKey::BpmX10));
}

uint8_t Engine::channel() const {
  const int32_t ch = cfg_.get(ConfigKey::MidiChannel);
  if (ch >= 0 && ch <= 15) return static_cast<uint8_t>(ch);
  return bootDeck_ == proto::Deck::B ? proto::kDefaultChannelDeckB : proto::kDefaultChannelDeckA;
}

uint8_t Engine::otherChannel() const {
  const uint8_t ch = channel();
  if (ch == proto::kDefaultChannelDeckA) return proto::kDefaultChannelDeckB;
  if (ch == proto::kDefaultChannelDeckB) return proto::kDefaultChannelDeckA;
  return static_cast<uint8_t>((ch + 1) & 0x0F);
}

// A non-zero config value wins over the build-time default; applied live by applyConfig.
uint8_t Engine::linkGroup() const {
  const int32_t g = cfg_.get(ConfigKey::LinkGroup);
  return static_cast<uint8_t>(g != 0 ? g : id_.linkGroup);
}

proto::TransportState Engine::transport() const { return isMaster() ? fsm_.state() : mirror_; }

bool Engine::followingLink() const {
  return mode_ == Mode::SyncFollow && lt_.valid && linkEnabled_ && link_.linkUp();
}

uint16_t Engine::displayTempoX10() const {
  if (!isMaster()) {
    if (follower_.locked(now_)) return follower_.bpmX10(now_);
    if (followingLink()) return lt_.bpmX10;
  }
  return bpm_;
}

// ---- output queue -----------------------------------------------------------------------------

void Engine::ringPut(uint8_t b) {
  outBuf_[outTail_] = b;
  if (++outTail_ == kOutQueueBytes) outTail_ = 0;
}

uint8_t Engine::ringGet() {
  const uint8_t b = outBuf_[outHead_];
  if (++outHead_ == kOutQueueBytes) outHead_ = 0;
  return b;
}

bool Engine::push(OutEvent::Kind kind, uint8_t portMask, uint8_t cable, const uint8_t* data, size_t len) {
  if (portMask == 0) return true;
  const size_t need = kRecordHeader + len;
  if (len > OutEvent::kMaxBytes || outUsed_ + need > kOutQueueBytes) {
    ++stats_.outDropped;
    return false;
  }
  ringPut(static_cast<uint8_t>(kind));
  ringPut(portMask);
  ringPut(cable);
  ringPut(static_cast<uint8_t>(len));
  for (size_t i = 0; i < len; ++i) ringPut(data[i]);
  outUsed_ += need;
  ++stats_.outPushed;
  if (outUsed_ > stats_.outHighWaterBytes) stats_.outHighWaterBytes = static_cast<uint32_t>(outUsed_);
  return true;
}

bool Engine::pushMessage(uint8_t portMask, uint8_t cable, const midi::Message& m) {
  return push(OutEvent::Kind::Message, portMask, cable, m.b, m.len);
}

bool Engine::popOut(OutEvent* ev) {
  if (outUsed_ == 0) return false;
  ev->kind = static_cast<OutEvent::Kind>(ringGet());
  ev->portMask = ringGet();
  ev->cable = ringGet();
  const size_t len = ringGet();
  ev->len = static_cast<uint16_t>(len);
  for (size_t i = 0; i < len; ++i) ev->data[i] = ringGet();
  outUsed_ -= kRecordHeader + len;
  ++stats_.outPopped;
  return true;
}

// This unit's own controls: USB cable 0 when a host is attached, and the radio bridge when a peer
// carries our MIDI for us (the link refuses messages that are not forwardable).
void Engine::emitDeck(const midi::Message& m) {
  if (usbMounted_) pushMessage(ports::kUsb, proto::cable::kDeck, m);
  if (linkEnabled_) link_.forwardMidi(m);
}

uint8_t Engine::systemPorts() const {
  uint8_t mask = static_cast<uint8_t>(cfg_.get(ConfigKey::ClockOutMask) & (ports::kUsb | ports::kTrs));
  if (!usbMounted_) mask = static_cast<uint8_t>(mask & ~ports::kUsb);
  if (!trsEnabled()) mask = static_cast<uint8_t>(mask & ~ports::kTrs);
  return mask;
}

void Engine::emitSystem(const midi::Message& m) { pushMessage(systemPorts(), proto::cable::kDeck, m); }

void Engine::emitSystemSysex(const uint8_t* msg, size_t len) {
  push(OutEvent::Kind::SysEx, systemPorts(), proto::cable::kDeck, msg, len);
}

void Engine::noteOn(uint8_t note, HeldNote* held, uint8_t ch) {
  held->note = note;
  held->channel = ch;
  emitDeck(midi::noteOn(ch, note, 127));
}

void Engine::noteOff(HeldNote* held) {
  if (held->note == kNoNote) return;
  emitDeck(midi::noteOff(held->channel, held->note, 0));
  held->note = kNoNote;
}

// ---- configuration ----------------------------------------------------------------------------

// Lazily from the first call of any kind, so an input that arrives before the first step() meets a
// configured engine.
void Engine::ensureStarted() {
  if (started_) return;
  started_ = true;
  bootUs_ = now_;
  bootDeck_ = cfg_.deck();
  link::Link::Config lc;
  lc.mac = id_.mac;
  lc.deck = static_cast<uint8_t>(bootDeck_);
  lc.group = linkGroup();
  lc.epoch = id_.epoch;
  link_.configure(lc);
  applyConfig(true);
}

// Re-parameterises only what changed, keeping every filter's state, so a SET_CONFIG during a scratch
// cannot glitch the platter or the fader.
void Engine::applyConfig(bool force) {
  using K = ConfigKey;
  const auto changed = [&](K k) { return force || applied_[static_cast<size_t>(k)] != cfg_.get(k); };
  const auto v = [&](K k) { return cfg_.get(k); };

  if (changed(K::PlatterSource) || changed(K::JogInvert) || changed(K::TapMaxMs) || changed(K::TapMoveTicks)) {
    if (changed(K::PlatterSource)) padProven_ = false;
    if (!force && changed(K::PlatterSource)) {  // the tracker restarts without a touch-up: close the touch we reported
      noteOff(&heldTouch_);
      nudgeAcc_ = 0;
      sticky_.reset();
    }
    JogTracker::Config jc;
    jc.source = static_cast<proto::PlatterSource>(v(K::PlatterSource));
    jc.invert = v(K::JogInvert) != 0;
    jc.tapMaxMs = static_cast<uint16_t>(v(K::TapMaxMs));
    jc.tapMoveTicks = static_cast<uint8_t>(v(K::TapMoveTicks));
    jog_.configure(jc);
  }

  if (changed(K::FaderRawMin) || changed(K::FaderRawMax) || changed(K::FaderInvert) ||
      changed(K::FaderDeadLoPermille) || changed(K::FaderDeadHiPermille) || changed(K::FaderCurve)) {
    AnalogControl::Config ac;
    ac.rawMin = static_cast<uint16_t>(v(K::FaderRawMin));
    ac.rawMax = static_cast<uint16_t>(v(K::FaderRawMax));
    ac.invert = v(K::FaderInvert) != 0;
    ac.deadLoPermille = static_cast<uint16_t>(v(K::FaderDeadLoPermille));
    ac.deadHiPermille = static_cast<uint16_t>(v(K::FaderDeadHiPermille));
    ac.curve = static_cast<proto::FaderCurve>(v(K::FaderCurve));
    fader_.configure(ac);
    // The right pitch and the crossfader are the same kind of slide pot. They follow this calibration,
    // curve and invert: one saved window, three pots. A pot whose travel differs can be flipped in
    // hardware (which end is 3V3) until its own keys exist.
    pitchRight_.configure(ac);
    crossfader_.configure(ac);
    if (!force) {
      pitchRightSent_ = -1;
      pitchRightForce_ = true;
      crossSent_ = -1;
      crossForce_ = true;
    }
  }
  if (!force && changed(K::FaderRole)) {  // the other CC pair has never been told the position
    faderSent_ = -1;
    faderForce_ = true;
  }

  if (changed(K::KnobRawMin) || changed(K::KnobRawMax) || changed(K::KnobInvert)) {
    AnalogControl::Config ac;
    ac.rawMin = static_cast<uint16_t>(v(K::KnobRawMin));
    ac.rawMax = static_cast<uint16_t>(v(K::KnobRawMax));
    ac.invert = v(K::KnobInvert) != 0;
    ac.deadLoPermille = 0;
    ac.deadHiPermille = 0;
    ac.curve = proto::FaderCurve::Linear;
    knob_.configure(ac);
  }
  if (!force && changed(K::KnobRole)) {
    knobSent_ = -1;
    knobForce_ = true;
  }
  // The same physical position now reads differently (or means something else): pick up again.
  if (changed(K::KnobRole) || changed(K::KnobRawMin) || changed(K::KnobRawMax) || changed(K::KnobInvert)) {
    knobPickup_ = KnobPickup::Arm;
  }

  if (changed(K::ButtonDebounceMs)) {
    deb_.setDebounceMs(static_cast<uint8_t>(v(K::ButtonDebounceMs)));
    twinDeb_.setDebounceMs(static_cast<uint8_t>(v(K::ButtonDebounceMs)));
  }
  if (changed(K::MtcFps)) mtc_.setFps(static_cast<uint8_t>(v(K::MtcFps)), now_);
  if (changed(K::LinkBridge)) link_.setBridgeMode(static_cast<proto::LinkBridge>(v(K::LinkBridge)));
  if (!force && changed(K::LinkGroup)) link_.setGroup(linkGroup(), now_);  // boot reads it in ensureStarted
  if (changed(K::LinkEnable)) {
    linkEnabled_ = v(K::LinkEnable) != 0;
    linkUsbHostSent_ = -1;  // the link missed host changes while disabled: tell it again next step
  }

  // Tempo before mode, so entering SYNC_MASTER starts the clock at the right rate.
  if (changed(K::BpmX10) && v(K::BpmX10) != bpm_) setTempo(static_cast<uint16_t>(v(K::BpmX10)), TempoSource::Config);
  if (changed(K::Mode)) setMode(static_cast<Mode>(v(K::Mode)));
  if (!force && changed(K::ClockOutMask) && (v(K::ClockOutMask) & ports::kLink) != 0) sendLinkTransport();

  for (size_t i = 0; i < proto::kConfigCount; ++i) applied_[i] = cfg_.get(static_cast<ConfigKey>(i));
  appliedRev_ = cfg_.revision();
}

void Engine::setMode(Mode m) {
  if (m == mode_) return;
  if (isMaster()) leaveMaster();
  if (mode_ == Mode::SyncFollow) lt_.valid = false;
  mode_ = m;
  nudgeAcc_ = 0;
  if (isMaster()) {
    enterMaster();
  } else {
    mirror_ = follower_.transport();
  }
}

void Engine::enterMaster() {
  fsm_ = TransportFsm();
  mtc_.rewind(now_);
  clock_.start(now_, bpm_);
  freeTick_ = 0;
  flashUntilUs_ = 0;
  jogOut_.clear();
  nextTransportRefreshUs_ = now_ + kTransportRefreshUs;
  sendLinkTransport();  // followers learn the tempo although nothing is playing
}

// A follower must not be left running without a clock: stop (and rewind) what we started.
void Engine::leaveMaster() {
  if (fsm_.state() != TransportState::Stopped) doTransport(TransportFsm::Event::Stop);
  clock_.stop();
  fsm_ = TransportFsm();
}

// ---- inputs -----------------------------------------------------------------------------------

void Engine::step(const RawInputs& in) {
  if (in.nowUs > now_) now_ = in.nowUs;  // time never runs backwards, whatever a caller passes
  ensureStarted();
  in_ = in;
  in_.faderRaw = in.faderRaw > 4095 ? static_cast<uint16_t>(4095) : in.faderRaw;
  in_.knobRaw = in.knobRaw > 4095 ? static_cast<uint16_t>(4095) : in.knobRaw;
  in_.pitchRightRaw = in.pitchRightRaw > 4095 ? static_cast<uint16_t>(4095) : in.pitchRightRaw;
  in_.crossfaderRaw = in.crossfaderRaw > 4095 ? static_cast<uint16_t>(4095) : in.crossfaderRaw;
  in_.angle12 = static_cast<uint16_t>(in.angle12 & 4095u);
  if (in_.touchCount > kMaxTouch) in_.touchCount = kMaxTouch;
  dacPresent_ = in.dacPresent;
  audioStreaming_ = in.audioStreaming;

  if (cfg_.revision() != appliedRev_) applyConfig(false);

  const bool mountedEdge = in.usbMounted && !usbMounted_;
  usbMounted_ = in.usbMounted;
  if (linkEnabled_ && linkUsbHostSent_ != (usbMounted_ ? 1 : 0)) {
    linkUsbHostSent_ = usbMounted_ ? 1 : 0;
    link_.setUsbHost(usbMounted_);
  }
  const bool routeOut = linkEnabled_ && link_.routeOut();
  if (mountedEdge || (routeOut && !prevRouteOut_)) resendContinuous();  // the new listener has never seen our state
  prevRouteOut_ = routeOut;

  calFader_.feed(in_.faderRaw);
  calKnob_.feed(in_.knobRaw);

  applyHostBpm();
  runButtons();
  runPlatter();
  runJog();
  runFader();
  runKnob();
  runTwin();
  runHousekeeping();
  runTempoAnnounce();
  if (isMaster()) {
    runClock();
    runMtc();
    if (now_ >= nextTransportRefreshUs_) {
      nextTransportRefreshUs_ = now_ + kTransportRefreshUs;
      sendLinkTransport();
    }
  }
  if (linkEnabled_) link_.poll(now_);
  server_.tick(now_);
  updateLed();
}

void Engine::resendContinuous() {
  faderSent_ = knobSent_ = pitchRightSent_ = crossSent_ = -1;
  faderForce_ = knobForce_ = pitchRightForce_ = crossForce_ = true;
  bpmSent_ = -1;
  if (isMaster()) bpmAnnouncePending_ = true;
}

// ---- buttons ----------------------------------------------------------------------------------

void Engine::runButtons() {
  const uint8_t raw = static_cast<uint8_t>((in_.cue ? proto::buttons::kCue : 0) | (in_.rev ? proto::buttons::kRev : 0));
  buttonsDown_ = deb_.update(raw, now_);
  const ButtonGestures::Events ev = gestures_.update(buttonsDown_, now_);
  for (unsigned b = 0; b < ButtonDebouncer::kButtons; ++b) {
    const uint8_t bit = static_cast<uint8_t>(1u << b);
    if (ev.pressed & bit) onButtonDown(b);
    if (ev.released & bit) onButtonUp(b);
  }
  if (ev.chord == ButtonGestures::Chord::None) return;

  for (unsigned b = 0; b < ButtonDebouncer::kButtons; ++b) {
    if (ev.noteOffMask & (1u << b)) onButtonUp(b);
  }
  if (ev.chord == ButtonGestures::Chord::CycleMode) {
    const int32_t next = (cfg_.get(ConfigKey::Mode) + 1) % 3;  // DJ -> SYNC_MASTER -> SYNC_FOLLOW -> DJ
    cfg_.set(ConfigKey::Mode, next);
    applyConfig(false);
  } else {
    const proto::Deck next = cfg_.deck() == proto::Deck::A ? proto::Deck::B : proto::Deck::A;
    cfg_.set(ConfigKey::DeckId, static_cast<int32_t>(next));
    host_.setDeck(next);
  }
}

void Engine::onButtonDown(unsigned button) {
  noteOff(&heldBtn_[button]);
  if (button == 0) {  // CUE
    if (isMaster()) {
      const uint16_t t = tap_.tap(now_);
      if (t != 0) setTempo(t, TempoSource::Tap);
      noteOn(proto::note::kTap, &heldBtn_[button]);
    } else {
      noteOn(proto::note::kCue, &heldBtn_[button]);
    }
  } else if (isMaster()) {  // REV: stop + rewind, no note
    doTransport(TransportFsm::Event::Stop);
  } else {
    noteOn(proto::note::kRev, &heldBtn_[button]);
  }
}

// The note-off mirrors whatever the press sent, even if the mode changed in between.
void Engine::onButtonUp(unsigned button) { noteOff(&heldBtn_[button]); }

// ---- platter ----------------------------------------------------------------------------------

void Engine::runPlatter() {
  const auto source = static_cast<proto::PlatterSource>(cfg_.get(ConfigKey::PlatterSource));

  // A ring with fewer pads wired than configured sectors uses what is there; fewer than 3 pads are
  // repeated around the ring, which still gives touch/hold detection (the angle is then meaningless).
  TouchRing::Config rc;
  rc.sectors = static_cast<uint8_t>(cfg_.get(ConfigKey::TouchSectors));
  if (in_.touchCount > 0 && in_.touchCount < rc.sectors) {
    rc.sectors = in_.touchCount < TouchRing::kMinSectors ? static_cast<uint8_t>(TouchRing::kMinSectors) : in_.touchCount;
  }
  rc.onPermille = static_cast<uint16_t>(cfg_.get(ConfigKey::TouchOnPermille));
  rc.offPermille = static_cast<uint16_t>(cfg_.get(ConfigKey::TouchOffPermille));
  if (!ringConfigured_ || !sameRing(rc, ringCfg_)) {
    ring_.configure(rc);
    ringCfg_ = rc;
    ringConfigured_ = true;
  }

  TouchRing::Result tr;
  if (in_.touchCount > 0) {
    uint16_t r[kMaxTouch];
    for (unsigned i = 0; i < rc.sectors; ++i) r[i] = in_.touchRaw[i % in_.touchCount];
    tr = ring_.update(r, now_);
  }

  JogTracker::Input ji;
  ji.touched = tr.touched;
  const bool holdPad = source != proto::PlatterSource::TouchRing;
  if (holdPad) ji.touched = sticky_.update(tr.touched, now_);
  switch (source) {
    case proto::PlatterSource::TouchRing:
      ji.angleValid = tr.angleValid;
      ji.angle = tr.angle;
      ji.angleSigma = tr.angleSigma;
      break;
    case proto::PlatterSource::As5600:
      ji.angleValid = in_.angleValid;
      ji.angle = in_.angle12;
      break;
    case proto::PlatterSource::TouchHoldOnly:
      break;
  }
  angleSeen_ = ji.angle;
  angleValidSeen_ = ji.angleValid;

  const JogTracker::Output o = jog_.update(ji, now_);
  if (holdPad) sticky_.noteMotion(o.ticks != 0, now_);

  if (o.touchDown) {
    padProven_ = true;
    noteOn(proto::note::kTouch, &heldTouch_);
    nudgeAcc_ = 0;
  }

  if (isMaster()) {
    // The platter is a tempo wheel here, and only while a finger is on it.
    if (o.ticks != 0 && (jog_.touched() || o.touchUp)) {
      nudgeAcc_ += o.ticks;
      const int32_t steps = nudgeAcc_ / kNudgeTicksPerStep;
      if (steps != 0) {
        nudgeAcc_ -= steps * kNudgeTicksPerStep;
        setTempo(bpm::nudge(bpm_, steps), TempoSource::Nudge);
      }
    }
  } else if (jog_.touched() || o.touchUp || sendsUntouchedJog()) {
    jogOut_.add(o.ticks);
  }  // else: motion with no hand on the platter, refused by jogUntouched. Gone, not queued: no burst at the next touch.

  if (o.touchUp) {
    noteOff(&heldTouch_);
    if (o.tap && cfg_.get(ConfigKey::TapPlay) != 0) {
      if (isMaster()) doTransport(fsm_.state() == TransportState::Playing ? TransportFsm::Event::Pause : TransportFsm::Event::Play);
      playPulse();
    }
  }
}

// jogUntouched (PROTOCOL 3.1): is platter motion with no hand on it sent as jog? On the AS5600 platter AUTO says no, because a
// free-spinning platter coasts for seconds after the hand leaves and would bend the pitch the whole time (the original Field
// Jog author sends jog only while touched), but only once the hold pad has registered a touch since power-up (padProven_).
// Until then jog is sent, so the encoder works before the pad does. A ring only ever produces motion while touched, so the
// policy cannot matter there.
bool Engine::sendsUntouchedJog() const {
  switch (static_cast<proto::JogUntouched>(cfg_.get(ConfigKey::JogUntouched))) {
    case proto::JogUntouched::Report:
      return true;
    case proto::JogUntouched::Suppress:
      return false;
    case proto::JogUntouched::Auto:
      break;
  }
  const auto source = static_cast<proto::PlatterSource>(cfg_.get(ConfigKey::PlatterSource));
  return !(source == proto::PlatterSource::As5600 && in_.touchCount > 0 && padProven_);
}

void Engine::pulsePlay(HeldNote* held, uint64_t* offAtUs, uint8_t ch) {
  noteOff(held);
  noteOn(proto::note::kPlay, held, ch);
  *offAtUs = now_ + kPlayPulseUs;
}

void Engine::playPulse() { pulsePlay(&heldPulse_, &pulseOffUs_, channel()); }

namespace {
constexpr uint8_t kTwinPlay = static_cast<uint8_t>(1u << 0);
constexpr uint8_t kTwinLoopIn = static_cast<uint8_t>(1u << 1);
constexpr uint8_t kTwinCueRight = static_cast<uint8_t>(1u << 2);
constexpr uint8_t kTwinPlayRight = static_cast<uint8_t>(1u << 3);
constexpr uint8_t kTwinLoopInRight = static_cast<uint8_t>(1u << 4);
constexpr uint8_t kTwinLoopOutRight = static_cast<uint8_t>(1u << 5);
}  // namespace

void Engine::runTwin() {
  if (!in_.twinWired) return;

  const uint8_t raw = static_cast<uint8_t>((in_.play ? kTwinPlay : 0) | (in_.loopIn ? kTwinLoopIn : 0) |
                                            (in_.cueRight ? kTwinCueRight : 0) | (in_.playRight ? kTwinPlayRight : 0) |
                                            (in_.loopInRight ? kTwinLoopInRight : 0) |
                                            (in_.loopOutRight ? kTwinLoopOutRight : 0));
  const uint8_t down = twinDeb_.update(raw, now_);
  const uint8_t pressed = static_cast<uint8_t>(down & ~twinDown_);
  const uint8_t released = static_cast<uint8_t>(twinDown_ & ~down);
  twinDown_ = down;

  if (pressed & kTwinPlay) {
    // Same message a platter tap sends. In SYNC_MASTER the tap also moves this unit's transport;
    // the button does too. The right PLAY button does not: this unit has one transport.
    if (isMaster()) {
      doTransport(fsm_.state() == TransportState::Playing ? TransportFsm::Event::Pause : TransportFsm::Event::Play);
    }
    playPulse();
  }
  if (pressed & kTwinLoopIn) noteOn(proto::note::kLoopIn, &heldLoopIn_);
  if (released & kTwinLoopIn) noteOff(&heldLoopIn_);

  const uint8_t other = otherChannel();
  if (pressed & kTwinCueRight) noteOn(proto::note::kCue, &heldCueRight_, other);
  if (released & kTwinCueRight) noteOff(&heldCueRight_);
  if (pressed & kTwinPlayRight) pulsePlay(&heldPulseRight_, &pulseOffRightUs_, other);
  if (pressed & kTwinLoopInRight) noteOn(proto::note::kLoopIn, &heldLoopInRight_, other);
  if (released & kTwinLoopInRight) noteOff(&heldLoopInRight_);
  if (pressed & kTwinLoopOutRight) noteOn(proto::note::kLoopOut, &heldLoopOutRight_, other);
  if (released & kTwinLoopOutRight) noteOff(&heldLoopOutRight_);

  const AnalogControl::Result pitch = pitchRight_.update(in_.pitchRightRaw, now_);
  if (pitch.changed || pitchRightForce_) {
    pitchRightForce_ = false;
    sendCc(other, proto::cc::kPitchMsb, proto::cc::kPitchLsb, pitch.value, &pitchRightSent_);
  }
  const AnalogControl::Result cross = crossfader_.update(in_.crossfaderRaw, now_);
  if (cross.changed || crossForce_) {
    crossForce_ = false;
    sendCc(channel(), proto::cc::kCrossfaderMsb, proto::cc::kCrossfaderLsb, cross.value, &crossSent_);
  }
}

void Engine::runJog() {
  // With nobody to receive it, motion is dropped instead of queued: a host that appears later must not
  // get a burst of old movement.
  if (!usbMounted_ && !(linkEnabled_ && link_.routeOut())) {
    jogOut_.clear();
    return;
  }
  uint8_t value = 0;
  if (!isMaster() && jogOut_.poll(now_, &value)) {
    emitDeck(midi::controlChange(channel(), proto::cc::kJog, value));
  }
}

// ---- fader and knob ---------------------------------------------------------------------------

void Engine::sendFader(uint16_t v14) {
  uint8_t msb = proto::cc::kCrossfaderMsb, lsb = proto::cc::kCrossfaderLsb;
  switch (static_cast<proto::FaderRole>(cfg_.get(ConfigKey::FaderRole))) {
    case proto::FaderRole::Crossfader:
      // The dedicated crossfader pot owns CC 23/55 while it is wired, so the left slide does not
      // fight it. The left slide still tracks; it just sends nothing in this role.
      if (in_.twinWired) {
        faderSent_ = v14;
        return;
      }
      break;
    case proto::FaderRole::ChannelVolume:
      msb = proto::cc::kChannelVolumeMsb;
      lsb = proto::cc::kChannelVolumeLsb;
      break;
    case proto::FaderRole::Pitch:
      msb = proto::cc::kPitchMsb;
      lsb = proto::cc::kPitchLsb;
      break;
  }
  midi::Message out[2];
  const size_t n = midi::cc14(channel(), msb, lsb, v14, faderSent_, out);
  faderSent_ = v14;
  for (size_t i = 0; i < n; ++i) emitDeck(out[i]);
}

void Engine::sendCc(uint8_t ch, uint8_t msb, uint8_t lsb, uint16_t v14, int32_t* sent) {
  midi::Message out[2];
  const size_t n = midi::cc14(ch, msb, lsb, v14, *sent, out);
  *sent = v14;
  for (size_t i = 0; i < n; ++i) emitDeck(out[i]);
}

void Engine::sendKnob(uint16_t v14) {
  midi::Message out[2];
  const size_t n = midi::cc14(channel(), proto::cc::kKnobMsb, proto::cc::kKnobLsb, v14, knobSent_, out);
  knobSent_ = v14;
  for (size_t i = 0; i < n; ++i) emitDeck(out[i]);
}

void Engine::runFader() {
  const AnalogControl::Result r = fader_.update(in_.faderRaw, now_);
  faderOut_ = r.value;
  if (r.changed || faderForce_) {
    faderForce_ = false;
    sendFader(r.value);
  }
}

// Pickup for knobRole = BPM. The first sample after arming is "home"; the knob takes over the tempo
// once it is more than ~2% of its travel away from there.
bool Engine::knobTakesOver(uint16_t v14) {
  if (knobPickup_ == KnobPickup::Arm) {
    knobHome_ = v14;
    knobPickup_ = KnobPickup::Waiting;
  }
  if (knobPickup_ == KnobPickup::Waiting) {
    const int32_t away = static_cast<int32_t>(v14) - static_cast<int32_t>(knobHome_);
    if ((away < 0 ? -away : away) > kKnobPickup14) knobPickup_ = KnobPickup::Active;
  }
  return knobPickup_ == KnobPickup::Active;
}

void Engine::runKnob() {
  const AnalogControl::Result r = knob_.update(in_.knobRaw, now_);
  knobOut_ = r.value;
  if (cfg_.get(ConfigKey::KnobRole) == static_cast<int32_t>(proto::KnobRole::Bpm)) {
    knobForce_ = false;  // a resend must not replace a tempo that the host or a tap set since
    if (!knobTakesOver(r.value) || !r.changed) return;
    // 60.0 + 120.0 x position BPM, in tenths, rounded.
    const uint32_t x10 = 600u + (static_cast<uint32_t>(r.value) * 1200u + 8191u) / 16383u;
    setTempo(static_cast<uint16_t>(x10), TempoSource::Knob);
    return;
  }
  if (!r.changed && !knobForce_) return;
  knobForce_ = false;
  sendKnob(r.value);
}

// ---- tempo, transport, clock ------------------------------------------------------------------

void Engine::setTempo(uint16_t x10, TempoSource src) {
  const uint16_t t = bpm::clampX10(x10);
  if (t == bpm_) return;
  bpm_ = t;
  if (src != TempoSource::Config) {  // keep GET_CONFIG / SAVE_CONFIG showing the live tempo
    const uint32_t before = cfg_.revision();
    cfg_.set(ConfigKey::BpmX10, t);
    applied_[static_cast<size_t>(ConfigKey::BpmX10)] = t;
    if (appliedRev_ == before) appliedRev_ = cfg_.revision();
  }
  if (isMaster()) {
    clock_.setBpmX10(t);
    linkTransportPending_ = true;
  }
  // The host is told about tempo changes made here, not about the ones it made itself.
  if ((isMaster() && src != TempoSource::Host) || src == TempoSource::Knob) bpmAnnouncePending_ = true;
}

// The two halves of a host-sent bpm pair arrive back to back; applying once per step skips the
// intermediate value between them.
void Engine::applyHostBpm() {
  if (!bpmRxPending_) return;
  bpmRxPending_ = false;
  setTempo(bpm::fromCc14(bpmRxValue_), TempoSource::Host);
}

void Engine::runTempoAnnounce() {
  if (!bpmAnnouncePending_ && !linkTransportPending_) return;
  if (now_ < nextBpmAnnounceUs_) return;
  nextBpmAnnounceUs_ = now_ + kBpmAnnounceIntervalUs;
  if (bpmAnnouncePending_) {
    bpmAnnouncePending_ = false;
    midi::Message out[2];
    const size_t n = midi::cc14(channel(), proto::cc::kBpmMsb, proto::cc::kBpmLsb, bpm::toCc14(bpm_), bpmSent_, out);
    bpmSent_ = bpm::toCc14(bpm_);
    for (size_t i = 0; i < n; ++i) emitDeck(out[i]);
  }
  if (linkTransportPending_) {
    linkTransportPending_ = false;
    sendLinkTransport();
  }
}

void Engine::doTransport(TransportFsm::Event e) { afterTransport(fsm_.handle(e), e); }

void Engine::goTransport(TransportState target) {
  switch (target) {
    case TransportState::Playing: doTransport(TransportFsm::Event::Play); break;
    case TransportState::Paused: doTransport(TransportFsm::Event::Pause); break;
    case TransportState::Stopped: doTransport(TransportFsm::Event::Stop); break;
  }
}

void Engine::afterTransport(const TransportFsm::Output& out, TransportFsm::Event e) {
  for (uint8_t i = 0; i < out.count; ++i) emitSystem(out.msg[i]);
  mtc_.onTransportEvent(e, now_);  // idempotent, so it stays in step even when the FSM ignored the event
  if (out.count == 0) return;
  if (cfg_.get(ConfigKey::MtcEnable) != 0) {
    uint8_t frame[MtcGenerator::kFullFrameBytes];
    emitSystemSysex(frame, mtc_.fullFrame(now_, frame));
  }
  sendLinkTransport();
}

// TRANSPORT is the radio's clock sync: system messages are not bridged, so a linked follower learns
// state, tempo and phase from this instead of from 0xF8 bytes. atUs is the send time (phase error is
// at most one clock tick; it feeds the follower's beat LED only).
void Engine::sendLinkTransport() {
  if (!isMaster() || !linkEnabled_ || (cfg_.get(ConfigKey::ClockOutMask) & ports::kLink) == 0) return;
  link::TransportMsg t;
  t.state = fsm_.state();
  t.bpmX10 = bpm_;
  t.positionClocks = fsm_.songPositionClocks();
  t.atUs = static_cast<uint32_t>(now_);
  link_.sendTransport(t, now_);
}

void Engine::runClock() {
  const uint32_t n = clock_.advance(now_);  // bounded by ClockGenerator::kMaxCatchUpTicks
  for (uint32_t i = 0; i < n; ++i) {
    const bool playing = fsm_.state() == TransportState::Playing;
    if ((playing ? fsm_.songPositionClocks() : freeTick_) % proto::kClockPpqn == 0) flashUntilUs_ = now_ + kBeatFlashUs;
    emitSystem(midi::clock());
    fsm_.onClockTick();
    ++freeTick_;
  }
}

void Engine::runMtc() {
  uint8_t qf[8];
  const size_t n = mtc_.advance(now_, qf, sizeof qf);
  if (cfg_.get(ConfigKey::MtcEnable) == 0) return;  // the generator keeps running so enabling mid-song is in step
  for (size_t i = 0; i < n; ++i) emitSystem(midi::mtcQuarterFrame(qf[i]));
}

void Engine::runHousekeeping() {
  if (heldPulse_.note != kNoNote && now_ >= pulseOffUs_) noteOff(&heldPulse_);
  if (heldPulseRight_.note != kNoNote && now_ >= pulseOffRightUs_) noteOff(&heldPulseRight_);
  if (toneUntilUs_ != 0 && now_ >= toneUntilUs_) {
    toneUntilUs_ = 0;
    host_.testTone(false, 0, 0);
  }
}

// ---- inbound MIDI -----------------------------------------------------------------------------

void Engine::onMidiIn(MidiPort port, const uint8_t* bytes, size_t len, uint64_t nowUs) {
  if (nowUs > now_) now_ = nowUs;
  ensureStarted();
  if (bytes == nullptr) return;
  switch (port) {
    case MidiPort::Usb0:
      for (size_t i = 0; i < len; ++i) {
        midi::Parser::Event ev;
        if (parser0_.feed(bytes[i], &ev)) handleUsb0(ev, nowUs);
      }
      break;
    case MidiPort::Usb1:  // raw pass-through to the TRS jack
      if (trsEnabled()) {
        for (size_t i = 0; i < len; i += 3) push(OutEvent::Kind::Bytes, ports::kTrs, 0, bytes + i, len - i < 3u ? len - i : 3u);
      }
      break;
    case MidiPort::Usb2:
      feedUsb2(bytes, len);
      break;
    case MidiPort::Trs:
      if (trsEnabled()) feedTrs(bytes, len, nowUs);
      break;
  }
}

void Engine::onUsbPacket(const midi::UsbPacket& p, uint64_t nowUs) {
  if (p.cable > 2) return;
  onMidiIn(static_cast<MidiPort>(p.cable), p.data, p.size(), nowUs);
}

void Engine::handleUsb0(const midi::Parser::Event& ev, uint64_t nowUs) {
  if (ev.kind == midi::Parser::Event::Kind::SysEx) {
    onSysexIn(ev.sysex, ev.sysexLen, nowUs);
  } else if (ev.kind == midi::Parser::Event::Kind::Message) {
    if (ev.message.isChannelVoice()) {
      handleHostMessage(ev.message);
    } else {
      onSystemIn(ev.message, nowUs);
    }
  }
}

void Engine::onSysexIn(const uint8_t* frame, size_t len, uint64_t nowUs) {
  if (nowUs > now_) now_ = nowUs;
  ensureStarted();
  if (frame == nullptr) return;
  server_.handle(frame, len, nowUs);
}

// Notes and controls the host (or, relayed over the radio, the host of the peer) sends to this deck.
void Engine::handleHostMessage(const midi::Message& m) {
  if (!m.isChannelVoice() || m.channel() != channel()) return;
  if (m.isNoteOn() || m.isNoteOff()) {
    const bool on = m.isNoteOn();
    switch (m.data1()) {
      case proto::note::kCue:
        cueLit_ = on;
        break;
      case proto::note::kPlay:
        // "vel > 0 = deck is playing": feedback for the LED, in every mode. Only the transport CC commands
        // a master, so a host can mirror its play state to the unit without starting its clock. A
        // velocity-0 release pauses only the mirror, so the pulse we sent cannot be echoed into a state change.
        hostPlaying_ = on;
        if (!isMaster()) {
          mirror_ = on ? TransportState::Playing : (mirror_ == TransportState::Playing ? TransportState::Paused : mirror_);
        }
        break;
      case proto::note::kTap:
        if (on) {
          const uint16_t t = tap_.tap(now_);
          if (t != 0) setTempo(t, TempoSource::Host);
        }
        break;
      default:
        break;
    }
    return;
  }
  if (!m.isControlChange()) return;
  const uint8_t cc = m.data1(), value = m.data2();
  uint16_t v14 = 0;
  if (cc == proto::cc::kTransport) {
    if (value > static_cast<uint8_t>(TransportState::Paused)) return;
    const auto target = static_cast<TransportState>(value);
    if (isMaster()) {
      goTransport(target);
    } else {
      mirror_ = target;
    }
  } else if (cc == proto::cc::kBpmNudge) {
    setTempo(bpm::nudgeFromRelative(bpm_, value), TempoSource::Host);
  } else if (bpmRx_.onControlChange(cc, value, &v14)) {
    bpmRxValue_ = v14;
    bpmRxPending_ = true;
  }
}

// Real-time and system-common bytes from a host or a drum machine on the TRS jack.
void Engine::onSystemIn(const midi::Message& m, uint64_t nowUs) {
  if (!follower_.onMessage(m, nowUs)) return;
  const uint8_t status = m.status();
  if (status == proto::kStatusClock) {
    if (!isMaster() && followTick_ % proto::kClockPpqn == 0) flashUntilUs_ = now_ + kBeatFlashUs;
    ++followTick_;
    return;
  }
  if (status == proto::kStatusStart) followTick_ = 0;
  if (status == proto::kStatusSongPosition) followTick_ = static_cast<uint32_t>(midi::join14(m.data1(), m.data2())) * 6u;
  if (!isMaster()) mirror_ = follower_.transport();
}

void Engine::feedTrs(const uint8_t* bytes, size_t len, uint64_t nowUs) {
  for (size_t i = 0; i < len; ++i) {
    midi::Parser::Event ev;
    if (!parserTrs_.feed(bytes[i], &ev)) continue;
    if (ev.kind == midi::Parser::Event::Kind::Message) {
      if (!ev.message.isChannelVoice()) onSystemIn(ev.message, nowUs);
      if (usbMounted_) pushMessage(ports::kUsb, proto::cable::kTrsMidi, ev.message);
    } else if (ev.kind == midi::Parser::Event::Kind::SysEx && usbMounted_) {
      push(OutEvent::Kind::SysEx, ports::kUsb, proto::cable::kTrsMidi, ev.sysex, ev.sysexLen);
    }
  }
}

// Cable 2 carries the bridged deck's host feedback: notes and controls go to the peer over the radio.
void Engine::feedUsb2(const uint8_t* bytes, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    midi::Parser::Event ev;
    if (!parser2_.feed(bytes[i], &ev) || ev.kind != midi::Parser::Event::Kind::Message) continue;
    const midi::Message& m = ev.message;
    if (linkEnabled_ && (m.isNoteOn() || m.isNoteOff() || m.isControlChange())) link_.relayHostMidi(m);
  }
}

void Engine::onLinkPacket(const uint8_t* data, size_t len, int8_t rssi, uint64_t rxUs, uint64_t nowUs) {
  if (nowUs > now_) now_ = nowUs;
  ensureStarted();
  if (!linkEnabled_ || data == nullptr) return;
  link_.onPacket(data, len, rssi, rxUs, nowUs);
}

void Engine::onLinkTransport(const link::TransportEvent& ev) {
  if (mode_ != Mode::SyncFollow) return;
  lt_.valid = true;
  lt_.state = ev.msg.state;
  lt_.bpmX10 = ev.msg.bpmX10;
  lt_.positionClocks = ev.msg.positionClocks;
  lt_.atUs = ev.msg.atUs;
  lt_.arrivalUs = static_cast<uint32_t>(now_);
  if (!follower_.locked(now_)) mirror_ = lt_.state;  // an external MIDI clock, when there is one, wins
}

// ---- status -----------------------------------------------------------------------------------

// Beat phase of the linked master from its last TRANSPORT message, in the master's clock when time
// sync has an estimate (so it does not drift with crystal error), otherwise from our arrival time.
bool Engine::linkBeatFlash() const {
  if (lt_.state != TransportState::Playing) return false;
  const uint32_t x10 = lt_.bpmX10;
  const link::TimeSync& ts = link_.timeSync();
  const uint32_t now32 = static_cast<uint32_t>(now_);
  uint32_t since = ts.valid() ? ts.peerTime(now32) - lt_.atUs : now32 - lt_.arrivalUs;
  if (since >= 0x80000000u) since = 0;
  const uint64_t beatUs = 600000000ull / x10;
  const uint64_t posUs = static_cast<uint64_t>(lt_.positionClocks) * 25000000ull / x10 + since;
  return posUs % beatUs < kBeatFlashUs;
}

void Engine::updateLed() {
  led_.deck = bootDeck_;
  led_.mode = mode_;
  if (!linkEnabled_) {
    led_.link = LedStatus::LinkLed::Off;
  } else if (link_.deckConflict()) {
    led_.link = LedStatus::LinkLed::Conflict;
  } else if (!link_.linkUp()) {
    led_.link = LedStatus::LinkLed::Searching;
  } else {
    led_.link = link_.bridging() ? LedStatus::LinkLed::Bridging : LedStatus::LinkLed::Up;
  }
  if (isMaster()) {
    led_.beatFlash = now_ < flashUntilUs_;
  } else if (mode_ == Mode::SyncFollow) {
    led_.beatFlash = follower_.locked(now_) ? now_ < flashUntilUs_ : (followingLink() && linkBeatFlash());
  } else {
    led_.beatFlash = false;
  }
  led_.identify = now_ < identifyUntilUs_ && ((now_ - identifyStartUs_) / kIdentifyHalfPeriodUs) % 2 == 0;
  led_.cueLit = cueLit_;
  led_.touched = jog_.touched();
  led_.transport = transport();
  // A master's own transport stays authoritative for output; the host's play note only lights the indicator.
  // (Outside SYNC_MASTER the note is already folded into mirror_.)
  if (isMaster() && hostPlaying_) led_.transport = TransportState::Playing;
  const uint32_t heldMs = gestures_.chordHeldMs(now_);
  led_.chordStage = heldMs >= ButtonGestures::kSwapMinMs ? 2 : (heldMs >= ButtonGestures::kCycleMinMs ? 1 : 0);
  led_.brightness = static_cast<uint8_t>(cfg_.get(ConfigKey::LedBrightness));
}

}  // namespace epdj
