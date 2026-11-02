// The SysEx configuration protocol driven end to end through the engine: sessions a macOS app would run.
#include <algorithm>
#include <cstdlib>
#include <memory>

#include "test_engine_sim.h"

using namespace esim;
using proto::sysex::Cmd;
using proto::sysex::Err;

namespace {

constexpr int kSettleMs = 3600;

bool isAck(const Reply& r, Cmd c) { return r.cmd == proto::sysex::kAck && r.payload.size() == 1 && r.payload[0] == static_cast<uint8_t>(c); }
bool isNak(const Reply& r, Cmd c, Err e) {
  return r.cmd == proto::sysex::kNak && r.payload.size() == 2 && r.payload[0] == static_cast<uint8_t>(c) &&
         r.payload[1] == static_cast<uint8_t>(e);
}

int32_t cfgValue(Deck& d, ConfigKey k) {
  const std::vector<Reply> r = ask(d, 127, Cmd::GetConfig, b({static_cast<int>(k)}));
  CHECK_EQ(r.size(), 1);
  if (r.size() != 1 || r[0].cmd != (0x40 | static_cast<uint8_t>(Cmd::GetConfig)) || r[0].payload.size() != 5) return -999999;
  return syx::readI32le(&r[0].payload[1]);
}

int lastPair(Deck& d, uint8_t msb, uint8_t lsb) {
  const std::vector<int> v = pairValues(usbMsgs(d.log, 0), 0, msb, lsb);
  return v.empty() ? -1 : v.back();
}

std::vector<proto::Telemetry> telemetryFrames(const std::vector<Rec>& log, std::vector<uint64_t>* times = nullptr) {
  std::vector<proto::Telemetry> v;
  for (const Rec& r : log) {
    if (r.kind != OutEvent::Kind::SysEx) continue;
    syx::Frame f;
    if (syx::decodeFrame(r.bytes.data(), r.bytes.size(), syx::kNoDeckFilter, &f) != syx::DecodeStatus::Ok) continue;
    if (f.cmd != static_cast<uint8_t>(proto::sysex::Reply::Telemetry)) continue;
    proto::Telemetry t;
    CHECK(proto::deserialize(f.payload, f.payloadLen, &t));
    v.push_back(t);
    if (times) times->push_back(r.t);
  }
  return v;
}

}  // namespace

// ---- identity and addressing ---------------------------------------------------------------------

EPDJ_TEST(engine_sysex_get_info_reports_the_live_unit) {
  Deck d(1, 0x0B);
  d.set(ConfigKey::Mode, 2);
  d.set(ConfigKey::PlatterSource, 2);
  d.run(2500);
  std::vector<Reply> r = ask(d, 127, Cmd::GetInfo);
  CHECK_EQ(r.size(), 1);
  if (r.size() == 1) {
    CHECK_EQ(r[0].cmd, 0x41);
    CHECK_EQ(r[0].deck, 1);  // replies carry the replying unit's deck
    proto::Info info;
    CHECK(proto::deserialize(r[0].payload.data(), r[0].payload.size(), &info));
    CHECK_EQ(info.protocolVersion, proto::kProtocolVersion);
    CHECK_EQ(info.fwMajor, 0);
    CHECK_EQ(info.fwMinor, 1);
    CHECK_EQ(info.hwRev, 2);
    CHECK_EQ(info.deck, 1);
    CHECK_EQ(info.mode, 2);
    CHECK_EQ(info.platterSource, 2);
    CHECK_EQ(info.serial, 0xA0B0C00Bu);
    CHECK_EQ(info.uptimeS, 2);
    CHECK_EQ(info.caps, proto::caps::kTouchRing | proto::caps::kTrsMidi | proto::caps::kLink | proto::caps::kStatusLed);
  }

  d.in.dacPresent = true;
  d.run(2);
  r = ask(d, 1, Cmd::GetInfo);
  CHECK_EQ(r.size(), 1);
  proto::Info info;
  if (r.size() == 1 && proto::deserialize(r[0].payload.data(), r[0].payload.size(), &info)) {
    CHECK((info.caps & proto::caps::kDac) != 0);  // the DAC bit follows the hardware probe
  }

  CHECK_EQ(ask(d, 0, Cmd::GetInfo).size(), 0);  // addressed to the other deck: not ours
}

