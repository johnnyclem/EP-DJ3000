// epdj_engine_sim: the real firmware Engine behind a text pipe, for the end-to-end conformance suites
// (docs/E2E.md). The Swift client libraries and the Mixxx script run against this process instead of
// against a fake written from the spec.
//
// One or two Engine instances ("units") are stepped at 1 kHz of virtual time with a sensor model in front
// (an AS5600 angle and ONE hold pad, as the default build has; or the experimental touch ring; noisy fader/knob
// ADC, buttons) and, for two units, a lossy in-process radio between them. A unit is built like the default
// firmware unless `init` asks for something else (platter=). Nothing here reads a clock: time only moves on
// `run` / `ramp`.
//
// Protocol: one command per line on stdin, `verb key=value key=value ...` (no quoting, no spaces in values).
// Every command is answered by zero or more event lines and then exactly one terminator, all JSON objects
// on single lines on stdout:
//   {"e":"out",...}     something a unit sent: t (virtual us), u (unit), k (msg|sysex|bytes|link), ports
//                       (clockPorts mask), cable, hex (the message bytes) and, for a USB port, usb (the 4-byte
//                       USB-MIDI packets as the firmware's encodeUsb produces them)
//   {"e":"platform",...} an effect the engine asked the platform for (save, reboot, set deck, tone)
//   {"e":"done",...}    the command finished; `t` is the virtual time afterwards
//   {"e":"error","msg":"..."}  the command was not understood or failed (nothing else is sent for it)
// Commands are listed in docs/E2E.md and in `commandHelp` below.
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "epdj/engine.h"
#include "epdj/midi.h"
#include "sim_models.h"

namespace {

using namespace epdj;
using esim::Bytes;
namespace ports = proto::clockPorts;

constexpr uint64_t kStartUs = 1000000;  // the engine never sees time 0
constexpr uint64_t kRestartFlushUs = 100000;  // setDeck/reboot: "give the USB FIFO time to flush first"
constexpr uint64_t kReenumerateUs = 300000;   // how long the host takes to see the unit again after a restart

// ---- output ---------------------------------------------------------------------------------------

std::string g_out;

void emit(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void emit(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list again;
  va_copy(again, ap);
  const int n = std::vsnprintf(nullptr, 0, fmt, ap);
  va_end(ap);
  if (n > 0) {
    std::string line(static_cast<size_t>(n) + 1, '\0');
    std::vsnprintf(&line[0], line.size(), fmt, again);
    line.resize(static_cast<size_t>(n));
    g_out += line;
  }
  va_end(again);
  g_out.push_back('\n');
}

void flushOut() {
  std::fwrite(g_out.data(), 1, g_out.size(), stdout);
  std::fflush(stdout);
  g_out.clear();
}

std::string hexOf(const uint8_t* d, size_t n) {
  static const char* digits = "0123456789abcdef";
  std::string s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    s.push_back(digits[d[i] >> 4]);
    s.push_back(digits[d[i] & 15]);
  }
  return s;
}

bool parseHex(const std::string& s, Bytes* out) {
  out->clear();
  if (s.size() % 2 != 0) return false;
  for (size_t i = 0; i < s.size(); i += 2) {
    char pair[3] = {s[i], s[i + 1], 0};
    char* end = nullptr;
    const long v = std::strtol(pair, &end, 16);
    if (end != pair + 2 || v < 0) return false;
    out->push_back(static_cast<uint8_t>(v));
  }
  return true;
}

// ---- command arguments ------------------------------------------------------------------------------

struct Args {
  std::string verb;
  std::vector<std::pair<std::string, std::string>> kv;

