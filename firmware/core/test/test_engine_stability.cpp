// One hour of simulated time on two linked engines with random sensors, random host MIDI, random SysEx,
// random config changes, hostile radio packets, stalls and a lossy air. Run under the sanitizers.
// Invariants: no crash or sanitizer report, every output event well formed, work per step bounded, the
// output queue never beyond its capacity and every loss counted, the engine alive afterwards.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "test_engine_sim.h"

using namespace esim;
using proto::sysex::Cmd;

namespace {

constexpr int kHourMs = 3600 * 1000;
constexpr uint32_t kMaxPushesPerStep = 64;  // 24 catch-up clocks + 8 quarter frames + a few notes, CCs, radio packets

bool wellFormed(const OutEvent& e) {
  if (e.portMask == 0 || (e.portMask & ~7u) != 0 || e.len > OutEvent::kMaxBytes) return false;
  switch (e.kind) {
    case OutEvent::Kind::Message: {
      if ((e.portMask & ports::kLink) != 0 || e.len < 1 || e.len > 3) return false;
      const uint8_t s = e.data[0];
      size_t want = 0;
      if (s >= 0x80 && s < 0xC0) want = 3;
      else if (s >= 0xC0 && s < 0xE0) want = 2;
      else if (s >= 0xE0 && s < 0xF0) want = 3;
      else if (s == 0xF1 || s == 0xF3) want = 2;
      else if (s == 0xF2) want = 3;
      else if (s == 0xF6 || s >= 0xF8) want = 1;
      if (want != e.len) return false;
      for (size_t i = 1; i < e.len; ++i) {
        if (e.data[i] >= 0x80) return false;
      }
      return true;
    }
    case OutEvent::Kind::SysEx: {
      if ((e.portMask & ports::kLink) != 0 || e.len < 2 || e.data[0] != 0xF0 || e.data[e.len - 1] != 0xF7) return false;
      for (size_t i = 1; i + 1 < e.len; ++i) {
        if (e.data[i] >= 0x80) return false;
      }
      return true;
    }
    case OutEvent::Kind::Bytes:
      return e.portMask == ports::kTrs && e.len >= 1 && e.len <= 3;
    case OutEvent::Kind::Link: {
      link::Header h;
      const uint8_t* payload = nullptr;
      return e.portMask == ports::kLink && link::decodePacket(e.data, e.len, &h, &payload) == link::DecodeError::None;
    }
  }
  return false;
}

struct Unit {
  Config cfg;
  FakePlatform plat;
  Engine engine;
  RawInputs in;
  Rng rng;
  RingSim ring;
  int64_t offset;
  uint64_t t = 1000000;  // true time of the next step
  uint8_t macLast;

  // sensor state
  int faderBase = 2000, knobBase = 2000, angle = 0;
  bool finger = false;
  double fingerRev = 0.0, fingerAmp = 1.0;
  int garbageLeft = 0;

  // bookkeeping
  uint64_t events = 0, badEvents = 0, linkPackets = 0;
  uint32_t maxStepPush = 0, maxInputPush = 0;
  // coverage: the chaos must actually visit the states the invariants are about
  uint64_t modeMs[3] = {}, linkMs[5] = {}, transportMs[3] = {};
  uint64_t clocks = 0, jogCcs = 0, notes = 0, sysexFrames = 0, bridged = 0;

