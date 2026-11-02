// Shared simulation helpers for the engine tests: a deck (Engine + Config + fake platform + recorded
// output), synthetic platter sensors, a lossy two-unit radio, and queries over the recorded output.
// Header-only, host tests only. Time is always simulated: nothing here reads a clock.
//
// A Deck is the unit the firmware ships by default: contract defaults (platterSource = AS5600), the AS5600 on
// the bus and ONE touch pad wired as the hold pad. A test about the experimental touch ring says so with
// Deck::useRing(); nothing else in here assumes a ring.
#pragma once
#include <cmath>
#include <cstdint>
#include <memory>
#include <map>
#include <vector>

#include "epdj/engine.h"
#include "epdj/sysex.h"
#include "sim_models.h"
#include "test_harness.h"

namespace esim {

using namespace epdj;
namespace ports = proto::clockPorts;
using proto::ConfigKey;

struct FakePlatform final : EnginePlatform {
  std::vector<proto::RebootMode> reboots;
  std::vector<proto::Deck> deckSets;
  struct Tone {
    bool on;
    uint16_t freqHz;
    uint8_t attenDb;
  };
  std::vector<Tone> tones;
  bool toneOk = true, saveOk = true;
  unsigned saves = 0;
  Config saved;

  void reboot(proto::RebootMode m) override { reboots.push_back(m); }
  void setDeck(proto::Deck d) override { deckSets.push_back(d); }
  bool testTone(bool on, uint16_t f, uint8_t a) override {
    tones.push_back({on, f, a});
    return toneOk;
  }
  bool saveConfig(const Config& c) override {
    ++saves;
    if (!saveOk) return false;
    saved = c;
    return true;
  }
};

// One recorded OutEvent. t is the true (simulation) time of the step during which it was popped.
struct Rec {
  uint64_t t;
  OutEvent::Kind kind;
  uint8_t portMask, cable;
  Bytes bytes;
};

inline Config configFor(int deckId) {
  Config c;
  c.set(ConfigKey::DeckId, deckId);
  return c;
}

// A complete unit. `clockOffsetUs` makes its microsecond counter differ from true time, so multi-unit
// tests do not accidentally rely on a shared clock.
struct Deck {
  Config cfg;
  FakePlatform plat;
  Engine engine;
  RawInputs in;
  int64_t clockOffsetUs;
  uint64_t t = 1000000;  // true time of the NEXT step
  std::vector<Rec> log;
  std::vector<Bytes> radioOut;  // Link packets popped since the last takeRadio()
  RingSim ring;  // the hold pad (n = 1) until useRing()
  Rng rng;
  double angle = 0.0;      // AS5600 platter, absolute, in 1/4096 rev (may exceed a lap); in.angle12 is its 12-bit floor
  bool ringWired = false;  // useRing() was called: eight pads, platterSource = TOUCH_RING

  static Engine::Identity ident(uint8_t macLast, uint16_t epoch) {
    Engine::Identity id;
    id.hwRev = 2;
    id.serial = 0xA0B0C000u + macLast;
    id.caps = proto::caps::kAs5600 | proto::caps::kTrsMidi | proto::caps::kLink | proto::caps::kStatusLed;
    id.mac.b[0] = 2;
    id.mac.b[5] = macLast;
    id.epoch = epoch;
    return id;
  }

  explicit Deck(int deckId, uint8_t macLast = 0x0A, int64_t offsetUs = 0, uint32_t seed = 7)
      : cfg(configFor(deckId)),
        engine(cfg, plat, ident(macLast, static_cast<uint16_t>(100 + macLast))),
        clockOffsetUs(offsetUs),
        rng(seed) {
    in.usbMounted = true;
    in.touchCount = 1;  // the hold pad
    in.faderRaw = 2048;
    in.knobRaw = 2048;
    in.angleValid = true;
    ring.n = 1;
    // An idle pad, so a step before anything is set up reads a sane baseline.
    padRead(false);
  }

  uint64_t local(uint64_t trueUs) const { return static_cast<uint64_t>(static_cast<int64_t>(trueUs) + clockOffsetUs); }
  uint64_t localNow() const { return local(t); }

  void set(ConfigKey k, int32_t v) {
    CHECK(cfg.set(k, v) == ConfigStatus::Ok);
  }