EPDJ_TEST(engine_accepts_input_before_the_first_step) {
  Deck d(1, 0x0B);
  d.set(ConfigKey::Mode, 1);
  const Bytes req = request(127, Cmd::GetInfo);
  d.engine.onSysexIn(req.data(), req.size(), d.localNow());
  d.drain();
  const std::vector<Reply> r = replies(d.log);
  CHECK_EQ(r.size(), 1);
  proto::Info info;
  if (r.size() == 1 && proto::deserialize(r[0].payload.data(), r[0].payload.size(), &info)) {
    CHECK_EQ(info.deck, 1);
    CHECK_EQ(info.mode, 1);  // the stored configuration was already applied
  }
  d.hostMsg(b({0x91, 36, 127}));
  d.step();
  CHECK(d.engine.led().cueLit);
  CHECK(d.engine.led().mode == proto::Mode::SyncMaster);
}

// ---- configuration -------------------------------------------------------------------------------

EPDJ_TEST(engine_sysex_config_round_trip) {
  Deck d(0);
  d.run(10);
  const std::vector<Reply> all = ask(d, 0, Cmd::GetConfig, b({0xFF}));
  CHECK_EQ(all.size(), proto::kConfigCount);
  for (size_t i = 0; i < all.size() && i < proto::kConfigCount; ++i) {
    CHECK_EQ(all[i].cmd, 0x42);
    CHECK_EQ(all[i].payload.size(), 5);
    CHECK_EQ(all[i].payload[0], i);
    CHECK_EQ(syx::readI32le(&all[i].payload[1]), proto::kConfigTable[i].defaultValue);
  }

  std::vector<Reply> r = ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::FaderRole, 2));
  CHECK_EQ(r.size(), 1);
  if (r.size() == 1) CHECK(isAck(r[0], Cmd::SetConfig));
  CHECK_EQ(cfgValue(d, ConfigKey::FaderRole), 2);
  CHECK_EQ(d.cfg.get(ConfigKey::FaderRole), 2);

  r = ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::BpmX10, -3));  // negative values travel as two's complement
  CHECK(r.size() == 1 && isNak(r[0], Cmd::SetConfig, Err::OutOfRange));
  r = ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::TouchSectors, 9));
  CHECK(r.size() == 1 && isNak(r[0], Cmd::SetConfig, Err::OutOfRange));
  Bytes badKey = setConfigPayload(ConfigKey::DeckId, 0);
  badKey[0] = 200;
  r = ask(d, 0, Cmd::SetConfig, badKey);
  CHECK(r.size() == 1 && isNak(r[0], Cmd::SetConfig, Err::BadKey));
  r = ask(d, 0, Cmd::SetConfig, b({3, 1}));
  CHECK(r.size() == 1 && isNak(r[0], Cmd::SetConfig, Err::BadLength));
  r = ask(d, 0, static_cast<Cmd>(0x33));
  CHECK(r.size() == 1 && isNak(r[0], static_cast<Cmd>(0x33), Err::UnknownCmd));
  CHECK_EQ(cfgValue(d, ConfigKey::FaderRole), 2);  // rejected requests changed nothing

  // SAVE_CONFIG hands the live config to the platform; a flash error is reported.
  r = ask(d, 0, Cmd::SaveConfig);
  CHECK(r.size() == 1 && isAck(r[0], Cmd::SaveConfig));
  CHECK_EQ(d.plat.saves, 1);
  CHECK(d.plat.saved == d.cfg);
  d.plat.saveOk = false;
  r = ask(d, 0, Cmd::SaveConfig);
  CHECK(r.size() == 1 && isNak(r[0], Cmd::SaveConfig, Err::FlashError));

  r = ask(d, 0, Cmd::LoadDefaults);
  CHECK(r.size() == 1 && isAck(r[0], Cmd::LoadDefaults));
  CHECK_EQ(cfgValue(d, ConfigKey::FaderRole), 0);
}

EPDJ_TEST(engine_deck_identity_changes_only_after_a_restart) {
  // deckId needs a reboot (PROTOCOL 3.1): LOAD_DEFAULTS or SET_CONFIG of it must not move a running Deck B
  // to channel 1 / deck A colours while its USB name and radio identity still say B.
  Deck d(1, 0x0B);
  d.run(20);
  ask(d, 127, Cmd::LoadDefaults);
  CHECK_EQ(d.cfg.get(ConfigKey::DeckId), 0);
  ask(d, 127, Cmd::SetConfig, setConfigPayload(ConfigKey::DeckId, 0));
  d.click(true, 20);
  CHECK(d.engine.led().deck == proto::Deck::B);
  CHECK_EQ(countEq(usbMsgs(d.log, 0), b({0x91, 36, 127})), 1);  // still channel 2
  CHECK_EQ(countEq(usbMsgs(d.log, 0), b({0x90, 36, 127})), 0);
  // A fixed midiChannel still wins, live.
  d.set(ConfigKey::MidiChannel, 5);
  d.click(true, 20);
  CHECK_EQ(countEq(usbMsgs(d.log, 0), b({0x95, 36, 127})), 1);
}