  Unit(int deckId, uint8_t mac, int64_t off, uint32_t seed)
      : cfg(configFor(deckId)), engine(cfg, plat, Deck::ident(mac, static_cast<uint16_t>(500 + mac))), rng(seed), offset(off), macLast(mac) {
    in.usbMounted = deckId == 0;
    in.touchCount = 8;
    in.angleValid = true;
  }
  uint64_t local(uint64_t trueUs) const { return static_cast<uint64_t>(static_cast<int64_t>(trueUs) + offset); }
};

// Pops everything the engine has; Link events go on the air.
void drain(Unit& u, Air& air, size_t idx, bool discard = false) {
  OutEvent e;
  while (u.engine.popOut(&e)) {
    ++u.events;
    if (!wellFormed(e)) ++u.badEvents;
    if (e.kind == OutEvent::Kind::Message && e.len == 1 && e.data[0] == 0xF8) ++u.clocks;
    if (e.kind == OutEvent::Kind::Message && e.len == 3 && (e.data[0] & 0xF0) == 0xB0 && e.data[1] == 22) ++u.jogCcs;
    if (e.kind == OutEvent::Kind::Message && e.len == 3 && (e.data[0] & 0xE0) == 0x80) ++u.notes;
    if (e.kind == OutEvent::Kind::Message && e.cable == 2) ++u.bridged;
    if (e.kind == OutEvent::Kind::SysEx) ++u.sysexFrames;
    if (e.kind == OutEvent::Kind::Link && !discard) {
      ++u.linkPackets;
      air.broadcast(idx, 2, Bytes(e.data, e.data + e.len), u.t);
    }
  }
}

void randomSensors(Unit& u) {
  Rng& r = u.rng;
  if (r.below(400) == 0) u.faderBase = static_cast<int>(r.below(4096));
  else u.faderBase = std::clamp(u.faderBase + (r.below(4) == 0 ? r.range(-5, 5) : 0), 0, 4095);
  if (r.below(600) == 0) u.knobBase = static_cast<int>(r.below(4096));
  else u.knobBase = std::clamp(u.knobBase + (r.below(4) == 0 ? r.range(-5, 5) : 0), 0, 4095);
  u.in.faderRaw = static_cast<uint16_t>(std::clamp(u.faderBase + r.range(-3, 3), 0, 4095));
  u.in.knobRaw = static_cast<uint16_t>(std::clamp(u.knobBase + r.range(-3, 3), 0, 4095));
  if (r.below(30000) == 0) u.in.faderRaw = 0xFFFF;  // a broken ADC read
  if (r.below(30000) == 0) u.in.knobRaw = 0xFFFF;

  if (!u.finger && r.below(1200) == 0) {
    u.finger = true;
    u.fingerRev = r.uni();
    u.fingerAmp = 0.3 + 1.2 * r.uni();
  } else if (u.finger && r.below(700) == 0) {
    u.finger = false;
  }
  u.fingerRev += (r.below(3) == 0 ? 0.0 : (r.sym() * 0.002));
  u.angle = (u.angle + r.range(-12, 12) + 4096) & 4095;
  u.in.angle12 = static_cast<uint16_t>(u.angle);
  if (r.below(1500) == 0) u.in.angleValid = !u.in.angleValid;
  if (r.below(40000) == 0) {
    static const uint8_t counts[] = {8, 8, 8, 5, 3, 2, 1, 0};
    u.in.touchCount = counts[r.below(8)];
  }
  u.ring.read(u.fingerRev, u.finger ? u.fingerAmp : 0.0, r, u.in.touchRaw);
  if (u.garbageLeft > 0) {
    --u.garbageLeft;
    const uint32_t kind = r.below(3);
    for (uint16_t& x : u.in.touchRaw) x = kind == 0 ? 0 : (kind == 1 ? 0xFFFF : static_cast<uint16_t>(r.next()));
  } else if (r.below(6000) == 0) {
    u.garbageLeft = r.range(1, 5);
  }

  if (r.below(250) == 0) u.in.cue = !u.in.cue;
  if (r.below(250) == 0) u.in.rev = !u.in.rev;
  if (r.below(20000) == 0) u.in.usbMounted = !u.in.usbMounted;
  if (r.below(15000) == 0) u.in.dacPresent = !u.in.dacPresent;
  if (r.below(15000) == 0) u.in.audioStreaming = !u.in.audioStreaming;
}

void randomSysex(Unit& u, uint64_t nowLocal) {
  Rng& r = u.rng;
  uint8_t buf[syx::kMaxFrameBytes];
  const uint8_t deck = r.below(3) == 0 ? 127 : static_cast<uint8_t>(r.below(2));
  size_t n = 0;
  const uint32_t pick = r.below(100);
  if (pick < 50) {  // SET_CONFIG: a valid value most of the time, anything otherwise
    const int key = r.below(10) == 0 ? static_cast<int>(r.below(256)) : static_cast<int>(r.below(32));
    int32_t v = static_cast<int32_t>(r.next());
    if (key < 32 && r.below(5) != 0) {
      const proto::ConfigDesc& d = proto::kConfigTable[key];
      v = d.minValue + static_cast<int32_t>(r.below(static_cast<uint32_t>(d.maxValue - d.minValue + 1)));
    }
    if (key == static_cast<int>(ConfigKey::LinkEnable) && r.below(10) < 8) v = 1;  // the link should be up most of the time
    uint8_t p[5];
    p[0] = static_cast<uint8_t>(key);
    syx::writeI32le(v, p + 1);
    n = syx::encodeFrame(deck, static_cast<uint8_t>(Cmd::SetConfig), p, 5, buf, sizeof buf);
  } else {
    static const Cmd cmds[] = {Cmd::GetInfo, Cmd::GetConfig, Cmd::SaveConfig, Cmd::Stream,  Cmd::CalBegin, Cmd::CalEnd,
                               Cmd::SetDeck, Cmd::Reboot,    Cmd::LinkStatus, Cmd::TestTone, Cmd::Identify,  Cmd::LoadDefaults};
    uint8_t p[8];
    const size_t len = r.below(6);
    for (size_t i = 0; i < len; ++i) p[i] = static_cast<uint8_t>(r.below(4) == 0 ? r.next() : r.below(3));
    Cmd c = cmds[r.below(sizeof cmds / sizeof cmds[0])];
    if (c == Cmd::LoadDefaults && r.below(20) != 0) c = Cmd::GetInfo;  // keep the chaos from always resetting itself
    if (c == Cmd::SetDeck && r.below(10) != 0) c = Cmd::LinkStatus;
    n = syx::encodeFrame(deck, static_cast<uint8_t>(c), p, len, buf, sizeof buf);
  }
  if (n == 0) return;
  if (r.below(15) == 0) buf[r.below(static_cast<uint32_t>(n))] ^= static_cast<uint8_t>(1u << r.below(8));  // corrupt it
  u.engine.onSysexIn(buf, n, nowLocal);
}

void randomHostMidi(Unit& u, uint64_t nowLocal) {
  Rng& r = u.rng;
  const Engine::MidiPort port = static_cast<Engine::MidiPort>(r.below(4));
  uint8_t m[3];
  size_t n = 0;
  const uint32_t kind = r.below(10);
  const uint8_t ch = static_cast<uint8_t>(r.below(3));
  if (kind < 3) {  // notes the unit cares about, and some it does not
    static const uint8_t notes[] = {36, 37, 38, 39, 41, 60};
    m[0] = static_cast<uint8_t>((r.below(2) ? 0x90 : 0x80) | ch);
    m[1] = notes[r.below(6)];
    m[2] = static_cast<uint8_t>(r.below(128));
    n = 3;
  } else if (kind < 7) {
    static const uint8_t ccs[] = {29, 20, 52, 21, 22, 23, 55, 7, 24, 1};
    m[0] = static_cast<uint8_t>(0xB0 | ch);
    m[1] = ccs[r.below(10)];
    m[2] = static_cast<uint8_t>(m[1] == 29 ? r.below(5) : r.below(128));
    n = 3;
  } else if (kind < 9) {
    static const uint8_t rt[] = {0xF8, 0xF8, 0xF8, 0xFA, 0xFB, 0xFC};
    m[0] = rt[r.below(6)];
    n = 1;
  } else {
    n = r.range(1, 3);
    for (size_t i = 0; i < n; ++i) m[i] = static_cast<uint8_t>(r.next());
  }
  u.engine.onMidiIn(port, m, n, nowLocal);
}

void randomRadio(Unit& u, uint64_t nowLocal) {
  Rng& r = u.rng;
  uint8_t pkt[link::kMaxPacket];
  size_t n = 0;
  if (r.below(2) == 0) {  // noise
    n = r.below(link::kMaxPacket + 8);
    if (n > sizeof pkt) n = sizeof pkt;
    for (size_t i = 0; i < n; ++i) pkt[i] = static_cast<uint8_t>(r.next());
  } else {  // well framed, random content, from one of a few MACs (one of them is our own)
    link::Header h;
    h.type = static_cast<link::Type>(r.range(1, 7));
    h.group = r.below(8) == 0 ? 1 : 0;
    h.deck = r.below(3) == 0 ? link::kDeckNone : static_cast<uint8_t>(r.below(2));
    h.flags = static_cast<uint8_t>(r.below(8));
    h.epoch = static_cast<uint16_t>(r.below(3));
    h.seq = static_cast<uint16_t>(r.next());
    h.mac.b[0] = 2;
    h.mac.b[5] = static_cast<uint8_t>(0x0A + r.below(5));
    uint8_t payload[100];
    const size_t plen = r.below(sizeof payload);
    for (size_t i = 0; i < plen; ++i) payload[i] = static_cast<uint8_t>(r.below(4) == 0 ? r.next() : r.below(3));
    n = link::encodePacket(h, payload, plen, pkt, sizeof pkt);
  }
  if (n == 0) return;
  u.engine.onLinkPacket(pkt, n, -50, u.local(u.t), nowLocal);
}

}  // namespace