  void drain() {
    OutEvent ev;
    while (engine.popOut(&ev)) {
      Rec r{t, ev.kind, ev.portMask, ev.cable, Bytes(ev.data, ev.data + ev.len)};
      if (ev.kind == OutEvent::Kind::Link) radioOut.push_back(r.bytes);
      log.push_back(std::move(r));
    }
  }

  // One step at the current time, then advance by stepUs. The recorded time of its output is the
  // step's own time.
  void step(uint64_t stepUs = 1000) {
    in.nowUs = localNow();
    engine.step(in);
    drain();
    t += stepUs;
  }
  void run(int ms) {
    for (int i = 0; i < ms; ++i) step();
  }

  // ---- the AS5600 build: one hold pad, motion from the encoder ----------------------------------------------

  // The hold pad (touchCount = 1): idle noise, or a finger on it (a clean 40 per-mille rise).
  void padRead(bool finger) { ring.read(0.0, finger ? 1.0 : 0.0, rng, in.touchRaw); }
  void padStep(bool finger) {
    CHECK(!ringWired);
    padRead(finger);
    step();
  }
  void padIdle(int ms) {
    for (int i = 0; i < ms; ++i) padStep(false);
  }
  // One tick in which the platter turns by `units` (1/4096 rev, clockwise positive), finger on the hold pad or not.
  void turn(double units, bool finger = false) {
    angle += units;
    in.angle12 = static_cast<uint16_t>(static_cast<int64_t>(std::floor(angle)) & 4095);
    padStep(finger);
  }
  void spin(double unitsPerMs, int ms, bool finger = false) {
    for (int i = 0; i < ms; ++i) turn(unitsPerMs, finger);
  }
  // The same, to an absolute angle (exact at the end of a gesture: no rounding error accumulates).
  void turnTo(double absUnits, bool finger = false) {
    angle = absUnits;
    in.angle12 = static_cast<uint16_t>(static_cast<int64_t>(std::floor(angle)) & 4095);
    padStep(finger);
  }

  // ---- the experimental touch ring ---------------------------------------------------------------------------

  // Wires eight pads and selects platterSource = TOUCH_RING explicitly: the ring is not the default platter.
  void useRing() {
    set(ConfigKey::PlatterSource, static_cast<int32_t>(proto::PlatterSource::TouchRing));
    ringWired = true;
    in.touchCount = 8;
    ring.n = 8;
    ring.read(0.0, 0.0, rng, in.touchRaw);
  }
  // Ring tick: the finger at `rev` with `amplitude` (0 = no finger).
  void ringStep(double rev, double amplitude) {
    CHECK(ringWired);
    ring.read(rev, amplitude, rng, in.touchRaw);
    step();
  }
  void ringIdle(int ms) {
    for (int i = 0; i < ms; ++i) ringStep(0.0, 0.0);
  }
  // Land at startRev, dwell, move `revs` at revPerSec (negative = counter-clockwise), dwell, lift.
  void ringGesture(double startRev, double revPerSec, double revs, int dwellMs, int tailMs = 150) {
    for (int i = 0; i < dwellMs; ++i) ringStep(startRev, 1.0);
    const double dir = revPerSec < 0 ? -1.0 : 1.0;
    const int ms = static_cast<int>(std::lround(revs / std::fabs(revPerSec) * 1000.0));
    for (int i = 1; i <= ms; ++i) ringStep(startRev + dir * std::fabs(revPerSec) * i * 1e-3, 1.0);
    for (int i = 0; i < tailMs; ++i) ringStep(startRev + dir * revs, 1.0);
    ringIdle(150);
  }

  // ---- whichever platter this deck has ------------------------------------------------------------------------

  // A finger on the platter that does not move (ring: where the last gesture left it; AS5600: the hold pad).
  void touchStep(bool finger) {
    if (ringWired) ringStep(0.4, finger ? 1.0 : 0.0);
    else padStep(finger);
  }
  void touchIdle(int ms) {
    for (int i = 0; i < ms; ++i) touchStep(false);
  }
  // Land, dwell, scratch `revs` at revPerSec (negative = counter-clockwise), dwell, lift. On the ring the finger lands at
  // startRev; the AS5600 build has no such place (the hold pad is one pad) and the platter simply starts turning from
  // wherever it is, which is also what a real one does.
  void gesture(double startRev, double revPerSec, double revs, int dwellMs, int tailMs = 150) {
    if (ringWired) return ringGesture(startRev, revPerSec, revs, dwellMs, tailMs);
    const double dir = revPerSec < 0 ? -1.0 : 1.0;
    const int ms = static_cast<int>(std::lround(revs / std::fabs(revPerSec) * 1000.0));
    const double from = angle;
    for (int i = 0; i < dwellMs; ++i) padStep(true);
    for (int i = 1; i <= ms; ++i) turnTo(from + dir * revs * 4096.0 * i / ms, true);
    for (int i = 0; i < tailMs; ++i) padStep(true);
    padIdle(150);
  }