  static Args parse(const std::string& line) {
    Args a;
    size_t i = 0;
    bool first = true;
    while (i < line.size()) {
      while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
      size_t j = i;
      while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r') ++j;
      if (j == i) break;
      const std::string tok = line.substr(i, j - i);
      i = j;
      if (first) {
        a.verb = tok;
        first = false;
        continue;
      }
      const size_t eq = tok.find('=');
      if (eq == std::string::npos) a.kv.emplace_back(tok, "1");
      else a.kv.emplace_back(tok.substr(0, eq), tok.substr(eq + 1));
    }
    return a;
  }
  const std::string* find(const char* key) const {
    for (const auto& p : kv) {
      if (p.first == key) return &p.second;
    }
    return nullptr;
  }
  bool has(const char* key) const { return find(key) != nullptr; }
  double num(const char* key, double def) const {
    const std::string* s = find(key);
    return s ? std::strtod(s->c_str(), nullptr) : def;
  }
  long integer(const char* key, long def) const {
    const std::string* s = find(key);
    return s ? std::strtol(s->c_str(), nullptr, 0) : def;
  }
  std::string str(const char* key, const char* def = "") const {
    const std::string* s = find(key);
    return s ? *s : std::string(def);
  }
};

// ---- the units --------------------------------------------------------------------------------------

struct Unit;

struct SimPlatform final : EnginePlatform {
  Unit* unit = nullptr;
  void reboot(proto::RebootMode mode) override;
  void setDeck(proto::Deck deck) override;
  bool testTone(bool on, uint16_t freqHz, uint8_t attenDb) override;
  bool saveConfig(const Config& cfg) override;
};

struct Sensors {
  double fader = 2048, knob = 2048;
  int faderNoise = 2, knobNoise = 2;  // +-counts of uniform ADC noise (PROTOCOL 2.4: +-3 must not chatter)
  double fingerRev = 0, pressure = 0;
  double as5600Rev = 0, as5600Noise = 0;  // noise in 12-bit counts
  bool as5600Present = true;  // false: no chip ever answers (angleValid is always false), as when the I2C wiring is wrong
  bool as5600Valid = true;    // a chip that is there: does this read succeed?
  bool cue = false, rev = false;
  bool usbWanted = true, dac = false, audio = false;
  uint8_t touchCount = 1;  // pads wired: the hold pad of the default build. The experimental ring has 8.
};

struct Unit {
  int index = 0;
  Engine::Identity ident;
  Config cfg;
  Config stored;  // what the non-volatile store holds: written by SAVE_CONFIG / SET_DECK, read at boot
  SimPlatform plat;
  std::unique_ptr<Engine> eng;
  Sensors s;
  esim::RingSim ring;
  esim::Rng rng;
  int64_t clockOffsetUs = 0;
  uint64_t rebootAtUs = 0;  // true time; 0 = none pending
  bool rebootToBootloader = false;
  bool halted = false;      // in the ROM bootloader: no longer a MIDI device
  uint64_t usbDownUntilUs = 0;
  bool saveOk = true, toneOk = true;
  unsigned saves = 0;
  std::vector<int> deckSets;
  unsigned rebootCount = 0;

  explicit Unit(uint32_t seed) : rng(seed) { plat.unit = this; }

  uint64_t local(uint64_t trueUs) const { return static_cast<uint64_t>(static_cast<int64_t>(trueUs) + clockOffsetUs); }