EPDJ_TEST(engine_sysex_set_config_applies_live_to_the_fader) {
  Deck d(0);
  d.in.faderRaw = 3000;
  d.run(100);
  const int base = lastPair(d, 23, 55);
  CHECK_NEAR(base, 12195, 15);  // (3000-40)/4015 through the 1.5% dead zones

  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::FaderInvert, 1));  // the pot did not move
  d.run(5);
  CHECK_NEAR(lastPair(d, 23, 55), 16383 - base, 2);

  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::FaderInvert, 0));
  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::FaderRawMin, 100));
  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::FaderRawMax, 3100));
  d.run(5);
  CHECK_NEAR(lastPair(d, 23, 55), 16074, 30);  // new travel: (3000-100)/3000 = 96.7% of it, through the dead zones

  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::FaderCurve, 1));  // sharp cut: full scale from 10% travel
  d.in.faderRaw = 800;
  d.run(50);
  CHECK_EQ(lastPair(d, 23, 55), 16383);
  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::FaderCurve, 0));
  d.run(5);
  CHECK_NEAR(lastPair(d, 23, 55), 3687, 30);  // linear again: (800-100)/3000 = 23% of travel

  // Swapped min/max is a pot wired the other way round.
  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::FaderRawMin, 3100));
  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::FaderRawMax, 100));
  d.run(5);
  CHECK(lastPair(d, 23, 55) > 11000);
}

EPDJ_TEST(engine_sysex_set_config_during_a_scratch_does_not_glitch) {
  Deck d(0);
  d.ringIdle(kSettleMs);
  d.clearLog();
  // Finger down and moving; reconfigure the touch thresholds, the tap window and the invert flag on the way.
  for (int i = 0; i < 150; ++i) d.ringStep(0.2, 1.0);
  double rev = 0.2;
  auto sweep = [&](int ms, double revPerSec) {
    for (int i = 0; i < ms; ++i) {
      rev += revPerSec * 1e-3;
      d.ringStep(rev, 1.0);
    }
  };
  sweep(400, 0.4);
  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::TouchOnPermille, 15));
  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::TouchOffPermille, 9));
  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::TapMaxMs, 120));
  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::TapMoveTicks, 20));
  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::ButtonDebounceMs, 8));
  sweep(400, 0.4);
  const int64_t before = ccSum(usbMsgs(d.log, 0), 0, proto::cc::kJog);
  CHECK(before > 250);  // 0.32 rev so far

  ask(d, 0, Cmd::SetConfig, setConfigPayload(ConfigKey::JogInvert, 1));  // flips the direction from here on, mid-touch
  const int64_t totalAtFlip = d.engine.jogTotal();
  sweep(600, 0.4);
  d.run(1);
  for (int i = 0; i < 100; ++i) d.ringStep(rev, 1.0);
  d.ringIdle(300);

  const std::vector<Bytes> msgs = usbMsgs(d.log, 0);
  CHECK_EQ(countEq(msgs, b({0x90, 38, 127})), 1);  // one touch-down ...
  CHECK_EQ(countEq(msgs, b({0x80, 38, 0})), 1);    // ... and one touch-up, no spurious release/re-touch
  CHECK_EQ(ccSum(msgs, 0, proto::cc::kJog), d.engine.jogTotal());  // everything produced was delivered
  CHECK(d.engine.jogTotal() < totalAtFlip);  // continued motion now counts the other way
  // No step in the stream is a jump (a glitch would show as a lone huge value in the middle of a steady sweep).
  int maxAbs = 0;
  for (const Bytes& m : msgs) {
    if (m.size() == 3 && m[0] == 0xB0 && m[1] == proto::cc::kJog) maxAbs = std::max(maxAbs, std::abs(m[2] - 64));
  }
  CHECK(maxAbs < 20);  // 0.4 rev/s = 410 ticks/s = 0.8 ticks per 2 ms
}

// ---- telemetry -----------------------------------------------------------------------------------