  void pressCue(bool down) { in.cue = down; }
  void pressRev(bool down) { in.rev = down; }
  void click(bool cue, int holdMs) {
    (cue ? in.cue : in.rev) = true;
    run(holdMs);
    (cue ? in.cue : in.rev) = false;
    run(holdMs);
  }

  void midiIn(Engine::MidiPort p, const Bytes& b) {
    engine.onMidiIn(p, b.data(), b.size(), localNow());
    drain();
  }
  void hostMsg(const Bytes& b) { midiIn(Engine::MidiPort::Usb0, b); }

  std::vector<Bytes> takeRadio() {
    std::vector<Bytes> r;
    r.swap(radioOut);
    return r;
  }
  void clearLog() { log.clear(); }
};

// ---- queries over recorded output --------------------------------------------------------------

// Messages (Kind::Message) on USB `cable`, as byte strings, optionally only those since log index `from`.
inline std::vector<Bytes> usbMsgs(const std::vector<Rec>& log, int cable, size_t from = 0) {
  std::vector<Bytes> v;
  for (size_t i = from; i < log.size(); ++i) {
    const Rec& r = log[i];
    if (r.kind == OutEvent::Kind::Message && (r.portMask & ports::kUsb) && r.cable == cable) v.push_back(r.bytes);
  }
  return v;
}
inline std::vector<Bytes> trsMsgs(const std::vector<Rec>& log) {
  std::vector<Bytes> v;
  for (const Rec& r : log) {
    if (r.kind == OutEvent::Kind::Message && (r.portMask & ports::kTrs)) v.push_back(r.bytes);
  }
  return v;
}
inline std::vector<Rec> sysexRecs(const std::vector<Rec>& log, uint8_t portBit = ports::kUsb) {
  std::vector<Rec> v;
  for (const Rec& r : log) {
    if (r.kind == OutEvent::Kind::SysEx && (r.portMask & portBit)) v.push_back(r);
  }
  return v;
}

inline Bytes b(std::initializer_list<int> l) {
  Bytes v;
  for (int x : l) v.push_back(static_cast<uint8_t>(x));
  return v;
}

inline size_t countEq(const std::vector<Bytes>& v, const Bytes& m) {
  size_t n = 0;
  for (const Bytes& x : v) n += x == m ? 1u : 0u;
  return n;
}

// Sum of the relative values of CC `cc` on channel `ch`, over messages [from, end) of `v`.
inline int64_t ccSum(const std::vector<Bytes>& v, uint8_t ch, uint8_t cc) {
  int64_t s = 0;
  for (const Bytes& m : v) {
    if (m.size() == 3 && m[0] == (0xB0 | ch) && m[1] == cc) s += static_cast<int>(m[2]) - 64;
  }
  return s;
}

inline std::vector<Bytes> onlyStatus(const std::vector<Bytes>& v, uint8_t status) {
  std::vector<Bytes> r;
  for (const Bytes& m : v) {
    if (!m.empty() && m[0] == status) r.push_back(m);
  }
  return r;
}

// Messages that are not real-time clock bytes: the "interesting" traffic of a SYNC_MASTER session.
inline std::vector<Bytes> withoutClock(const std::vector<Bytes>& v) {
  std::vector<Bytes> r;
  for (const Bytes& m : v) {
    if (!(m.size() == 1 && m[0] == 0xF8)) r.push_back(m);
  }
  return r;
}

inline Bytes flatten(const std::vector<Bytes>& v) {
  Bytes r;
  for (const Bytes& m : v) r.insert(r.end(), m.begin(), m.end());
  return r;
}

// True if any message anywhere starts with a system real-time/common status (clock, start, ...).
inline bool anySystemMessage(const std::vector<Rec>& log) {
  for (const Rec& r : log) {
    if (r.kind == OutEvent::Kind::Message && !r.bytes.empty() && r.bytes[0] >= 0xF1 && (r.portMask & (ports::kUsb | ports::kTrs))) {
      if (r.cable == proto::cable::kTrsMidi && (r.portMask & ports::kUsb)) continue;  // pass-through traffic
      return true;
    }
    if (r.kind == OutEvent::Kind::SysEx && r.bytes.size() > 3 && r.bytes[1] == 0x7F) return true;  // MTC full frame
  }
  return false;
}

// Last element, or -1 for none: a failed CHECK must report, not crash on an empty vector.
inline int lastOr(const std::vector<int>& v) { return v.empty() ? -1 : v.back(); }

// ---- SysEx request / reply helpers ---------------------------------------------------------------

inline Bytes request(uint8_t deck, proto::sysex::Cmd cmd, const Bytes& payload = {}) {
  uint8_t buf[syx::kMaxFrameBytes];
  const size_t n = syx::encodeFrame(deck, static_cast<uint8_t>(cmd), payload.data(), payload.size(), buf, sizeof buf);
  CHECK(n > 0);
  return Bytes(buf, buf + n);
}
inline Bytes setConfigPayload(ConfigKey k, int32_t v) {
  Bytes p(5);
  p[0] = static_cast<uint8_t>(k);
  syx::writeI32le(v, &p[1]);
  return p;
}

struct Reply {
  uint8_t deck, cmd;
  Bytes payload;
};
inline std::vector<Reply> replies(const std::vector<Rec>& log, size_t from = 0) {
  std::vector<Reply> v;
  for (size_t i = from; i < log.size(); ++i) {
    const Rec& r = log[i];
    if (r.kind != OutEvent::Kind::SysEx || !(r.portMask & ports::kUsb) || r.cable != proto::cable::kDeck) continue;
    syx::Frame f;
    if (syx::decodeFrame(r.bytes.data(), r.bytes.size(), syx::kNoDeckFilter, &f) != syx::DecodeStatus::Ok) continue;
    v.push_back({f.deck, f.cmd, Bytes(f.payload, f.payload + f.payloadLen)});
  }
  return v;
}

// Sends one request through onSysexIn, steps once, returns the replies it produced.
inline std::vector<Reply> ask(Deck& d, uint8_t deckByte, proto::sysex::Cmd cmd, const Bytes& payload = {}) {
  const size_t from = d.log.size();
  const Bytes req = request(deckByte, cmd, payload);
  d.engine.onSysexIn(req.data(), req.size(), d.localNow());
  d.step();
  return replies(d.log, from);
}

// Decoded 14-bit values of the pair (msb, lsb) on `ch`, one per message that changed it, in order. `seed`
// is what the receiver already holds (a sender skips an unchanged half, so a log cut mid-session needs it).
inline std::vector<int> pairValues(const std::vector<Bytes>& v, uint8_t ch, uint8_t msbCc, uint8_t lsbCc, int seed = 0) {
  midi::Cc14Receiver rx(msbCc, lsbCc);
  uint16_t tmp = 0;
  rx.onControlChange(msbCc, midi::msb14(static_cast<uint16_t>(seed)), &tmp);
  rx.onControlChange(lsbCc, midi::lsb14(static_cast<uint16_t>(seed)), &tmp);
  std::vector<int> out;
  for (const Bytes& m : v) {
    if (m.size() != 3 || m[0] != (0xB0 | ch)) continue;
    uint16_t val = 0;
    if (rx.onControlChange(m[1], m[2], &val)) out.push_back(val);
  }
  return out;
}

// ---- two units on a lossy radio ------------------------------------------------------------------

struct Rig {
  std::vector<Deck*> decks;
  Air air;
  explicit Rig(uint32_t seed) : air(seed) {}

  // All decks step at the same true time; due packets are delivered first, packets produced by this
  // step enter the air after it.
  void step() {
    const uint64_t now = decks.empty() ? 0 : decks[0]->t;
    while (!air.flight.empty() && air.flight.begin()->first <= now) {
      Air::Delivery d = std::move(air.flight.begin()->second);
      const uint64_t when = air.flight.begin()->first;
      air.flight.erase(air.flight.begin());
      Deck& to = *decks[d.to];
      to.engine.onLinkPacket(d.bytes.data(), d.bytes.size(), -40, to.local(when), to.local(now));
      to.drain();  // callbacks may have produced output
    }
    for (size_t i = 0; i < decks.size(); ++i) {
      decks[i]->step();
      for (const Bytes& p : decks[i]->takeRadio()) air.broadcast(i, decks.size(), p, now);
    }
  }
  void run(int ms) {
    for (int i = 0; i < ms; ++i) step();
  }
};

}  // namespace esim
