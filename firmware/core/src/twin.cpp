#include "epdj/twin.h"

namespace epdj {
namespace twin {
namespace {

constexpr uint64_t kDebounceUs = 4000;
constexpr uint64_t kPlayOffUs = 12000;     // protocol: the play pulse off is at least 10 ms later
constexpr uint64_t kPlayHoldUs = 600000;  // longer than this is the card-browser hold, not a play tap
constexpr uint8_t kChA = 0;
constexpr uint8_t kChB = 1;

struct Edge {
  uint16_t bit;
  uint8_t channel;
  uint8_t note;
  bool pulse;
};

// CUE A is not here. The engine sends it.
constexpr Edge kEdges[] = {
    {kPlayA, kChA, proto::note::kPlay, true}, {kInA, kChA, kLoopIn, false},
    {kOutA, kChA, kLoopOut, false},           {kPadA0, kChA, kPad0, false},
    {kPadA1, kChA, static_cast<uint8_t>(kPad0 + 1), false},
    {kPadA2, kChA, static_cast<uint8_t>(kPad0 + 2), false},
    {kPadA3, kChA, static_cast<uint8_t>(kPad0 + 3), false},
    {kCueB, kChB, proto::note::kCue, false},  {kPlayB, kChB, proto::note::kPlay, true},
    {kInB, kChB, kLoopIn, false},             {kOutB, kChB, kLoopOut, false},
    {kPadB0, kChB, kPad0, false},
    {kPadB1, kChB, static_cast<uint8_t>(kPad0 + 1), false},
    {kPadB2, kChB, static_cast<uint8_t>(kPad0 + 2), false},
    {kPadB3, kChB, static_cast<uint8_t>(kPad0 + 3), false},
};

const Edge* findEdge(uint16_t bit) {
  for (const Edge& e : kEdges)
    if (e.bit == bit) return &e;
  return nullptr;
}

}  // namespace

Bank::Bank() {
  TouchRing::Config rc;
  rc.sectors = TouchRing::kMinSectors;
  rc.equalizeGains = false;
  ring_.configure(rc);

  JogTracker::Config jc;
  jc.source = proto::PlatterSource::As5600;
  jog_.configure(jc);

  AnalogControl::Config ac;
  pitch_.configure(ac);
  cross_.configure(ac);
}

void Bank::setJogInvert(bool invert) {
  if (jogInvert_ == invert) return;
  jogInvert_ = invert;
  JogTracker::Config jc;
  jc.source = proto::PlatterSource::As5600;
  jc.invert = invert;
  jog_.configure(jc);
}

void Bank::setPitchInvert(bool invert) {
  if (pitchInvert_ == invert) return;
  pitchInvert_ = invert;
  AnalogControl::Config ac;
  ac.invert = invert;
  pitch_.configure(ac);
}

void Bank::onMidi(const midi::Message& m) {
  if (!m.isNoteOn() && !m.isNoteOff()) return;
  const uint8_t note = m.data1();
  if (note != kLoopIn && note != kLoopOut) return;
  const uint8_t ch = m.channel();
  if (ch > 1) return;
  const uint8_t bit = static_cast<uint8_t>((ch == 0 ? 0 : 2) + (note == kLoopOut ? 1 : 0));
  if (m.isNoteOn()) ledLatch_ = static_cast<uint8_t>(ledLatch_ | static_cast<uint8_t>(1u << bit));
  else ledLatch_ = static_cast<uint8_t>(ledLatch_ & static_cast<uint8_t>(~(1u << bit)));
}

void Bank::push(Out* out, const midi::Message& m) {
  if (out->count >= sizeof out->msg / sizeof out->msg[0]) return;
  out->msg[out->count++] = m;
}

uint16_t Bank::debounce(uint16_t raw, uint64_t nowUs) {
  for (unsigned i = 0; i < 16; ++i) {
    const uint16_t bit = static_cast<uint16_t>(1u << i);
    if (nowUs < lockUntil_[i] || (raw & bit) == (down_ & bit)) continue;
    down_ = static_cast<uint16_t>((down_ & static_cast<uint16_t>(~bit)) | (raw & bit));
    lockUntil_[i] = nowUs + kDebounceUs;
  }
  return down_;
}

void Bank::emitEdge(Out* out, uint16_t bit, uint64_t nowUs, bool press) {
  const Edge* e = findEdge(bit);
  if (e == nullptr) return;
  if (e->pulse) {
    if (press) {
      playDownAt_[e->channel] = nowUs;
      return;
    }
    if (nowUs - playDownAt_[e->channel] >= kPlayHoldUs) return;
    push(out, midi::noteOn(e->channel, e->note, 127));
    playPending_[e->channel] = true;
    playOffAt_[e->channel] = nowUs + kPlayOffUs;
    return;
  }
  if (press) push(out, midi::noteOn(e->channel, e->note, 127));
  else push(out, midi::noteOff(e->channel, e->note, 0));
}

void Bank::step(const Sample& in, Out* out) {
  out->count = 0;

  const uint16_t prev = down_;
  const uint16_t now = debounce(in.buttons, in.nowUs);
  if (!in.muteButtons) {
    const uint16_t pressed = static_cast<uint16_t>(now & static_cast<uint16_t>(~prev));
    const uint16_t released = static_cast<uint16_t>(prev & static_cast<uint16_t>(~now));
    for (unsigned i = 0; i < 16; ++i) {
      const uint16_t bit = static_cast<uint16_t>(1u << i);
      if (pressed & bit) emitEdge(out, bit, in.nowUs, true);
      if (released & bit) emitEdge(out, bit, in.nowUs, false);
    }
  }
  for (int slot = 0; slot < 2; ++slot) {
    if (!playPending_[slot] || in.nowUs < playOffAt_[slot]) continue;
    playPending_[slot] = false;
    if (!in.muteButtons) push(out, midi::noteOff(static_cast<uint8_t>(slot), proto::note::kPlay, 0));
  }

  uint8_t heldLeds = 0;
  if (now & kInA) heldLeds = static_cast<uint8_t>(heldLeds | kLedInA);
  if (now & kOutA) heldLeds = static_cast<uint8_t>(heldLeds | kLedOutA);
  if (now & kInB) heldLeds = static_cast<uint8_t>(heldLeds | kLedInB);
  if (now & kOutB) heldLeds = static_cast<uint8_t>(heldLeds | kLedOutB);
  out->leds = static_cast<uint8_t>(heldLeds | ledLatch_);

  const uint16_t raw[TouchRing::kMinSectors] = {in.touchB, in.touchB, in.touchB};
  const TouchRing::Result tr = ring_.update(raw, in.nowUs);
  const bool touched = sticky_.update(tr.touched, in.nowUs);

  JogTracker::Input ji;
  ji.touched = touched;
  ji.angleValid = in.angleValid;
  ji.angle = in.angle12;
  const JogTracker::Output jo = jog_.update(ji, in.nowUs);
  sticky_.noteMotion(jo.ticks != 0, in.nowUs);
  if (jo.touchDown) {
    touchEver_ = true;
    push(out, midi::noteOn(kChB, proto::note::kTouch, 127));
  }
  if (jo.touchUp) push(out, midi::noteOff(kChB, proto::note::kTouch, 0));

  const bool sendJog = jo.ticks != 0 && (!touchEver_ || touched);
  if (sendJog) emit_.add(jo.ticks);
  uint8_t jogVal = 0;
  if (emit_.poll(in.nowUs, &jogVal)) push(out, midi::controlChange(kChB, proto::cc::kJog, jogVal));

  const AnalogControl::Result pb = pitch_.update(in.pitchB, in.nowUs);
  if (pb.changed) {
    push(out, midi::controlChange(kChB, proto::cc::kPitchMsb, midi::msb14(pb.value)));
    push(out, midi::controlChange(kChB, proto::cc::kPitchLsb, midi::lsb14(pb.value)));
  }
  const AnalogControl::Result xf = cross_.update(in.cross, in.nowUs);
  if (xf.changed) {
    push(out, midi::controlChange(kChA, proto::cc::kCrossfaderMsb, midi::msb14(xf.value)));
    push(out, midi::controlChange(kChA, proto::cc::kCrossfaderLsb, midi::lsb14(xf.value)));
  }
}

}  // namespace twin
}  // namespace epdj