EPDJ_TEST(engine_sysex_stream_reports_inputs_at_the_requested_rate) {
  Deck d(0);
  d.ringIdle(300);
  d.in.faderRaw = 1234;
  d.in.knobRaw = 2345;
  d.in.audioStreaming = true;
  d.in.cue = true;
  d.run(20);
  d.clearLog();

  std::vector<Reply> r = ask(d, 0, Cmd::Stream, b({50}));
  CHECK(!r.empty() && isAck(r[0], Cmd::Stream));  // the first frame follows in the same step
  d.run(1000);
  std::vector<uint64_t> times;
  std::vector<proto::Telemetry> f = telemetryFrames(d.log, &times);
  CHECK(f.size() >= 49 && f.size() <= 51);  // 50 Hz
  for (size_t i = 1; i < times.size(); ++i) CHECK_EQ(times[i] - times[i - 1], 20000);
  for (size_t i = 1; i < f.size(); ++i) CHECK_EQ(static_cast<uint8_t>(f[i].seq - f[i - 1].seq), 1);
  if (!f.empty()) {
    const proto::Telemetry t = f.back();
    CHECK_EQ(t.faderRaw, 1234);
    CHECK_EQ(t.knobRaw, 2345);
    CHECK_EQ(t.buttons, proto::buttons::kCue);
    CHECK_EQ(t.flags & proto::stateFlags::kUsbMounted, proto::stateFlags::kUsbMounted);
    CHECK_EQ(t.flags & proto::stateFlags::kAudioStreaming, proto::stateFlags::kAudioStreaming);
    CHECK_EQ(t.flags & proto::stateFlags::kTouched, 0);
    CHECK_EQ(t.flags & proto::stateFlags::kLinkUp, 0);
    CHECK_EQ(t.bpmX10, 1200);
    CHECK_EQ(t.transport, 0);
    CHECK_NEAR(t.faderOut, 4770, 60);
    for (int i = 0; i < 8; ++i) CHECK_EQ(t.touchRaw[i], d.in.touchRaw[i]);
  }

  // Refreshing keeps it going; without a refresh it stops after 5 s.
  d.clearLog();
  for (int i = 0; i < 3; ++i) {
    ask(d, 0, Cmd::Stream, b({50}));
    d.run(2000);
  }
  CHECK(telemetryFrames(d.log).size() > 250);
  d.clearLog();
  d.run(6000);
  const size_t sent = telemetryFrames(d.log).size();
  CHECK(sent >= 1 && sent <= 5 * 50 + 2);  // stopped 5 s after the last request
  d.clearLog();
  d.run(1000);
  CHECK_EQ(telemetryFrames(d.log).size(), 0);

  // STREAM 0 stops at once; rates above 100 Hz are clamped.
  ask(d, 0, Cmd::Stream, b({200}));
  d.clearLog();
  d.run(1000);
  CHECK_NEAR(telemetryFrames(d.log).size(), 100, 2);
  ask(d, 0, Cmd::Stream, b({0}));
  d.clearLog();
  d.run(500);
  CHECK_EQ(telemetryFrames(d.log).size(), 0);
}

EPDJ_TEST(engine_sysex_telemetry_shows_touch_angle_and_jog) {
  Deck d(0);
  d.ringIdle(kSettleMs);
  ask(d, 0, Cmd::Stream, b({100}));
  d.clearLog();
  d.ringGesture(0.25, 0.5, 1.0, 100);
  std::vector<proto::Telemetry> f = telemetryFrames(d.log);
  bool sawTouched = false, sawAngle = false;
  for (const proto::Telemetry& t : f) {
    sawTouched = sawTouched || (t.flags & proto::stateFlags::kTouched) != 0;
    sawAngle = sawAngle || (t.flags & proto::stateFlags::kAngleValid) != 0;
  }
  CHECK(sawTouched && sawAngle);
  CHECK(!f.empty());
  if (!f.empty()) {
    CHECK_EQ(f.back().jogTotal, d.engine.jogTotal());
    CHECK(f.back().jogTotal > 900);
  }
}

// ---- calibration ---------------------------------------------------------------------------------