EPDJ_TEST(engine_stability_one_hour_two_units_random_everything) {
  // Both clocks cross the 32-bit microsecond wrap (71.6 min) during the hour, at different moments.
  constexpr int64_t kWrap = 4294967296ll;
  Unit a(0, 0x0A, kWrap - 1800000000ll, 101), b(1, 0x0B, kWrap - 1200000000ll + 7300000, 202);
  Unit* units[2] = {&a, &b};
  Air air(303);
  air.loss = 0.10;
  air.dup = 0.03;
  Rng ctl(404);

  uint64_t steps = 0;
  for (int ms = 0; ms < kHourMs; ++ms) {
    // Air: deliver what is due, to the unit it is for.
    while (!air.flight.empty() && air.flight.begin()->first <= a.t) {
      Air::Delivery d = std::move(air.flight.begin()->second);
      const uint64_t when = air.flight.begin()->first;
      air.flight.erase(air.flight.begin());
      Unit& to = *units[d.to];
      const uint32_t before = to.engine.stats().outPushed;
      to.engine.onLinkPacket(d.bytes.data(), d.bytes.size(), -40, to.local(when), to.local(a.t));
      to.maxInputPush = std::max(to.maxInputPush, to.engine.stats().outPushed - before);
      drain(to, air, d.to);
    }
    if (ctl.below(40000) == 0) air.partitioned = !air.partitioned;  // out of range now and then

    for (size_t i = 0; i < 2; ++i) {
      Unit& u = *units[i];
      randomSensors(u);
      const uint64_t nowLocal = u.local(u.t);
      uint32_t before = u.engine.stats().outPushed;
      if (u.rng.below(300) == 0) randomSysex(u, nowLocal);
      if (u.rng.below(100) == 0) randomHostMidi(u, nowLocal);
      if (u.rng.below(250) == 0) randomRadio(u, nowLocal);
      u.maxInputPush = std::max(u.maxInputPush, u.engine.stats().outPushed - before);
      drain(u, air, i);

      u.in.nowUs = nowLocal;
      before = u.engine.stats().outPushed;
      u.engine.step(u.in);
      u.maxStepPush = std::max(u.maxStepPush, u.engine.stats().outPushed - before);
      CHECK(u.engine.queuedBytes() <= Engine::kOutQueueBytes);
      drain(u, air, i);
      ++steps;
    }

    // Time: nearly always 1 ms; sometimes a late task, rarely a long stall of the whole firmware.
    uint64_t dt = 1000;
    if (ctl.below(200) == 0) dt = 1000 + ctl.below(4000);
    if (ctl.below(100000) == 0) dt = 50000 + ctl.below(5000000);
    a.t += dt;
    b.t += dt;

    for (Unit* u : units) {
      ++u->modeMs[static_cast<size_t>(u->engine.mode())];
      ++u->linkMs[static_cast<size_t>(u->engine.led().link)];
      ++u->transportMs[static_cast<size_t>(u->engine.transport())];
    }

    // Spot checks on the engine's own state.
    if ((ms & 1023) == 0) {
      for (Unit* u : units) {
        CHECK(u->engine.tempoX10() >= 200 && u->engine.tempoX10() <= 3000);
        CHECK(u->engine.displayTempoX10() >= 200 && u->engine.displayTempoX10() <= 3000);
        CHECK(u->engine.led().chordStage <= 2);
      }
    }
  }

  for (Unit* u : units) {
    CHECK_EQ(u->badEvents, 0);
    CHECK_EQ(u->engine.stats().outDropped, 0);  // drained every step: nothing may be lost
    CHECK_EQ(u->engine.stats().outPushed, u->engine.stats().outPopped);
    CHECK(u->maxStepPush <= kMaxPushesPerStep);
    CHECK(u->maxInputPush <= 3 * proto::kConfigCount);  // a full config dump is the largest answer to one request
    CHECK(u->events > 100000);
    for (uint64_t m : u->modeMs) CHECK(m > 60000);               // every mode for over a minute
    for (uint64_t m : u->transportMs) CHECK(m > 60000);          // stopped, playing and paused
    CHECK(u->linkMs[static_cast<size_t>(LedStatus::LinkLed::Searching)] > 60000);
    CHECK(u->linkMs[static_cast<size_t>(LedStatus::LinkLed::Up)] + u->linkMs[static_cast<size_t>(LedStatus::LinkLed::Bridging)] > 600000);
    CHECK(u->clocks > 10000 && u->jogCcs > 5000 && u->notes > 1000 && u->sysexFrames > 1000);
  }
  CHECK(a.bridged + b.bridged > 1000);  // the bridge carried real traffic
  std::printf("    coverage A: modes %llu/%llu/%llu s, link Off/Search/Up/Bridge/Conflict %llu/%llu/%llu/%llu/%llu s, clocks %llu, jog %llu, notes %llu, sysex %llu, bridged %llu\n",
              (unsigned long long)(a.modeMs[0] / 1000), (unsigned long long)(a.modeMs[1] / 1000), (unsigned long long)(a.modeMs[2] / 1000),
              (unsigned long long)(a.linkMs[0] / 1000), (unsigned long long)(a.linkMs[1] / 1000), (unsigned long long)(a.linkMs[2] / 1000),
              (unsigned long long)(a.linkMs[3] / 1000), (unsigned long long)(a.linkMs[4] / 1000), (unsigned long long)a.clocks,
              (unsigned long long)a.jogCcs, (unsigned long long)a.notes, (unsigned long long)a.sysexFrames, (unsigned long long)a.bridged);
  std::printf("    stability: %llu engine steps, events A/B %llu/%llu, radio packets %llu/%llu, max pushes in a step %u/%u, high water %u/%u bytes\n",
              static_cast<unsigned long long>(steps), static_cast<unsigned long long>(a.events),
              static_cast<unsigned long long>(b.events), static_cast<unsigned long long>(a.linkPackets),
              static_cast<unsigned long long>(b.linkPackets), a.maxStepPush, b.maxStepPush, a.engine.stats().outHighWaterBytes,
              b.engine.stats().outHighWaterBytes);

  // Overflow is bounded and counted: stop draining for a while, then drain and account for everything.
  Unit& u = a;
  for (int i = 0; i < 200; ++i) {
    uint8_t buf[syx::kMaxFrameBytes];
    const uint8_t p[1] = {0xFF};
    const size_t n = syx::encodeFrame(127, static_cast<uint8_t>(Cmd::GetConfig), p, 1, buf, sizeof buf);
    u.engine.onSysexIn(buf, n, u.local(u.t));
    CHECK(u.engine.queuedBytes() <= Engine::kOutQueueBytes);
  }
  for (int i = 0; i < 20000; ++i) {
    randomSensors(u);
    u.in.nowUs = u.local(u.t);
    u.engine.step(u.in);
    u.t += 1000;
    CHECK(u.engine.queuedBytes() <= Engine::kOutQueueBytes);
  }
  const Engine::Stats s = u.engine.stats();
  CHECK(s.outDropped > 0);
  uint32_t left = 0;
  OutEvent e;
  while (u.engine.popOut(&e)) {
    ++left;
    CHECK(wellFormed(e));
  }
  CHECK_EQ(u.engine.stats().outPushed, u.engine.stats().outPopped);
  CHECK(left > 0);
  CHECK_EQ(u.engine.queuedBytes(), 0);

  // Still alive: defaults, a USB host, quiet sensors, and a fader sweep comes out as MIDI.
  uint8_t buf[syx::kMaxFrameBytes];
  size_t n = syx::encodeFrame(127, static_cast<uint8_t>(Cmd::LoadDefaults), nullptr, 0, buf, sizeof buf);
  u.engine.onSysexIn(buf, n, u.local(u.t));
  u.in = RawInputs();
  u.in.usbMounted = true;
  u.in.faderRaw = 1000;
  u.in.knobRaw = 1000;
  u.in.touchCount = 8;
  u.ring.read(0.0, 0.0, u.rng, u.in.touchRaw);
  size_t cc = 0;
  for (int i = 0; i < 3000; ++i) {
    u.in.faderRaw = static_cast<uint16_t>(1000 + (i > 1000 ? (i - 1000) : 0));
    u.in.nowUs = u.local(u.t);
    u.engine.step(u.in);
    u.t += 1000;
    while (u.engine.popOut(&e)) cc += (e.kind == OutEvent::Kind::Message && e.len == 3 && e.data[0] == 0xB0 && (e.data[1] == 23 || e.data[1] == 55)) ? 1u : 0u;
  }
  CHECK(cc > 100);
  n = syx::encodeFrame(0, static_cast<uint8_t>(Cmd::GetInfo), nullptr, 0, buf, sizeof buf);
  u.engine.onSysexIn(buf, n, u.local(u.t));
  CHECK(u.engine.popOut(&e) && e.kind == OutEvent::Kind::SysEx);
}