  void boot() {
    cfg = stored;
    ++ident.epoch;
    eng.reset(new Engine(cfg, plat, ident));
    halted = false;
  }
};

struct World {
  std::vector<std::unique_ptr<Unit>> units;
  esim::Air air{7};
  uint64_t t = kStartUs;  // true time of the NEXT step
  bool traceLink = false;
  std::vector<Bytes> radio;  // scratch: link packets popped from the unit being drained
} g;

void SimPlatform::reboot(proto::RebootMode mode) {
  unit->rebootAtUs = g.t + kRestartFlushUs;
  unit->rebootToBootloader = mode == proto::RebootMode::Bootloader;
  emit("{\"e\":\"platform\",\"t\":%llu,\"u\":%d,\"what\":\"reboot\",\"bootloader\":%s}",
       static_cast<unsigned long long>(g.t), unit->index, unit->rebootToBootloader ? "true" : "false");
}

// Persists the new deck in the stored config only (a live tweak that was never saved must not be saved as a
// side effect, exactly as firmware/esp32s3/main/device_platform.cpp), then restarts.
void SimPlatform::setDeck(proto::Deck deck) {
  unit->stored.set(proto::ConfigKey::DeckId, static_cast<int32_t>(deck));
  unit->deckSets.push_back(static_cast<int>(deck));
  unit->rebootAtUs = g.t + kRestartFlushUs;
  unit->rebootToBootloader = false;
  emit("{\"e\":\"platform\",\"t\":%llu,\"u\":%d,\"what\":\"setDeck\",\"deck\":%d}", static_cast<unsigned long long>(g.t),
       unit->index, static_cast<int>(deck));
}

bool SimPlatform::testTone(bool on, uint16_t freqHz, uint8_t attenDb) {
  emit("{\"e\":\"platform\",\"t\":%llu,\"u\":%d,\"what\":\"tone\",\"on\":%s,\"freq\":%u,\"atten\":%u,\"ok\":%s}",
       static_cast<unsigned long long>(g.t), unit->index, on ? "true" : "false", freqHz, attenDb,
       unit->toneOk ? "true" : "false");
  return unit->toneOk;
}

bool SimPlatform::saveConfig(const Config& cfg) {
  ++unit->saves;
  if (unit->saveOk) unit->stored = cfg;
  emit("{\"e\":\"platform\",\"t\":%llu,\"u\":%d,\"what\":\"save\",\"ok\":%s}", static_cast<unsigned long long>(g.t),
       unit->index, unit->saveOk ? "true" : "false");
  return unit->saveOk;
}

// ---- running ----------------------------------------------------------------------------------------

constexpr size_t kMaxUsbPackets = (OutEvent::kMaxBytes + 2) / 3;

void emitOut(Unit& u, const OutEvent& ev, uint64_t trueUs) {
  if (ev.kind == OutEvent::Kind::Link) {
    g.radio.emplace_back(ev.data, ev.data + ev.len);
    if (!g.traceLink) return;
  }
  static const char* kinds[] = {"msg", "sysex", "bytes", "link"};
  std::string line = "{\"e\":\"out\",\"t\":" + std::to_string(trueUs) + ",\"u\":" + std::to_string(u.index) + ",\"k\":\"" +
                     kinds[static_cast<unsigned>(ev.kind)] + "\",\"ports\":" + std::to_string(ev.portMask) +
                     ",\"cable\":" + std::to_string(ev.cable) + ",\"hex\":\"" + hexOf(ev.data, ev.len) + "\"";
  midi::UsbPacket pkts[kMaxUsbPackets];
  const size_t n = encodeUsb(ev, pkts, kMaxUsbPackets);
  if (n > 0) {
    std::string usb;
    for (size_t i = 0; i < n; ++i) {
      uint8_t raw[4];
      pkts[i].toBytes(raw);
      usb += hexOf(raw, 4);
    }
    line += ",\"usb\":\"" + usb + "\"";
  }
  line += "}";
  g_out += line;
  g_out.push_back('\n');
}

void drain(Unit& u, uint64_t trueUs) {
  if (!u.eng) return;
  OutEvent ev;
  while (u.eng->popOut(&ev)) emitOut(u, ev, trueUs);
}

void flushRadio(size_t from) {
  for (const Bytes& p : g.radio) g.air.broadcast(from, g.units.size(), p, g.t);
  g.radio.clear();
}

RawInputs sample(Unit& u) {
  RawInputs in;
  in.nowUs = u.local(g.t);
  const auto adc = [&](double pos, int noise) {
    const double v = pos + (noise > 0 ? std::round(noise * u.rng.sym()) : 0.0);
    return static_cast<uint16_t>(std::lround(std::fmin(4095.0, std::fmax(0.0, v))));
  };
  in.faderRaw = adc(u.s.fader, u.s.faderNoise);
  in.knobRaw = adc(u.s.knob, u.s.knobNoise);
  in.touchCount = u.s.touchCount;
  u.ring.n = u.s.touchCount ? u.s.touchCount : 1u;
  u.ring.read(u.s.fingerRev, u.s.pressure, u.rng, in.touchRaw);
  // What the HAL reports when the encoder does not answer: angleValid false and angle12 left at 0.
  in.angleValid = u.s.as5600Present && u.s.as5600Valid;
  if (u.s.as5600Present) {
    const double a = u.s.as5600Rev * 4096.0 + u.s.as5600Noise * u.rng.sym();
    in.angle12 = static_cast<uint16_t>(static_cast<int64_t>(std::floor(a)) & 4095);
  }
  in.cue = u.s.cue;
  in.rev = u.s.rev;
  in.usbMounted = u.s.usbWanted && g.t >= u.usbDownUntilUs;
  in.audioStreaming = u.s.audio;
  in.dacPresent = u.s.dac;
  return in;
}

// One millisecond. Order mirrors the platform loop (engine_task.cpp) and esim::Rig::step: due radio packets
// are delivered first, every unit steps, then what the units radiated enters the air.
void tick() {
  while (!g.air.flight.empty() && g.air.flight.begin()->first <= g.t) {
    esim::Air::Delivery d = std::move(g.air.flight.begin()->second);
    const uint64_t when = g.air.flight.begin()->first;
    g.air.flight.erase(g.air.flight.begin());
    Unit& to = *g.units[d.to];
    if (!to.eng || to.halted) continue;
    to.eng->onLinkPacket(d.bytes.data(), d.bytes.size(), -40, to.local(when), to.local(g.t));
    drain(to, g.t);
    flushRadio(d.to);
  }
  for (size_t i = 0; i < g.units.size(); ++i) {
    Unit& u = *g.units[i];
    if (u.halted) continue;
    if (u.rebootAtUs != 0 && g.t >= u.rebootAtUs) {
      u.rebootAtUs = 0;
      ++u.rebootCount;
      if (u.rebootToBootloader) {
        u.halted = true;
        u.eng.reset();
        continue;
      }
      u.usbDownUntilUs = g.t + kReenumerateUs;
      u.boot();
      emit("{\"e\":\"platform\",\"t\":%llu,\"u\":%zu,\"what\":\"boot\",\"epoch\":%u}", static_cast<unsigned long long>(g.t), i,
           u.ident.epoch);
    }
    u.eng->step(sample(u));
    drain(u, g.t);
    flushRadio(i);
  }
  g.t += 1000;
}

// ---- commands ---------------------------------------------------------------------------------------

struct Ramp {
  enum class What { None, Fader, Knob, Finger, Pressure, As5600 } what = What::None;
  Unit* unit = nullptr;
  double from = 0, to = 0;
};

void applyRamp(const Ramp& r, double v) {
  if (!r.unit) return;
  switch (r.what) {
    case Ramp::What::Fader: r.unit->s.fader = v; break;
    case Ramp::What::Knob: r.unit->s.knob = v; break;
    case Ramp::What::Finger: r.unit->s.fingerRev = v; break;
    case Ramp::What::Pressure: r.unit->s.pressure = v; break;
    case Ramp::What::As5600: r.unit->s.as5600Rev = v; break;
    case Ramp::What::None: break;
  }
}

void runMs(long ms, const Ramp* ramp = nullptr) {
  for (long i = 1; i <= ms; ++i) {
    // The last step lands exactly on `to`: a ramp of one revolution must end on one revolution, not a rounding error short.
    if (ramp) applyRamp(*ramp, i == ms ? ramp->to : ramp->from + (ramp->to - ramp->from) * static_cast<double>(i) / static_cast<double>(ms));
    tick();
  }
}

Unit* pickUnit(const Args& a) {
  const long i = a.integer("u", 0);
  if (i < 0 || static_cast<size_t>(i) >= g.units.size()) return nullptr;
  return g.units[static_cast<size_t>(i)].get();
}

std::string cfgArray(const Config& c) {
  std::string s = "[";
  for (size_t i = 0; i < Config::kCount; ++i) {
    if (i) s += ",";
    s += std::to_string(c.get(static_cast<proto::ConfigKey>(i)));
  }
  return s + "]";
}

void doInit(const Args& a) {
  g.units.clear();
  g.air = esim::Air(static_cast<uint32_t>(a.integer("seed", 7)) * 977u + 5u);
  g.air.loss = a.num("loss", 0.0);
  g.air.dup = a.num("dup", 0.0);
  g.air.baseUs = a.num("delay", 800.0);
  g.air.jitterUs = a.num("jitter", 150.0);
  g.t = kStartUs;
  g.traceLink = false;
  const long count = std::min(2L, std::max(1L, a.integer("units", 1)));
  const long seed = a.integer("seed", 7);
  for (long i = 0; i < count; ++i) {
    std::unique_ptr<Unit> u(new Unit(static_cast<uint32_t>(seed + 31 * i)));
    u->index = static_cast<int>(i);
    char key[40];
    std::snprintf(key, sizeof key, "deck%ld", i);
    u->stored.set(proto::ConfigKey::DeckId, static_cast<int32_t>(a.integer(key, i)));
    std::snprintf(key, sizeof key, "usb%ld", i);
    u->s.usbWanted = a.integer(key, 1) != 0;
    std::snprintf(key, sizeof key, "offset%ld", i);
    u->clockOffsetUs = a.integer(key, 0);
    u->s.dac = a.integer("dac", 0) != 0;
    // `platter=` picks the hardware build: as5600 (the default: encoder + one hold pad), ring (the experimental touch
    // ring: eight pads, no encoder) or hold (one pad, no encoder). `touch=` and `as5600=` override a piece of it.
    // The platterSource CONFIG is only written when `platter=` is given; without it the unit boots on the contract
    // default, which is what a factory unit does.
    const std::string platter = a.str("platter", "as5600");
    proto::PlatterSource source = proto::PlatterSource::As5600;
    if (platter == "ring") source = proto::PlatterSource::TouchRing;
    else if (platter == "hold") source = proto::PlatterSource::TouchHoldOnly;
    else if (platter != "as5600") std::fprintf(stderr, "epdj_engine_sim: unknown platter=%s, using as5600\n", platter.c_str());
    if (a.has("platter")) u->stored.set(proto::ConfigKey::PlatterSource, static_cast<int32_t>(source));
    const bool hasEncoder = a.integer("as5600", source == proto::PlatterSource::As5600 ? 1 : 0) != 0;
    const long pads = a.integer("touch", source == proto::PlatterSource::TouchRing ? 8 : 1);
    u->s.touchCount = static_cast<uint8_t>(std::min(8L, std::max(0L, pads)));
    u->s.as5600Present = hasEncoder;
    u->ident.hwRev = 2;
    u->ident.serial = 0xA0B0C000u + static_cast<uint32_t>(i);
    // Capabilities as the platform derives them (main.cpp): the encoder answered, 3 or more pads wired.
    u->ident.caps = static_cast<uint16_t>(proto::caps::kTrsMidi | proto::caps::kLink | proto::caps::kStatusLed |
                                          (hasEncoder ? proto::caps::kAs5600 : 0) |
                                          (u->s.touchCount >= 3 ? proto::caps::kTouchRing : 0));
    u->ident.mac.b[0] = 2;
    u->ident.mac.b[5] = static_cast<uint8_t>(0x0A + i);
    u->ident.epoch = static_cast<uint16_t>(100 + i);
    u->ident.linkGroup = static_cast<uint8_t>(a.integer("group", 0));
    u->boot();
    g.units.push_back(std::move(u));
  }
}

// Splits a raw byte stream into messages and delivers each as USB-MIDI packets, the way the platform does.
bool injectMidi(Unit& u, const std::string& port, const Bytes& bytes) {
  if (!u.eng || u.halted) return false;
  const uint64_t now = u.local(g.t);
  if (port == "trs") {
    u.eng->onMidiIn(Engine::MidiPort::Trs, bytes.data(), bytes.size(), now);
    return true;
  }
  const long cable = std::strtol(port.c_str(), nullptr, 10);
  if (cable < 0 || cable > 2) return false;
  uint8_t sysexBuf[512];
  midi::Parser parser(sysexBuf, sizeof sysexBuf);
  for (uint8_t b : bytes) {
    midi::Parser::Event ev;
    if (!parser.feed(b, &ev)) continue;
    midi::UsbPacket pkts[kMaxUsbPackets];
    size_t n = 0;
    if (ev.kind == midi::Parser::Event::Kind::Message) {
      n = midi::encodePacket(static_cast<uint8_t>(cable), ev.message, &pkts[0]) ? 1u : 0u;
    } else if (ev.kind == midi::Parser::Event::Kind::SysEx) {
      n = midi::encodeSysex(static_cast<uint8_t>(cable), ev.sysex, ev.sysexLen, pkts, kMaxUsbPackets);
    }
    for (size_t i = 0; i < n; ++i) u.eng->onUsbPacket(pkts[i], now);
  }
  return true;
}

void ledJson(const Unit& u, std::string* s) {
  const LedStatus& l = u.eng->led();
  char buf[512];
  std::snprintf(buf, sizeof buf,
                "{\"deck\":%d,\"mode\":%d,\"link\":%d,\"beatFlash\":%s,\"identify\":%s,\"cueLit\":%s,\"touched\":%s,"
                "\"transport\":%d,\"chordStage\":%u,\"brightness\":%u}",
                static_cast<int>(l.deck), static_cast<int>(l.mode), static_cast<int>(l.link), l.beatFlash ? "true" : "false",
                l.identify ? "true" : "false", l.cueLit ? "true" : "false", l.touched ? "true" : "false",
                static_cast<int>(l.transport), l.chordStage, l.brightness);
  *s += buf;
}

const char* commandHelp =
    "init units= deck0= deck1= usb0= usb1= offset1= seed= loss= dup= delay= jitter= group= platter=as5600|ring|hold "
    "as5600=0|1 touch=0..8 dac=; run ms=; "
    "ramp what=fader|knob|finger|pressure|as5600 from= to= ms= [u=]; finger rev= pressure= [u=] (pressure is the hold pad, or the "
    "ring's finger; rev matters only with several pads); fader raw= noise= [u=]; "
    "knob raw= noise= [u=]; buttons cue= rev= [u=]; as5600 rev= noise= valid= [u=]; ring peak= width= noise= [u=]; "
    "usb mounted= [u=]; peripherals dac= audio= [u=]; midi port=0|1|2|trs hex= [u=]; usbpkt hex= [u=]; status [u=]; "
    "reboot [bootloader=] [u=]; platform saveok= toneok= [u=]; air loss= dup= partition= delay= jitter=; trace link=; quit";

// Returns false (after emitting an error) if the command failed.
bool execute(const Args& a) {
  const auto fail = [&](const char* why) {
    g_out.clear();
    emit("{\"e\":\"error\",\"msg\":\"%s: %s\"}", a.verb.c_str(), why);
    return false;
  };
  const std::string& v = a.verb;

  const auto doneGlobal = [&]() {
    emit("{\"e\":\"done\",\"t\":%llu}", static_cast<unsigned long long>(g.t));
    return true;
  };
  if (v == "init") {
    doInit(a);
    return doneGlobal();
  }
  if (v == "help") {
    emit("{\"e\":\"help\",\"commands\":\"%s\"}", commandHelp);
    return doneGlobal();
  }
  if (v == "run") {
    runMs(a.integer("ms", 1));
    return doneGlobal();
  }
  if (v == "air") {
    if (a.has("loss")) g.air.loss = a.num("loss", 0);
    if (a.has("dup")) g.air.dup = a.num("dup", 0);
    if (a.has("delay")) g.air.baseUs = a.num("delay", 800);
    if (a.has("jitter")) g.air.jitterUs = a.num("jitter", 150);
    if (a.has("partition")) g.air.partitioned = a.integer("partition", 0) != 0;
    return doneGlobal();
  }
  if (v == "trace") {
    g.traceLink = a.integer("link", 0) != 0;
    return doneGlobal();
  }

  Unit* u = pickUnit(a);
  if (u == nullptr) return fail("no such unit");

  if (v == "ramp") {
    Ramp r;
    r.unit = u;
    const std::string what = a.str("what");
    if (what == "fader") r.what = Ramp::What::Fader;
    else if (what == "knob") r.what = Ramp::What::Knob;
    else if (what == "finger") r.what = Ramp::What::Finger;
    else if (what == "pressure") r.what = Ramp::What::Pressure;
    else if (what == "as5600") r.what = Ramp::What::As5600;
    else return fail("what must be fader|knob|finger|pressure|as5600");
    r.from = a.num("from", 0);
    r.to = a.num("to", 0);
    applyRamp(r, r.from);
    runMs(std::max(1L, a.integer("ms", 1)), &r);
  } else if (v == "finger") {
    if (a.has("rev")) u->s.fingerRev = a.num("rev", 0);
    if (a.has("pressure")) u->s.pressure = a.num("pressure", 0);
  } else if (v == "fader") {
    if (a.has("raw")) u->s.fader = a.num("raw", 0);
    if (a.has("noise")) u->s.faderNoise = static_cast<int>(a.integer("noise", 0));
  } else if (v == "knob") {
    if (a.has("raw")) u->s.knob = a.num("raw", 0);
    if (a.has("noise")) u->s.knobNoise = static_cast<int>(a.integer("noise", 0));
  } else if (v == "buttons") {
    if (a.has("cue")) u->s.cue = a.integer("cue", 0) != 0;
    if (a.has("rev")) u->s.rev = a.integer("rev", 0) != 0;
  } else if (v == "as5600") {
    if (a.has("rev")) u->s.as5600Rev = a.num("rev", 0);
    if (a.has("noise")) u->s.as5600Noise = a.num("noise", 0);
    if (a.has("valid")) u->s.as5600Valid = a.integer("valid", 1) != 0;
  } else if (v == "ring") {
    if (a.has("peak")) u->ring.peakPermille = a.num("peak", 40);
    if (a.has("width")) u->ring.widthSectors = a.num("width", 1.5);
    if (a.has("noise")) u->ring.noisePermille = a.num("noise", 3);
  } else if (v == "usb") {
    u->s.usbWanted = a.integer("mounted", 1) != 0;
  } else if (v == "peripherals") {
    if (a.has("dac")) u->s.dac = a.integer("dac", 0) != 0;
    if (a.has("audio")) u->s.audio = a.integer("audio", 0) != 0;
  } else if (v == "platform") {
    if (a.has("saveok")) u->saveOk = a.integer("saveok", 1) != 0;
    if (a.has("toneok")) u->toneOk = a.integer("toneok", 1) != 0;
  } else if (v == "midi") {
    Bytes b;
    if (!parseHex(a.str("hex"), &b)) return fail("hex is not an even-length hex string");
    if (!injectMidi(*u, a.str("port", "0"), b)) return fail("bad port or unit not running");
    drain(*u, g.t);
    flushRadio(static_cast<size_t>(u->index));
  } else if (v == "usbpkt") {
    Bytes b;
    if (!parseHex(a.str("hex"), &b) || b.size() % 4 != 0) return fail("hex must be whole 4-byte USB-MIDI packets");
    if (!u->eng || u->halted) return fail("unit not running");
    for (size_t i = 0; i < b.size(); i += 4) {
      const midi::UsbPacket p = midi::UsbPacket::fromBytes(&b[i]);
      u->eng->onUsbPacket(p, u->local(g.t));
    }
    drain(*u, g.t);
    flushRadio(static_cast<size_t>(u->index));
  } else if (v == "reboot") {
    u->rebootAtUs = g.t;
    u->rebootToBootloader = a.integer("bootloader", 0) != 0;
    if (u->halted) {  // power cycle out of the ROM bootloader
      u->rebootToBootloader = false;
      u->halted = false;
    }
    tick();
  } else if (v != "status") {
    return fail("unknown command (try `help`)");
  }

  // Every per-unit command ends with the unit's state, so a client never needs a second round trip.
  std::string state;
  if (u->eng) {
    const Engine& e = *u->eng;
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  ",\"u\":%d,\"mode\":%d,\"transport\":%d,\"tempoX10\":%u,\"displayTempoX10\":%u,\"jogTotal\":%d,"
                  "\"linkUp\":%s,\"bridging\":%s,\"conflict\":%s,\"outDropped\":%u,\"outHighWater\":%u,\"saves\":%u,"
                  "\"reboots\":%u,\"epoch\":%u,\"halted\":%s,\"airSent\":%zu,\"airLost\":%zu",
                  u->index, static_cast<int>(e.mode()), static_cast<int>(e.transport()), e.tempoX10(), e.displayTempoX10(),
                  static_cast<int>(e.jogTotal()), e.link().linkUp() ? "true" : "false", e.link().bridging() ? "true" : "false",
                  e.link().deckConflict() ? "true" : "false", e.stats().outDropped, e.stats().outHighWaterBytes, u->saves,
                  u->rebootCount, u->ident.epoch, u->halted ? "true" : "false", g.air.sent, g.air.lost);
    state = buf;
    state += ",\"led\":";
    ledJson(*u, &state);
    state += ",\"cfg\":" + cfgArray(u->cfg) + ",\"stored\":" + cfgArray(u->stored);
    state += ",\"deckSets\":[";
    for (size_t i = 0; i < u->deckSets.size(); ++i) state += (i ? "," : "") + std::to_string(u->deckSets[i]);
    state += "]";
  } else {
    state = ",\"u\":" + std::to_string(u->index) + ",\"halted\":true";
  }
  emit("{\"e\":\"done\",\"t\":%llu%s}", static_cast<unsigned long long>(g.t), state.c_str());
  return true;
}

}  // namespace

int main() {
  std::ios::sync_with_stdio(false);
  doInit(Args::parse("init"));
  emit("{\"e\":\"ready\",\"sim\":1,\"t\":%llu,\"configKeys\":%zu}", static_cast<unsigned long long>(g.t), Config::kCount);
  flushOut();

  std::string line;
  while (std::getline(std::cin, line)) {
    const Args a = Args::parse(line);
    if (a.verb.empty() || a.verb[0] == '#') continue;
    if (a.verb == "quit") break;
    execute(a);  // always ends with a done or an error line
    flushOut();
  }
  return 0;
}