EPDJ_TEST(engine_sysex_calibrates_the_fader) {
  Deck d(0);
  d.in.faderRaw = 2000;
  d.run(50);
  std::vector<Reply> r = ask(d, 0, Cmd::CalBegin, b({0}));
  CHECK(r.size() == 1 && isAck(r[0], Cmd::CalBegin));
  // The tracker takes the median of 3 samples, so a real end stop must be held for a few samples
  // (a hand does), while a one-sample ADC spike is not an extreme.
  for (int raw = 2000; raw >= 250; raw -= 10) {
    d.in.faderRaw = static_cast<uint16_t>(raw);
    d.step();
  }
  d.run(4);
  for (int raw = 250; raw <= 3800; raw += 10) {
    d.in.faderRaw = static_cast<uint16_t>(raw);
    if (raw == 2000) d.in.faderRaw = 4095;
    d.step();
  }
  d.run(4);
  d.in.faderRaw = 3000;
  d.run(5);

  r = ask(d, 0, Cmd::CalEnd, b({0, 1}));
  CHECK_EQ(r.size(), 1);
  if (r.size() == 1) {
    CHECK_EQ(r[0].cmd, 0x48);
    proto::CalResult res;
    CHECK(proto::deserialize(r[0].payload.data(), r[0].payload.size(), &res));
    CHECK_EQ(res.kind, 0);
    CHECK_EQ(res.applied, 1);
    CHECK_EQ(res.rawMin, 250 + 36);   // measured extremes moved in by 1% of the 3550 span
    CHECK_EQ(res.rawMax, 3800 - 36);
  }
  CHECK_EQ(d.cfg.get(ConfigKey::FaderRawMin), 286);
  CHECK_EQ(d.cfg.get(ConfigKey::FaderRawMax), 3764);

  // The new travel is live: the ends of the sweep now give the full scale.
  d.in.faderRaw = 3800;
  d.run(50);
  CHECK_EQ(lastPair(d, 23, 55), 16383);
  d.in.faderRaw = 250;
  d.run(50);
  CHECK_EQ(lastPair(d, 23, 55), 0);
  d.in.faderRaw = static_cast<uint16_t>((286 + 3764) / 2);
  d.run(50);
  CHECK_NEAR(lastPair(d, 23, 55), 8192, 20);

  // No sweep in progress: nothing to end.
  r = ask(d, 0, Cmd::CalEnd, b({0, 1}));
  CHECK(r.size() == 1 && isNak(r[0], Cmd::CalEnd, Err::CalNotActive));
}

EPDJ_TEST(engine_sysex_calibration_failure_and_no_apply_change_nothing) {
  Deck d(0);
  d.in.faderRaw = 1000;
  d.in.knobRaw = 500;
  d.run(50);

  ask(d, 0, Cmd::CalBegin, b({0}));
  for (int raw = 1000; raw <= 1500; raw += 5) {  // a 500 count sweep is not a calibration
    d.in.faderRaw = static_cast<uint16_t>(raw);
    d.step();
  }
  std::vector<Reply> r = ask(d, 0, Cmd::CalEnd, b({0, 1}));
  CHECK(r.size() == 1 && isNak(r[0], Cmd::CalEnd, Err::CalFailed));
  CHECK_EQ(d.cfg.get(ConfigKey::FaderRawMin), 40);
  CHECK_EQ(d.cfg.get(ConfigKey::FaderRawMax), 4055);

  // Knob, apply = 0: the result is reported, the config is untouched.
  ask(d, 0, Cmd::CalBegin, b({1}));
  d.in.knobRaw = 100;
  d.run(4);
  for (int raw = 100; raw <= 3900; raw += 20) {
    d.in.knobRaw = static_cast<uint16_t>(raw);
    d.step();
  }
  d.run(4);
  r = ask(d, 0, Cmd::CalEnd, b({1, 0}));
  CHECK_EQ(r.size(), 1);
  if (r.size() == 1) {
    proto::CalResult res;
    CHECK(r[0].cmd == 0x48 && proto::deserialize(r[0].payload.data(), r[0].payload.size(), &res));
    CHECK_EQ(res.kind, 1);
    CHECK_EQ(res.applied, 0);
    CHECK_EQ(res.rawMin, 100 + 38);
    CHECK_EQ(res.rawMax, 3900 - 38);
  }
  CHECK_EQ(d.cfg.get(ConfigKey::KnobRawMin), 0);
  CHECK_EQ(d.cfg.get(ConfigKey::KnobRawMax), 4095);

  // Knob with apply = 1 lands in the knob keys.
  ask(d, 0, Cmd::CalBegin, b({1}));
  d.in.knobRaw = 100;
  d.run(4);
  for (int raw = 100; raw <= 3900; raw += 20) {
    d.in.knobRaw = static_cast<uint16_t>(raw);
    d.step();
  }
  d.run(4);
  r = ask(d, 0, Cmd::CalEnd, b({1, 1}));
  CHECK_EQ(r.size(), 1);
  CHECK_EQ(d.cfg.get(ConfigKey::KnobRawMin), 138);
  CHECK_EQ(d.cfg.get(ConfigKey::KnobRawMax), 3862);

  r = ask(d, 0, Cmd::CalBegin, b({7}));  // not a control we have
  CHECK(r.size() == 1 && isNak(r[0], Cmd::CalBegin, Err::NotSupported));
  r = ask(d, 0, Cmd::CalEnd, b({0, 2}));
  CHECK(r.size() == 1 && isNak(r[0], Cmd::CalEnd, Err::OutOfRange));
}

// ---- device commands -----------------------------------------------------------------------------

EPDJ_TEST(engine_sysex_deck_reboot_and_link_status) {
  Deck d(0);
  d.run(10);
  std::vector<Reply> r = ask(d, 0, Cmd::SetDeck, b({1}));
  CHECK_EQ(r.size(), 1);
  if (r.size() == 1) {
    CHECK(isAck(r[0], Cmd::SetDeck));
    CHECK_EQ(r[0].deck, 0);  // acknowledged under the old deck
  }
  CHECK_EQ(d.plat.deckSets.size(), 1);
  if (!d.plat.deckSets.empty()) CHECK(d.plat.deckSets[0] == proto::Deck::B);
  CHECK_EQ(d.cfg.get(ConfigKey::DeckId), 1);
  r = ask(d, 0, Cmd::SetDeck, b({2}));
  CHECK(r.size() == 0 || (r.size() == 1 && r[0].cmd == proto::sysex::kNak));  // deck byte 0 no longer matches, or out of range

  r = ask(d, 1, Cmd::SetDeck, b({2}));
  CHECK(r.size() == 1 && isNak(r[0], Cmd::SetDeck, Err::OutOfRange));

  r = ask(d, 1, Cmd::Reboot, b({0}));
  CHECK(r.size() == 1 && isAck(r[0], Cmd::Reboot));
  r = ask(d, 1, Cmd::Reboot, b({1}));
  CHECK(r.size() == 1 && isAck(r[0], Cmd::Reboot));
  r = ask(d, 1, Cmd::Reboot, b({2}));
  CHECK(r.size() == 1 && isNak(r[0], Cmd::Reboot, Err::OutOfRange));
  CHECK_EQ(d.plat.reboots.size(), 2);
  if (d.plat.reboots.size() == 2) {
    CHECK(d.plat.reboots[0] == proto::RebootMode::Normal);
    CHECK(d.plat.reboots[1] == proto::RebootMode::Bootloader);
  }

  r = ask(d, 1, Cmd::LinkStatus);  // alone on the air
  CHECK_EQ(r.size(), 1);
  if (r.size() == 1) {
    CHECK_EQ(r[0].cmd, 0x4B);
    CHECK_BYTES(r[0].payload, b({0}));
  }
  d.set(ConfigKey::LinkEnable, 0);
  d.run(5);
  CHECK(d.engine.led().link == LedStatus::LinkLed::Off);
}

EPDJ_TEST(engine_sysex_test_tone_needs_a_dac_and_stops_itself) {
  Deck d(0);
  d.run(10);
  std::vector<Reply> r = ask(d, 0, Cmd::TestTone, b({1, 0xE8, 0x03, 6}));  // 1000 Hz, -6 dB
  CHECK(r.size() == 1 && isNak(r[0], Cmd::TestTone, Err::NotSupported));  // no DAC
  CHECK(d.plat.tones.empty());

  d.in.dacPresent = true;
  d.run(2);
  r = ask(d, 0, Cmd::TestTone, b({1, 0xE8, 0x03, 6}));
  CHECK(r.size() == 1 && isAck(r[0], Cmd::TestTone));
  CHECK_EQ(d.plat.tones.size(), 1);
  if (d.plat.tones.size() == 1) {
    CHECK(d.plat.tones[0].on);
    CHECK_EQ(d.plat.tones[0].freqHz, 1000);
    CHECK_EQ(d.plat.tones[0].attenDb, 6);
  }
  const uint64_t startedAt = d.t;
  d.run(9000);
  CHECK_EQ(d.plat.tones.size(), 1);
  d.run(1200);  // auto-stop 10 s after the start
  CHECK_EQ(d.plat.tones.size(), 2);
  if (d.plat.tones.size() == 2) CHECK(!d.plat.tones[1].on);
  CHECK(d.t - startedAt >= 10000000);

  r = ask(d, 0, Cmd::TestTone, b({1, 0xE8, 0x03, 6}));
  CHECK(r.size() == 1 && isAck(r[0], Cmd::TestTone));
  r = ask(d, 0, Cmd::TestTone, b({0, 0, 0, 0}));  // explicit off
  CHECK(r.size() == 1 && isAck(r[0], Cmd::TestTone));
  d.run(11000);
  CHECK_EQ(d.plat.tones.size(), 4);  // the cancelled tone does not "stop" again later

  r = ask(d, 0, Cmd::TestTone, b({1, 5, 0, 6}));  // 5 Hz
  CHECK(r.size() == 1 && isNak(r[0], Cmd::TestTone, Err::OutOfRange));
  d.plat.toneOk = false;  // platform refuses (no audio path)
  r = ask(d, 0, Cmd::TestTone, b({1, 0xE8, 0x03, 6}));
  CHECK(r.size() == 1 && isNak(r[0], Cmd::TestTone, Err::NotSupported));
}

EPDJ_TEST(engine_sysex_identify_blinks_the_led) {
  Deck d(0);
  d.run(10);
  CHECK(!d.engine.led().identify);
  std::vector<Reply> r = ask(d, 0, Cmd::Identify, b({3}));
  CHECK(r.size() == 1 && isAck(r[0], Cmd::Identify));
  int edges = 0;
  bool prev = false, on = false;
  for (int i = 0; i < 3600; ++i) {
    d.step();
    on = d.engine.led().identify;
    if (on != prev) ++edges;
    prev = on;
  }
  CHECK(edges >= 11 && edges <= 13);  // 2 Hz for 3 s
  CHECK(!on);

  ask(d, 0, Cmd::Identify, b({20}));
  d.run(300);
  ask(d, 0, Cmd::Identify, b({0}));
  d.run(5);
  CHECK(!d.engine.led().identify);
}

// ---- framing, robustness -------------------------------------------------------------------------

EPDJ_TEST(engine_sysex_survives_usb_packetisation_and_garbage) {
  Deck d(0);
  d.run(10);

  // The request split across USB packets with a clock byte in the middle, as a host may send it.
  const Bytes req = request(0, Cmd::GetInfo);
  size_t from = d.log.size();
  d.engine.onMidiIn(Engine::MidiPort::Usb0, req.data(), 3, d.localNow());
  const uint8_t clk = 0xF8;
  d.engine.onMidiIn(Engine::MidiPort::Usb0, &clk, 1, d.localNow());
  d.engine.onMidiIn(Engine::MidiPort::Usb0, req.data() + 3, req.size() - 3, d.localNow());
  d.step();
  CHECK_EQ(replies(d.log, from).size(), 1);

  // Through real USB-MIDI event packets.
  midi::UsbPacket pk[12];
  const size_t n = midi::encodeSysex(0, req.data(), req.size(), pk, 12);
  CHECK(n > 0);
  from = d.log.size();
  for (size_t i = 0; i < n; ++i) d.engine.onUsbPacket(pk[i], d.localNow());
  d.step();
  CHECK_EQ(replies(d.log, from).size(), 1);

  // Corrupt requests are dropped silently.
  Bytes bad = req;
  bad[bad.size() - 2] ^= 0x01;  // checksum
  from = d.log.size();
  d.engine.onSysexIn(bad.data(), bad.size(), d.localNow());
  Bytes wrongVer = req;
  wrongVer[4] = 2;
  d.engine.onSysexIn(wrongVer.data(), wrongVer.size(), d.localNow());
  const Bytes cut(req.begin(), req.end() - 3);
  d.engine.onSysexIn(cut.data(), cut.size(), d.localNow());
  d.engine.onSysexIn(nullptr, 0, d.localNow());
  d.step();
  CHECK_EQ(replies(d.log, from).size(), 0);

  // Replies are never answered, so two linked units cannot ping-pong.
  const std::vector<Reply> info = ask(d, 0, Cmd::GetInfo);
  CHECK_EQ(info.size(), 1);
  from = d.log.size();
  for (size_t i = 0; i < from; ++i) {
    if (d.log[i].kind == OutEvent::Kind::SysEx) d.engine.onSysexIn(d.log[i].bytes.data(), d.log[i].bytes.size(), d.localNow());
  }
  d.step();
  CHECK_EQ(replies(d.log, from).size(), 0);

  // A SysEx longer than the assembly buffer is dropped without damaging the stream that follows.
  Bytes huge(400, 0x11);
  huge.front() = 0xF0;
  huge.back() = 0xF7;
  from = d.log.size();
  d.midiIn(Engine::MidiPort::Usb0, huge);
  d.midiIn(Engine::MidiPort::Usb0, req);
  d.step();
  CHECK_EQ(replies(d.log, from).size(), 1);

  // Configuration is a cable-0 service: the same bytes on another cable are not a request.
  from = d.log.size();
  d.midiIn(Engine::MidiPort::Usb2, req);
  d.midiIn(Engine::MidiPort::Usb1, req);
  d.midiIn(Engine::MidiPort::Trs, req);
  CHECK_EQ(replies(d.log, from).size(), 0);

  // Random bytes of every kind on every port: no crash, and the engine still answers afterwards.
  Rng rng(77);
  for (int i = 0; i < 20000; ++i) {
    Bytes junk(static_cast<size_t>(rng.range(1, 9)));
    for (uint8_t& x : junk) x = static_cast<uint8_t>(rng.next());
    d.midiIn(static_cast<Engine::MidiPort>(rng.below(4)), junk);
    if (i % 50 == 0) d.step();
  }
  const std::vector<Reply> after = ask(d, 127, Cmd::GetInfo);
  CHECK(after.size() >= 1);
}

EPDJ_TEST(engine_outputs_round_trip_through_usb_packets) {
  Deck d(0);
  d.set(ConfigKey::Mode, 1);
  d.ringIdle(kSettleMs);
  d.clearLog();
  d.ringGesture(0.3, 0.5, 0.5, 100);
  d.click(true, 20);
  for (int raw = 100; raw < 3000; raw += 20) {
    d.in.faderRaw = static_cast<uint16_t>(raw);
    d.step();
  }
  ask(d, 0, Cmd::GetConfig, b({0xFF}));
  ask(d, 0, Cmd::GetInfo);
  d.hostMsg(b({0xB0, 29, 1}));
  d.run(200);

  size_t usbEvents = 0, packets = 0;
  for (const Rec& r : d.log) {
    if (r.kind == OutEvent::Kind::Link || !(r.portMask & ports::kUsb)) continue;
    OutEvent ev;
    ev.kind = r.kind;
    ev.portMask = r.portMask;
    ev.cable = r.cable;
    ev.len = static_cast<uint16_t>(r.bytes.size());
    for (size_t i = 0; i < r.bytes.size(); ++i) ev.data[i] = r.bytes[i];
    midi::UsbPacket pk[64];
    const size_t n = encodeUsb(ev, pk, 64);
    CHECK(n > 0);
    ++usbEvents;
    packets += n;
    // Parse the packets back: exactly the original message comes out.
    uint8_t buf[syx::kMaxFrameBytes];
    midi::Parser p(buf, sizeof buf);
    Bytes got;
    for (size_t i = 0; i < n; ++i) {
      CHECK_EQ(pk[i].cable, r.cable);
      p.feed(pk[i], [&](const midi::Parser::Event& e) {
        if (e.kind == midi::Parser::Event::Kind::Message) got.assign(e.message.b, e.message.b + e.message.len);
        if (e.kind == midi::Parser::Event::Kind::SysEx) got.assign(e.sysex, e.sysex + e.sysexLen);
      });
    }
    CHECK_BYTES(got, r.bytes);
  }
  CHECK(usbEvents > 200);
  CHECK(packets > usbEvents);

  OutEvent radio;
  radio.kind = OutEvent::Kind::Link;
  radio.portMask = ports::kLink;
  radio.len = 4;
  midi::UsbPacket one[2];
  CHECK_EQ(encodeUsb(radio, one, 2), 0);  // not a USB event
  OutEvent trs;
  trs.kind = OutEvent::Kind::Bytes;
  trs.portMask = ports::kTrs;
  trs.len = 1;
  CHECK_EQ(encodeUsb(trs, one, 2), 0);
}

EPDJ_TEST(engine_output_queue_is_bounded_and_counts_what_it_drops) {
  Deck d(0);
  d.run(10);
  const Bytes dump = request(0, Cmd::GetConfig, b({0xFF}));
  OutEvent ev;
  while (d.engine.popOut(&ev)) {}

  // Three full config dumps fit without anyone draining.
  for (int i = 0; i < 3; ++i) d.engine.onSysexIn(dump.data(), dump.size(), d.localNow());
  CHECK_EQ(d.engine.stats().outDropped, 0);
  size_t frames = 0;
  while (d.engine.popOut(&ev)) ++frames;
  CHECK_EQ(frames, 3 * proto::kConfigCount);

  // A HAL that stops draining loses the newest events, and every loss is counted.
  const uint32_t pushedBefore = d.engine.stats().outPushed;
  for (int i = 0; i < 30; ++i) d.engine.onSysexIn(dump.data(), dump.size(), d.localNow());
  const Engine::Stats s = d.engine.stats();
  CHECK(s.outDropped > 0);
  CHECK(d.engine.queuedBytes() <= Engine::kOutQueueBytes);
  CHECK_EQ(s.outPushed - pushedBefore + s.outDropped, 30 * proto::kConfigCount);  // every frame is either queued or counted
  CHECK(s.outHighWaterBytes <= Engine::kOutQueueBytes);
  while (d.engine.popOut(&ev)) {}
  CHECK_EQ(d.engine.queuedBytes(), 0);
}
