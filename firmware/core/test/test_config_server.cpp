// SysEx command handler: every command, error codes, deck filtering, streaming and its expiry.
#include <string>
#include <vector>

#include "epdj/config_server.h"
#include "epdj/midi.h"
#include "epdj/protocol.h"
#include "epdj/sysex.h"
#include "test_harness.h"
#include "vectors.generated.h"

using namespace epdj;
using proto::ConfigKey;
using proto::sysex::Err;
using Bytes = std::vector<uint8_t>;
using Result = ConfigServer::Result;

namespace {

struct Reply {
  uint8_t deck = 0;
  uint8_t cmd = 0;
  Bytes payload;
  Bytes raw;
};

// Records everything the server and the platform do, in order, so ordering rules can be asserted.
struct Log {
  std::vector<std::string> events;
  size_t count(const std::string& e) const {
    size_t n = 0;
    for (const auto& x : events) n += x == e ? 1 : 0;
    return n;
  }
  int indexOf(const std::string& e) const {
    for (size_t i = 0; i < events.size(); ++i) {
      if (events[i] == e) return static_cast<int>(i);
    }
    return -1;
  }
};

struct Sink : ReplySink {
  Log* log = nullptr;
  std::vector<Reply> replies;
  bool accept = true;
  bool send(const uint8_t* frame, size_t len) override {
    if (log) log->events.push_back("reply");
    if (!accept) return false;
    syx::Frame f;
    CHECK(syx::decodeFrame(frame, len, syx::kNoDeckFilter, &f) == syx::DecodeStatus::Ok);
    Reply r;
    r.deck = f.deck;
    r.cmd = f.cmd;
    r.payload.assign(f.payload, f.payload + f.payloadLen);
    r.raw.assign(frame, frame + len);
    replies.push_back(r);
    return true;
  }
};

struct Actions : DeviceActions {
  Log* log = nullptr;
  int reboots = 0;
  proto::RebootMode lastReboot = proto::RebootMode::Normal;
  int setDecks = 0;
  proto::Deck lastDeck = proto::Deck::A;
  bool toneResult = true;
  int tones = 0;
  bool lastToneOn = false;
  uint16_t lastFreq = 0;
  uint8_t lastAtten = 0;
  int identifies = 0;
  uint8_t lastIdentify = 0;
  bool saveResult = true;
  int saves = 0;
  Config savedConfig;
  proto::Info info;
  proto::Telemetry telemetry;
  std::vector<proto::Peer> peers;
  int telemetryCalls = 0;

  void reboot(proto::RebootMode m) override { ++reboots; lastReboot = m; if (log) log->events.push_back("reboot"); }
  void setDeck(proto::Deck d) override { ++setDecks; lastDeck = d; if (log) log->events.push_back("setDeck"); }
  bool testTone(bool on, uint16_t f, uint8_t a) override { ++tones; lastToneOn = on; lastFreq = f; lastAtten = a; return toneResult; }
  void identify(uint8_t s) override { ++identifies; lastIdentify = s; }
  bool saveConfig(const Config& c) override { ++saves; savedConfig = c; return saveResult; }
  void getInfo(proto::Info* out) override { *out = info; }
  void getTelemetry(proto::Telemetry* out) override {
    ++telemetryCalls;
    *out = telemetry;
    out->seq = 0xEE;  // must be overwritten by the server
  }
  size_t getPeers(proto::Peer* out, size_t max) override {
    size_t n = 0;
    for (; n < peers.size() && n < max; ++n) out[n] = peers[n];
    return peers.size();  // honest about how many exist, even if more than max
  }
};

struct Cal : CalibrationHost {
  bool beginResult = true;
  EndStatus endResult = EndStatus::Ok;
  uint16_t min = 120, max = 3970;                // the range apply = 1 writes (the host's inset included)
  uint16_t measuredMin = 100, measuredMax = 4000;  // what the sweep actually saw
  int begins = 0, ends = 0;
  proto::CalKind lastBegin = proto::CalKind::Fader, lastEnd = proto::CalKind::Fader;
  bool begin(proto::CalKind k) override { ++begins; lastBegin = k; return beginResult; }
  EndStatus end(proto::CalKind k, Range* out) override {
    ++ends;
    lastEnd = k;
    out->rawMin = min;
    out->rawMax = max;
    out->measuredMin = measuredMin;
    out->measuredMax = measuredMax;
    return endResult;
  }
};

struct Rig {
  Log log;
  Config config;
  Sink sink;
  Actions actions;
  Cal cal;
  ConfigServer server;
  uint64_t now = 1000000;

  explicit Rig(bool withCal = true)
      : server(config, actions, sink, withCal ? static_cast<CalibrationHost*>(&cal) : nullptr) {
    sink.log = &log;
    actions.log = &log;
    actions.info.fwMajor = 0; actions.info.fwMinor = 1; actions.info.fwPatch = 2;
    actions.info.hwRev = 1;
    actions.info.caps = 62;
    actions.info.serial = 0xDEADBEEF;
    actions.info.uptimeS = 3600;
    actions.info.mode = 99;           // must be overwritten from the config
    actions.info.deck = 99;
    actions.info.protocolVersion = 0;
    actions.info.platterSource = 99;
  }

  Result send(uint8_t deck, uint8_t cmd, const Bytes& payload) {
    Bytes f(syx::kMaxFrameBytes);
    f.resize(syx::encodeFrame(deck, cmd, payload.data(), payload.size(), f.data(), f.size()));
    return server.handle(f.data(), f.size(), now);
  }
  Result sendRaw(const Bytes& f) { return server.handle(f.data(), f.size(), now); }
  const Reply& only() {
    CHECK_EQ(sink.replies.size(), 1u);
    static Reply empty;
    return sink.replies.empty() ? empty : sink.replies.back();
  }
  void expectNak(uint8_t cmd, Err err) {
    const Reply& r = only();
    CHECK_EQ(r.cmd, 0x7F);
    CHECK_BYTES(r.payload, (Bytes{cmd, static_cast<uint8_t>(err)}));
  }
  void expectAck(uint8_t cmd) {
    const Reply& r = only();
    CHECK_EQ(r.cmd, 0x7E);
    CHECK_BYTES(r.payload, (Bytes{cmd}));
  }
  void clear() { sink.replies.clear(); log.events.clear(); }
};

Bytes i32(int32_t v) {
  Bytes b(4);
  syx::writeI32le(v, b.data());
  return b;
}
Bytes cat(Bytes a, const Bytes& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

}  // namespace

// ---- GET_INFO ----

EPDJ_TEST(get_info_reply_is_byte_exact_with_the_reference_vector) {
  Rig r;
  r.config.set(ConfigKey::DeckId, 1);
  r.config.set(ConfigKey::PlatterSource, static_cast<int32_t>(proto::PlatterSource::TouchRing));  // not the default: the vector says TOUCH_RING
  // Reference "info reply": deck B, mode DJ, TOUCH_RING, fw 0.1.2, hw 1, caps 62, serial DEADBEEF, 1 h up.
  const auto& v = epdj_vec::frameVectors()[7];
  CHECK_EQ(v.cmd, 0x41);
  CHECK(r.send(1, 0x01, {}) == Result::Replied);
  CHECK_BYTES(r.only().raw, v.frame);
}

EPDJ_TEST(get_info_fills_identity_from_the_live_config) {
  Rig r;
  r.config.set(ConfigKey::Mode, 2);
  r.send(0, 0x01, {});
  proto::Info info;
  CHECK(proto::deserialize(r.only().payload.data(), r.only().payload.size(), &info));
  CHECK_EQ(info.protocolVersion, proto::kProtocolVersion);
  CHECK_EQ(info.deck, 0);
  CHECK_EQ(info.mode, 2);
  CHECK_EQ(info.platterSource, static_cast<int>(proto::PlatterSource::As5600));  // nothing set: the contract default
  CHECK_EQ(info.serial, 0xDEADBEEFu);

  // ... and it follows the live config, not a constant.
  for (proto::PlatterSource s : {proto::PlatterSource::TouchRing, proto::PlatterSource::TouchHoldOnly, proto::PlatterSource::As5600}) {
    r.config.set(ConfigKey::PlatterSource, static_cast<int32_t>(s));
    r.clear();
    r.send(0, 0x01, {});
    CHECK(proto::deserialize(r.only().payload.data(), r.only().payload.size(), &info));
    CHECK_EQ(info.platterSource, static_cast<int>(s));
  }
}

// ---- GET_CONFIG ----

EPDJ_TEST(get_config_single_key) {
  Rig r;
  r.config.set(ConfigKey::FaderRawMax, 4001);
  r.send(0, 0x02, {13});
  const Reply& rep = r.only();
  CHECK_EQ(rep.cmd, 0x42);
  CHECK_BYTES(rep.payload, cat({13}, i32(4001)));
}

EPDJ_TEST(get_config_dump_all_sends_one_frame_per_key_in_order_then_an_ack) {
  Rig r;
  r.config.set(ConfigKey::BpmX10, 1285);
  r.config.set(ConfigKey::MidiChannel, 5);
  CHECK(r.send(0, 0x02, {0xFF}) == Result::Replied);
  CHECK_EQ(r.sink.replies.size(), proto::kConfigCount + 1);
  if (r.sink.replies.size() != proto::kConfigCount + 1) return;
  for (size_t i = 0; i < proto::kConfigCount; ++i) {
    CHECK_EQ(r.sink.replies[i].cmd, 0x42);
    CHECK_EQ(r.sink.replies[i].payload[0], i);
    CHECK_EQ(syx::readI32le(r.sink.replies[i].payload.data() + 1), r.config.get(static_cast<ConfigKey>(i)));
  }
  CHECK_EQ(syx::readI32le(r.sink.replies[23].payload.data() + 1), 1285);
  CHECK_EQ(syx::readI32le(r.sink.replies[1].payload.data() + 1), 5);
  CHECK_EQ(syx::readI32le(r.sink.replies[32].payload.data() + 1), 0);  // linkGroup, the last key
  // ACK[GET_CONFIG] is the end marker: it follows the last key and nothing else does.
  CHECK_EQ(r.sink.replies.back().cmd, 0x7E);
  CHECK_BYTES(r.sink.replies.back().payload, (Bytes{0x02}));
  CHECK_EQ(r.sink.replies.back().deck, 0);

  // A single-key read is just the CONFIG frame, no end marker.
  r.clear();
  r.send(0, 0x02, {32});
  CHECK_EQ(r.only().cmd, 0x42);
}

EPDJ_TEST(get_config_errors) {
  Rig r;
  r.send(0, 0x02, {33});
  r.expectNak(0x02, Err::BadKey);
  r.clear();
  r.send(0, 0x02, {0xFE});
  r.expectNak(0x02, Err::BadKey);
  r.clear();
  r.send(0, 0x02, {});
  r.expectNak(0x02, Err::BadLength);
  r.clear();
  r.send(0, 0x02, {1, 2});
  r.expectNak(0x02, Err::BadLength);
}

// ---- SET_CONFIG ----

EPDJ_TEST(set_config_applies_live_and_acks_exactly_like_the_vectors) {
  Rig r;
  const auto& v = epdj_vec::frameVectors();
  // "setConfig platterSource=1 deck B" is addressed to deck B: a deck-A unit stays silent, a B unit acks.
  CHECK(r.sendRaw(v[2].frame) == Result::NotForUs);
  CHECK(r.sink.replies.empty());
  r.config.set(ConfigKey::DeckId, 1);
  r.config.set(ConfigKey::PlatterSource, 2);
  CHECK(r.sendRaw(v[2].frame) == Result::Replied);
  r.expectAck(0x03);
  CHECK_EQ(r.config.get(ConfigKey::PlatterSource), 1);

  // "setConfig faderRawMax=4055 any deck" is a broadcast.
  Rig a;
  a.config.set(ConfigKey::FaderRawMax, 100);
  CHECK(a.sendRaw(v[3].frame) == Result::Replied);
  CHECK_EQ(a.config.get(ConfigKey::FaderRawMax), 4055);
  CHECK_EQ(a.only().deck, 0);  // replies carry THIS unit's deck, not 127
}

EPDJ_TEST(set_config_replies_match_ack_and_nak_vectors_byte_for_byte) {
  Rig r;
  // ack for setConfig (deck 0, payload [3])
  r.send(0, 0x03, cat({3}, i32(1)));
  CHECK_BYTES(r.only().raw, epdj_vec::frameVectors()[5].frame);
  r.clear();
  // nak(setConfig, OUT_OF_RANGE)
  r.send(0, 0x03, cat({3}, i32(7)));
  CHECK_BYTES(r.only().raw, epdj_vec::frameVectors()[6].frame);
  CHECK_EQ(r.config.get(ConfigKey::PlatterSource), 1);  // rejected value not applied
}

EPDJ_TEST(set_config_negative_value_from_vector_is_out_of_range) {
  Rig r;
  const auto& v = epdj_vec::frameVectors()[4];  // key 23 (bpmX10) = -5
  CHECK(r.sendRaw(v.frame) == Result::Replied);
  r.expectNak(0x03, Err::OutOfRange);
  CHECK_EQ(r.config.get(ConfigKey::BpmX10), 1200);
}

EPDJ_TEST(set_config_errors) {
  Rig r;
  r.send(0, 0x03, cat({33}, i32(0)));
  r.expectNak(0x03, Err::BadKey);
  r.clear();
  r.send(0, 0x03, cat({255}, i32(0)));
  r.expectNak(0x03, Err::BadKey);
  r.clear();
  r.send(0, 0x03, cat({12}, i32(4096)));
  r.expectNak(0x03, Err::OutOfRange);
  r.clear();
  r.send(0, 0x03, cat({12}, i32(INT32_MIN)));
  r.expectNak(0x03, Err::OutOfRange);
  r.clear();
  r.send(0, 0x03, {12, 1, 0, 0});  // 4 bytes
  r.expectNak(0x03, Err::BadLength);
  r.clear();
  r.send(0, 0x03, {12, 1, 0, 0, 0, 0});  // 6 bytes
  r.expectNak(0x03, Err::BadLength);
  CHECK_EQ(r.config.get(ConfigKey::FaderRawMin), 40);
}

EPDJ_TEST(set_config_boundaries_for_every_key) {
  Rig r;
  for (size_t i = 0; i < proto::kConfigCount; ++i) {
    const auto& d = proto::kConfigTable[i];
    const uint8_t id = static_cast<uint8_t>(i);
    if (d.key == ConfigKey::DeckId) continue;  // never settable: see set_config_of_deck_id_is_not_supported...
    for (int32_t v : {d.minValue, d.maxValue}) {
      r.clear();
      r.send(0, 0x03, cat({id}, i32(v)));
      r.expectAck(0x03);
      CHECK_EQ(r.config.get(d.key), v);
    }
    r.clear();
    r.send(0, 0x03, cat({id}, i32(d.maxValue + 1)));
    r.expectNak(0x03, Err::OutOfRange);
    r.clear();
    r.send(0, 0x03, cat({id}, i32(d.minValue - 1)));
    r.expectNak(0x03, Err::OutOfRange);
  }
  CHECK_EQ(r.config.get(ConfigKey::LinkGroup), 255);  // the key added in contract v1.1 is settable over its whole range
}

EPDJ_TEST(set_config_of_deck_id_is_not_supported_and_has_no_side_effect) {
  // The deck changes only through SET_DECK, which persists and restarts. SET_CONFIG must not move the
  // unit the way it used to (it changed who the unit answers to, with nothing stored and no restart).
  for (int deck : {0, 1}) {
    Rig r;
    r.config.set(ConfigKey::DeckId, deck);
    const uint32_t rev = r.config.revision();
    for (int32_t v : {0, 1, 2, -1, INT32_MAX}) {  // in range or not, the answer is the same
      r.clear();
      CHECK(r.send(static_cast<uint8_t>(deck), 0x03, cat({0}, i32(v))) == Result::Replied);
      r.expectNak(0x03, Err::NotSupported);
      CHECK_EQ(r.only().deck, deck);
    }
    CHECK_EQ(r.config.get(ConfigKey::DeckId), deck);
    CHECK_EQ(r.config.revision(), rev);
    CHECK_EQ(r.actions.setDecks + r.actions.reboots + r.actions.saves, 0);
    r.clear();
    CHECK(r.send(static_cast<uint8_t>(deck), 0x01, {}) == Result::Replied);  // still answering to its own deck
    r.clear();
    r.send(static_cast<uint8_t>(deck), 0x03, Bytes{0, 0, 0, 0});
    r.expectNak(0x03, Err::BadLength);  // the length check still comes first
  }
}

// ---- SAVE_CONFIG / LOAD_DEFAULTS ----

EPDJ_TEST(save_config_persists_the_live_config) {
  Rig r;
  r.config.set(ConfigKey::BpmX10, 1285);
  r.send(0, 0x04, {});
  r.expectAck(0x04);
  CHECK_EQ(r.actions.saves, 1);
  CHECK_EQ(r.actions.savedConfig.get(ConfigKey::BpmX10), 1285);
}

EPDJ_TEST(save_config_flash_error) {
  Rig r;
  r.actions.saveResult = false;
  r.send(0, 0x04, {});
  r.expectNak(0x04, Err::FlashError);
  r.clear();
  r.send(0, 0x04, {1});
  r.expectNak(0x04, Err::BadLength);
  CHECK_EQ(r.actions.saves, 1);  // the malformed request did not save
}

EPDJ_TEST(load_defaults_resets_live_config_without_saving) {
  Rig r;
  r.config.set(ConfigKey::BpmX10, 1285);
  r.config.set(ConfigKey::FaderRawMin, 99);
  r.send(0, 0x05, {});
  r.expectAck(0x05);
  CHECK(r.config == Config());
  CHECK_EQ(r.actions.saves, 0);
}

EPDJ_TEST(load_defaults_keeps_the_deck_so_the_unit_keeps_answering) {
  // Deck B: LOAD_DEFAULTS used to reset deckId to 0, after which requests to deck 1 went unanswered
  // and replies were stamped as deck A.
  Rig r;
  r.config.set(ConfigKey::DeckId, 1);
  r.config.set(ConfigKey::BpmX10, 1285);
  r.config.set(ConfigKey::FaderRole, 2);
  r.config.set(ConfigKey::LinkGroup, 7);
  CHECK(r.send(1, 0x05, {}) == Result::Replied);
  r.expectAck(0x05);
  CHECK_EQ(r.only().deck, 1);
  Config expected;
  expected.set(ConfigKey::DeckId, 1);
  CHECK(r.config == expected);  // everything else is back to its default, linkGroup included
  CHECK_EQ(r.actions.saves + r.actions.setDecks + r.actions.reboots, 0);
  r.clear();
  CHECK(r.send(1, 0x01, {}) == Result::Replied);
  CHECK_EQ(r.only().deck, 1);
  r.clear();
  CHECK(r.send(0, 0x01, {}) == Result::NotForUs);
  r.clear();
  CHECK(r.send(127, 0x02, {0xFF}) == Result::Replied);
  CHECK_EQ(r.sink.replies.size(), proto::kConfigCount + 1);
  for (const Reply& rep : r.sink.replies) CHECK_EQ(rep.deck, 1);
}

// ---- STREAM ----

EPDJ_TEST(stream_sends_ack_first_then_telemetry_at_the_requested_rate) {
  Rig r;
  r.actions.telemetry.faderRaw = 2048;
  r.actions.telemetry.jogTotal = -70000;
  r.send(0, 0x06, {50});
  r.expectAck(0x06);
  CHECK(r.server.streaming());
  CHECK_EQ(r.server.streamRateHz(), 50);

  // 1 s of ticks at 1 kHz: 50 Hz -> 50 frames (the first is due immediately).
  r.clear();
  for (int i = 0; i < 1000; ++i) {
    r.server.tick(r.now);
    r.now += 1000;
  }
  CHECK_EQ(r.sink.replies.size(), 50u);
  for (size_t i = 0; i < r.sink.replies.size(); ++i) {
    const Reply& rep = r.sink.replies[i];
    CHECK_EQ(rep.cmd, 0x47);
    proto::Telemetry t;
    CHECK(proto::deserialize(rep.payload.data(), rep.payload.size(), &t));
    CHECK_EQ(t.seq, i);  // server-owned sequence number, not whatever the provider left
    CHECK_EQ(t.faderRaw, 2048);
    CHECK_EQ(t.jogTotal, -70000);
  }
}

EPDJ_TEST(stream_rate_is_clamped_to_100_hz) {
  Rig r;
  r.send(0, 0x06, {255});
  r.expectAck(0x06);
  CHECK_EQ(r.server.streamRateHz(), 100);
  r.clear();
  for (int i = 0; i < 1000; ++i) {
    r.server.tick(r.now);
    r.now += 1000;
  }
  CHECK_EQ(r.sink.replies.size(), 100u);
}

EPDJ_TEST(stream_seq_wraps_at_256) {
  Rig r;
  r.send(0, 0x06, {100});
  r.clear();
  for (int i = 0; i < 3000; ++i) {
    r.server.tick(r.now);
    r.now += 1000;
    if (i % 1000 == 999) r.send(0, 0x06, {100});  // refresh so it does not expire
  }
  size_t telemetry = 0;
  for (const auto& rep : r.sink.replies) {
    if (rep.cmd != 0x47) continue;
    proto::Telemetry t;
    CHECK(proto::deserialize(rep.payload.data(), rep.payload.size(), &t));
    CHECK_EQ(t.seq, telemetry & 0xFF);
    ++telemetry;
  }
  CHECK(telemetry > 256);
}

EPDJ_TEST(stream_zero_stops_and_expires_after_5s_without_refresh) {
  Rig r;
  r.send(0, 0x06, {10});
  r.clear();
  r.server.tick(r.now);
  CHECK_EQ(r.sink.replies.size(), 1u);

  r.clear();
  r.send(0, 0x06, {0});
  r.expectAck(0x06);
  CHECK(!r.server.streaming());
  CHECK_EQ(r.server.streamRateHz(), 0);
  r.clear();
  r.now += 200000;
  r.server.tick(r.now);
  CHECK(r.sink.replies.empty());
}

EPDJ_TEST(stream_auto_expires_5s_after_the_last_request) {
  Rig r;
  const uint64_t t0 = r.now;
  r.send(0, 0x06, {20});
  r.clear();
  uint64_t lastFrame = 0;
  for (uint64_t t = t0; t < t0 + 7000000; t += 1000) {
    const size_t before = r.sink.replies.size();
    r.server.tick(t);
    if (r.sink.replies.size() > before) lastFrame = t;
  }
  CHECK(lastFrame >= t0 + 4900000);
  CHECK(lastFrame < t0 + 5000000);  // nothing at or after the 5 s mark
  CHECK(!r.server.streaming());
  const size_t n = r.sink.replies.size();
  CHECK(n >= 99 && n <= 101);  // 20 Hz for ~5 s
}

EPDJ_TEST(stream_refresh_extends_expiry_and_keeps_the_frame_phase) {
  Rig r;
  const uint64_t t0 = r.now;
  r.send(0, 0x06, {10});
  r.clear();
  std::vector<uint64_t> times;
  for (uint64_t t = t0; t < t0 + 12000000; t += 1000) {
    if (t == t0 + 1050000 || t == t0 + 4000000 || t == t0 + 8000000) {
      r.now = t;
      r.send(0, 0x06, {10});     // same rate: a pure refresh
      r.sink.replies.pop_back();  // the ACK
    }
    const size_t before = r.sink.replies.size();
    r.server.tick(t);
    if (r.sink.replies.size() > before) times.push_back(t);
  }
  // Without the refreshes it would have stopped at 5 s; with them it runs on, one frame per 100 ms,
  // and the refreshes themselves neither add nor swallow a frame.
  CHECK_EQ(times.size(), 120u);
  for (size_t i = 1; i < times.size(); ++i) CHECK_EQ(times[i] - times[i - 1], 100000);
  CHECK(r.server.streaming());
}

EPDJ_TEST(stream_rate_change_restarts_the_phase_with_an_immediate_frame) {
  Rig r;
  r.send(0, 0x06, {10});
  r.server.tick(r.now);
  r.clear();
  r.now += 30000;
  r.send(0, 0x06, {20});
  r.clear();
  r.server.tick(r.now);
  CHECK_EQ(r.sink.replies.size(), 1u);
  r.now += 49000;
  r.server.tick(r.now);
  CHECK_EQ(r.sink.replies.size(), 1u);
  r.now += 1000;
  r.server.tick(r.now);
  CHECK_EQ(r.sink.replies.size(), 2u);
}

EPDJ_TEST(stream_late_ticks_never_burst) {
  Rig r;
  r.send(0, 0x06, {100});
  r.clear();
  r.server.tick(r.now);  // frame 1, due immediately
  r.now += 1000000;      // the engine was stuck for a whole second
  r.server.tick(r.now);
  r.server.tick(r.now);
  r.server.tick(r.now);
  CHECK_EQ(r.sink.replies.size(), 2u);  // one more frame, then it waits a period
  r.now += 10000;
  r.server.tick(r.now);
  CHECK_EQ(r.sink.replies.size(), 3u);
}

EPDJ_TEST(stream_bad_length) {
  Rig r;
  r.send(0, 0x06, {});
  r.expectNak(0x06, Err::BadLength);
  CHECK(!r.server.streaming());
  r.clear();
  r.send(0, 0x06, {1, 2});
  r.expectNak(0x06, Err::BadLength);
}

EPDJ_TEST(stream_tick_without_stream_is_silent) {
  Rig r;
  for (int i = 0; i < 100; ++i) {
    r.server.tick(r.now);
    r.now += 10000;
  }
  CHECK(r.sink.replies.empty());
  CHECK_EQ(r.actions.telemetryCalls, 0);
}

// ---- CAL_BEGIN / CAL_END ----

EPDJ_TEST(cal_without_a_host_is_not_supported) {
  Rig r(false);
  r.send(0, 0x07, {0});
  r.expectNak(0x07, Err::NotSupported);
  r.clear();
  r.send(0, 0x08, {0, 1});
  r.expectNak(0x08, Err::CalNotActive);
}

EPDJ_TEST(cal_begin_delegates_and_maps_errors) {
  Rig r;
  r.send(0, 0x07, {0});
  r.expectAck(0x07);
  CHECK_EQ(r.cal.begins, 1);
  CHECK(r.cal.lastBegin == proto::CalKind::Fader);
  r.clear();
  r.send(0, 0x07, {1});
  r.expectAck(0x07);
  CHECK(r.cal.lastBegin == proto::CalKind::Knob);
  r.clear();
  r.cal.beginResult = false;
  r.send(0, 0x07, {0});
  r.expectNak(0x07, Err::NotSupported);
  r.clear();
  r.cal.beginResult = true;
  r.send(0, 0x07, {2});  // not a CalKind
  r.expectNak(0x07, Err::NotSupported);
  CHECK_EQ(r.cal.begins, 3);  // the unknown kind never reached the host
  r.clear();
  r.send(0, 0x07, {});
  r.expectNak(0x07, Err::BadLength);
}

EPDJ_TEST(cal_end_apply_writes_the_range_into_the_live_config_and_reports_it) {
  Rig r;
  r.cal.min = 120;
  r.cal.max = 3970;
  r.send(0, 0x08, {0, 1});
  const Reply& rep = r.only();
  CHECK_EQ(rep.cmd, 0x48);
  proto::CalResult res;
  CHECK(proto::deserialize(rep.payload.data(), rep.payload.size(), &res));
  CHECK_EQ(res.kind, 0);
  CHECK_EQ(res.applied, 1);
  CHECK_EQ(res.rawMin, 120);
  CHECK_EQ(res.rawMax, 3970);
  CHECK_EQ(r.config.get(ConfigKey::FaderRawMin), 120);
  CHECK_EQ(r.config.get(ConfigKey::FaderRawMax), 3970);
  CHECK_EQ(r.config.get(ConfigKey::KnobRawMin), 0);  // knob untouched

  r.clear();
  r.cal.min = 30;
  r.cal.max = 4060;
  r.send(0, 0x08, {1, 1});
  CHECK_EQ(r.config.get(ConfigKey::KnobRawMin), 30);
  CHECK_EQ(r.config.get(ConfigKey::KnobRawMax), 4060);
  CHECK_EQ(r.config.get(ConfigKey::FaderRawMin), 120);
  CHECK(r.cal.lastEnd == proto::CalKind::Knob);
}

EPDJ_TEST(cal_end_without_apply_reports_the_measured_extremes_not_the_inset_range) {
  // PROTOCOL.md 3 (CalResult): the values written (with the inset) when applied = 1, otherwise the measured extremes.
  Rig r;
  const uint32_t rev = r.config.revision();
  r.send(0, 0x08, {0, 0});
  proto::CalResult res;
  CHECK(proto::deserialize(r.only().payload.data(), r.only().payload.size(), &res));
  CHECK_EQ(res.applied, 0);
  CHECK_EQ(res.rawMin, 100);  // measured, not the 120 that apply would have written
  CHECK_EQ(res.rawMax, 4000);
  CHECK_EQ(r.config.revision(), rev);
  CHECK_EQ(r.config.get(ConfigKey::FaderRawMin), 40);
  CHECK_EQ(r.config.get(ConfigKey::FaderRawMax), 4055);

  // The same sweep with apply = 1 reports exactly what it wrote.
  r.clear();
  r.send(0, 0x08, {0, 1});
  CHECK(proto::deserialize(r.only().payload.data(), r.only().payload.size(), &res));
  CHECK_EQ(res.applied, 1);
  CHECK_EQ(res.rawMin, 120);
  CHECK_EQ(res.rawMax, 3970);
  CHECK_EQ(r.config.get(ConfigKey::FaderRawMin), 120);
  CHECK_EQ(r.config.get(ConfigKey::FaderRawMax), 3970);
}

EPDJ_TEST(cal_end_error_mapping) {
  Rig r;
  r.cal.endResult = CalibrationHost::EndStatus::NotActive;
  r.send(0, 0x08, {0, 1});
  r.expectNak(0x08, Err::CalNotActive);
  r.clear();
  r.cal.endResult = CalibrationHost::EndStatus::Failed;
  r.send(0, 0x08, {0, 1});
  r.expectNak(0x08, Err::CalFailed);
  r.clear();
  r.cal.endResult = CalibrationHost::EndStatus::Ok;
  r.send(0, 0x08, {5, 1});  // unknown kind: nothing can be active for it
  r.expectNak(0x08, Err::CalNotActive);
  r.clear();
  r.send(0, 0x08, {0, 2});
  r.expectNak(0x08, Err::OutOfRange);
  r.clear();
  r.send(0, 0x08, {0});
  r.expectNak(0x08, Err::BadLength);
  CHECK_EQ(r.config.get(ConfigKey::FaderRawMin), 40);
}

EPDJ_TEST(cal_end_with_unusable_measurement_changes_nothing) {
  Rig r;
  r.cal.min = 10;
  r.cal.max = 5000;  // above the 12-bit range
  r.cal.measuredMin = 10;
  r.cal.measuredMax = 5000;
  const uint32_t rev = r.config.revision();
  r.send(0, 0x08, {0, 1});
  r.expectNak(0x08, Err::CalFailed);
  CHECK_EQ(r.config.revision(), rev);
  CHECK_EQ(r.config.get(ConfigKey::FaderRawMin), 40);
  // Without apply the same measurement is merely reported.
  r.clear();
  r.send(0, 0x08, {0, 0});
  CHECK_EQ(r.only().cmd, 0x48);
}

// ---- SET_DECK / REBOOT ----

EPDJ_TEST(set_deck_acks_under_the_old_deck_then_hands_over_to_the_platform) {
  Rig r;
  CHECK(r.send(0, 0x09, {1}) == Result::Replied);
  r.expectAck(0x09);
  CHECK_EQ(r.only().deck, 0);
  CHECK_EQ(r.config.get(ConfigKey::DeckId), 1);
  CHECK_EQ(r.actions.setDecks, 1);
  CHECK(r.actions.lastDeck == proto::Deck::B);
  CHECK(r.log.indexOf("reply") < r.log.indexOf("setDeck"));  // ACK is out before the restart is requested
}

EPDJ_TEST(set_deck_rejects_bad_values_and_bad_length) {
  Rig r;
  r.send(0, 0x09, {2});
  r.expectNak(0x09, Err::OutOfRange);
  r.clear();
  r.send(0, 0x09, {127});
  r.expectNak(0x09, Err::OutOfRange);
  r.clear();
  r.send(0, 0x09, {});
  r.expectNak(0x09, Err::BadLength);
  CHECK_EQ(r.actions.setDecks, 0);
  CHECK_EQ(r.config.get(ConfigKey::DeckId), 0);
}

EPDJ_TEST(reboot_acks_before_rebooting) {
  Rig r;
  r.send(0, 0x0A, {1});
  r.expectAck(0x0A);
  CHECK_EQ(r.actions.reboots, 1);
  CHECK(r.actions.lastReboot == proto::RebootMode::Bootloader);
  CHECK(r.log.indexOf("reply") < r.log.indexOf("reboot"));
  r.clear();
  r.send(0, 0x0A, {0});
  CHECK(r.actions.lastReboot == proto::RebootMode::Normal);
  CHECK_EQ(r.actions.reboots, 2);
}

EPDJ_TEST(reboot_rejects_unknown_mode_without_rebooting) {
  Rig r;
  r.send(0, 0x0A, {2});
  r.expectNak(0x0A, Err::OutOfRange);
  r.clear();
  r.send(0, 0x0A, {});
  r.expectNak(0x0A, Err::BadLength);
  CHECK_EQ(r.actions.reboots, 0);
}

// ---- LINK_STATUS / TEST_TONE / IDENTIFY ----

EPDJ_TEST(link_status_lists_peers) {
  Rig r;
  proto::Peer p;
  p.deck = 1; p.flags = 5; p.rssi = -60; p.rttUs = 1500; p.offsetUs = -250;
  r.actions.peers = {p};
  r.send(0, 0x0B, {});
  const Reply& rep = r.only();
  CHECK_EQ(rep.cmd, 0x4B);
  CHECK_EQ(rep.payload.size(), 1u + proto::Peer::kBytes);
  CHECK_EQ(rep.payload[0], 1);
  CHECK_BYTES(Bytes(rep.payload.begin() + 1, rep.payload.end()), epdj_vec::bytesPeer());
}

EPDJ_TEST(link_status_with_no_or_too_many_peers) {
  Rig r;
  r.send(0, 0x0B, {});
  CHECK_BYTES(r.only().payload, (Bytes{0}));
  r.clear();
  r.actions.peers.assign(20, proto::Peer());
  r.send(0, 0x0B, {});
  CHECK_EQ(r.only().payload[0], syx::kMaxPeers);
  r.clear();
  r.send(0, 0x0B, {1});
  r.expectNak(0x0B, Err::BadLength);
}

EPDJ_TEST(test_tone_passes_arguments_and_maps_results) {
  Rig r;
  r.send(0, 0x0C, {1, 0xE8, 0x03, 20});  // 1000 Hz, -20 dB
  r.expectAck(0x0C);
  CHECK(r.actions.lastToneOn);
  CHECK_EQ(r.actions.lastFreq, 1000);
  CHECK_EQ(r.actions.lastAtten, 20);
  r.clear();
  r.send(0, 0x0C, {0, 0, 0, 0});
  r.expectAck(0x0C);
  CHECK(!r.actions.lastToneOn);
  r.clear();
  r.actions.toneResult = false;  // no DAC
  r.send(0, 0x0C, {1, 0xE8, 0x03, 20});
  r.expectNak(0x0C, Err::NotSupported);
}

EPDJ_TEST(test_tone_validates_before_touching_the_dac) {
  Rig r;
  const Bytes bad[] = {
      {1, 19, 0, 0},       // 19 Hz
      {1, 0x21, 0x4E, 0},  // 20001 Hz
      {1, 0xE8, 0x03, 97}, // 97 dB
      {2, 0xE8, 0x03, 0},  // on must be 0 or 1
  };
  for (const auto& p : bad) {
    r.clear();
    r.send(0, 0x0C, p);
    r.expectNak(0x0C, Err::OutOfRange);
  }
  r.clear();
  r.send(0, 0x0C, {1, 20, 0, 0});     // 20 Hz: lowest accepted
  r.expectAck(0x0C);
  r.clear();
  r.send(0, 0x0C, {1, 0x20, 0x4E, 96});  // 20000 Hz, 96 dB: highest accepted
  r.expectAck(0x0C);
  r.clear();
  r.send(0, 0x0C, {1, 0xE8, 0x03});
  r.expectNak(0x0C, Err::BadLength);
  CHECK_EQ(r.actions.tones, 2);
}

EPDJ_TEST(identify) {
  Rig r;
  r.send(0, 0x0D, {5});
  r.expectAck(0x0D);
  CHECK_EQ(r.actions.identifies, 1);
  CHECK_EQ(r.actions.lastIdentify, 5);
  r.clear();
  r.send(0, 0x0D, {});
  r.expectNak(0x0D, Err::BadLength);
  CHECK_EQ(r.actions.identifies, 1);
}

// ---- addressing and robustness ----

EPDJ_TEST(deck_filtering_and_reply_deck_byte) {
  Rig a;  // deck A
  CHECK(a.send(1, 0x01, {}) == Result::NotForUs);
  CHECK(a.sink.replies.empty());
  CHECK(a.send(15, 0x01, {}) == Result::NotForUs);
  CHECK(a.send(0, 0x01, {}) == Result::Replied);
  CHECK_EQ(a.only().deck, 0);
  a.clear();
  CHECK(a.send(127, 0x01, {}) == Result::Replied);
  CHECK_EQ(a.only().deck, 0);

  Rig b;
  b.config.set(ConfigKey::DeckId, 1);
  CHECK(b.send(0, 0x01, {}) == Result::NotForUs);
  CHECK(b.send(1, 0x01, {}) == Result::Replied);
  CHECK_EQ(b.only().deck, 1);
  b.clear();
  CHECK(b.send(127, 0x02, {0xFF}) == Result::Replied);
  for (const auto& rep : b.sink.replies) CHECK_EQ(rep.deck, 1);
}

EPDJ_TEST(after_set_deck_the_unit_answers_to_its_new_deck) {
  Rig r;
  r.send(0, 0x09, {1});
  r.clear();
  CHECK(r.send(0, 0x01, {}) == Result::NotForUs);
  CHECK(r.send(1, 0x01, {}) == Result::Replied);
  CHECK_EQ(r.only().deck, 1);
}

EPDJ_TEST(invalid_frames_are_ignored_without_any_reply_or_side_effect) {
  Rig r;
  Bytes good(syx::kMaxFrameBytes);
  const Bytes payload = cat({3}, i32(2));
  good.resize(syx::encodeFrame(0, 0x03, payload.data(), payload.size(), good.data(), good.size()));
  CHECK(r.sendRaw(good) == Result::Replied);
  r.clear();
  r.config.set(ConfigKey::PlatterSource, 1);

  Bytes badSum = good; badSum[badSum.size() - 2] ^= 0x01;
  Bytes badVer = good; badVer[4] = 2;
  Bytes badMfr = good; badMfr[1] = 0x41;
  Bytes badId = good; badId[3] = 0x45;
  Bytes noEnd(good.begin(), good.end() - 1);
  Bytes noStart = good; noStart[0] = 0xF1;
  Bytes tiny = {0xF0, 0xF7};
  const Bytes* cases[] = {&badSum, &badVer, &badMfr, &badId, &noEnd, &noStart, &tiny};
  for (const Bytes* c : cases) {
    CHECK(r.sendRaw(*c) == Result::Ignored);
    CHECK(r.sink.replies.empty());
  }
  CHECK(r.server.handle(nullptr, 0, r.now) == Result::Ignored);
  CHECK_EQ(r.config.get(ConfigKey::PlatterSource), 1);
}

EPDJ_TEST(replies_acks_and_naks_from_other_units_are_never_answered) {
  Rig r;
  for (uint8_t cmd : {0x41, 0x42, 0x47, 0x48, 0x4B, 0x7E, 0x7F, 0x40, 0x60}) {
    CHECK(r.send(0, cmd, {1, 2}) == Result::Ignored);
    CHECK(r.send(127, cmd, {}) == Result::Ignored);
  }
  CHECK(r.sink.replies.empty());
}

EPDJ_TEST(two_servers_wired_back_to_back_do_not_ping_pong) {
  // Unit A's NAK is fed to unit B and vice versa; neither may answer.
  Rig a, b;
  b.config.set(ConfigKey::DeckId, 1);
  a.send(127, 0x20, {});  // unknown command, broadcast
  b.send(127, 0x20, {});
  CHECK_EQ(a.sink.replies.size(), 1u);
  CHECK_EQ(b.sink.replies.size(), 1u);
  // Same deck byte (A's NAK arrives at A's own deck filter) or a different one: never a reply.
  CHECK(b.sendRaw(a.sink.replies[0].raw) != Result::Replied);
  CHECK(a.sendRaw(b.sink.replies[0].raw) != Result::Replied);
  Rig c;  // a third unit that shares A's deck id (misconfiguration): it must still not answer a NAK
  CHECK(c.sendRaw(a.sink.replies[0].raw) == Result::Ignored);
  CHECK_EQ(a.sink.replies.size(), 1u);
  CHECK_EQ(b.sink.replies.size(), 1u);
}

EPDJ_TEST(every_command_checks_its_exact_payload_length_before_doing_anything) {
  const size_t want[14] = {0, 0, 1, 5, 0, 0, 1, 1, 2, 1, 1, 0, 4, 1};  // index = command code
  for (uint8_t cmd = 1; cmd <= 13; ++cmd) {
    for (int delta : {-1, +1}) {
      if (delta < 0 && want[cmd] == 0) continue;
      Rig r;
      r.send(0, cmd, Bytes(want[cmd] + static_cast<size_t>(delta), 0));
      r.expectNak(cmd, Err::BadLength);
      CHECK(r.config == Config());
      CHECK_EQ(r.actions.saves + r.actions.reboots + r.actions.setDecks + r.actions.tones +
                   r.actions.identifies + r.cal.begins + r.cal.ends, 0);
      CHECK(!r.server.streaming());
    }
  }
}

EPDJ_TEST(unknown_command_is_nakked) {
  Rig r;
  for (uint8_t cmd : {0x00, 0x0E, 0x20, 0x3F}) {
    r.clear();
    CHECK(r.send(0, cmd, {}) == Result::Replied);
    r.expectNak(cmd, Err::UnknownCmd);
  }
}

EPDJ_TEST(unpackable_payload_gets_bad_length_for_requests_only) {
  Rig r;
  // 9 septets: a header with no data after it. Valid checksum, impossible payload.
  Bytes m = {0xF0, 0x7D, 0x45, 0x44, 1, 0, 3};
  for (int i = 0; i < 9; ++i) m.push_back(1);
  m.push_back(syx::checksum(m.data() + 4, m.size() - 4));
  m.push_back(0xF7);
  CHECK(r.sendRaw(m) == Result::Replied);
  r.expectNak(0x03, Err::BadLength);
  r.clear();
  m[6] = 0x41;  // same garbage but as a "reply": ignored
  m[m.size() - 2] = syx::checksum(m.data() + 4, m.size() - 6);
  CHECK(r.sendRaw(m) == Result::Ignored);
  CHECK(r.sink.replies.empty());
}

EPDJ_TEST(a_full_sink_loses_frames_counts_them_and_never_breaks_the_server) {
  Rig r;
  r.sink.accept = false;
  CHECK(r.send(0, 0x02, {0xFF}) == Result::Replied);
  CHECK_EQ(r.log.count("reply"), 1u);          // the dump stops at the first refused frame
  CHECK_EQ(r.server.repliesDropped(), 1u);
  r.send(0, 0x01, {});
  r.send(0, 0x03, cat({3}, i32(1)));
  CHECK_EQ(r.server.repliesDropped(), 3u);     // every refused reply is counted, whatever it was
  CHECK_EQ(r.config.get(ConfigKey::PlatterSource), 1);  // the command itself still took effect
  r.sink.accept = true;
  r.clear();
  r.send(0, 0x01, {});
  CHECK_EQ(r.sink.replies.size(), 1u);
  CHECK_EQ(r.server.repliesDropped(), 3u);
}

// ---- ReplyQueue ----

EPDJ_TEST(reply_queue_is_a_fifo_of_whole_frames) {
  ReplyQueue q;
  uint8_t out[syx::kMaxFrameBytes];
  CHECK(q.empty());
  CHECK_EQ(q.pop(out, sizeof(out)), 0u);
  const uint8_t a[] = {1, 2, 3};
  const uint8_t b[] = {9, 8, 7, 6, 5};
  CHECK(q.send(a, 3));
  CHECK(q.send(b, 5));
  CHECK(!q.empty());
  CHECK_EQ(q.pop(out, sizeof(out)), 3u);
  CHECK_EQ(out[0], 1);
  CHECK_EQ(q.pop(out, 2), 0u);  // too small a buffer: frame stays queued
  CHECK_EQ(q.pop(out, sizeof(out)), 5u);
  CHECK_EQ(out[4], 5);
  CHECK(q.empty());
}

EPDJ_TEST(reply_queue_wraps_and_drops_when_full) {
  ReplyQueue q;
  uint8_t frame[100];
  uint8_t out[syx::kMaxFrameBytes];
  for (int round = 0; round < 500; ++round) {
    const size_t len = 20 + static_cast<size_t>(round % 70);
    for (size_t i = 0; i < len; ++i) frame[i] = static_cast<uint8_t>(round + static_cast<int>(i));
    CHECK(q.send(frame, len));
    CHECK_EQ(q.pop(out, sizeof(out)), len);
    for (size_t i = 0; i < len; ++i) CHECK_EQ(out[i], static_cast<uint8_t>(round + static_cast<int>(i)));
  }
  size_t accepted = 0;
  while (q.send(frame, 100)) ++accepted;
  CHECK_EQ(accepted, ReplyQueue::kCapacityBytes / 101);
  CHECK_EQ(q.dropped(), 1u);
  for (size_t i = 0; i < accepted; ++i) CHECK_EQ(q.pop(out, sizeof(out)), 100u);
  CHECK(q.empty());
  CHECK(!q.send(frame, 0));
  uint8_t huge[300] = {};
  CHECK(!q.send(huge, 300));
  q.send(frame, 10);
  q.clear();
  CHECK(q.empty());
}

EPDJ_TEST(dump_all_through_a_queue_over_usb_midi_reassembles) {
  // The full device path: server -> ReplyQueue -> USB-MIDI packets -> host parser -> frame decode.
  Config config;
  ReplyQueue q;
  Actions actions;
  ConfigServer server(config, actions, q);
  const uint8_t all = 0xFF;
  Bytes req(syx::kMaxFrameBytes);
  req.resize(syx::encodeFrame(0, 0x02, &all, 1, req.data(), req.size()));
  CHECK(server.handle(req.data(), req.size(), 0) == Result::Replied);

  uint8_t sysexBuf[syx::kMaxFrameBytes];
  midi::Parser parser(sysexBuf, sizeof(sysexBuf));
  size_t keys = 0, acks = 0, bytes = 0;
  uint8_t frame[syx::kMaxFrameBytes];
  while (size_t n = q.pop(frame, sizeof(frame))) {
    bytes += n;
    midi::UsbPacket pk[(syx::kMaxFrameBytes + 2) / 3];
    const size_t np = midi::encodeSysex(0, frame, n, pk, sizeof(pk) / sizeof(pk[0]));
    CHECK(np != 0);
    for (size_t i = 0; i < np; ++i) {
      uint8_t wire[4];
      pk[i].toBytes(wire);
      parser.feed(midi::UsbPacket::fromBytes(wire), [&](const midi::Parser::Event& ev) {
        CHECK(ev.kind == midi::Parser::Event::Kind::SysEx);
        syx::Frame f;
        CHECK(syx::decodeFrame(ev.sysex, ev.sysexLen, 0, &f) == syx::DecodeStatus::Ok);
        if (f.cmd == 0x42) {
          CHECK_EQ(acks, 0u);  // the end marker comes last
          CHECK_EQ(f.payload[0], keys);
          ++keys;
        } else {
          CHECK_EQ(f.cmd, 0x7E);
          CHECK_EQ(f.payloadLen, 1u);
          CHECK_EQ(f.payload[0], 0x02);
          ++acks;
        }
      });
    }
  }
  CHECK_EQ(keys, proto::kConfigCount);
  CHECK_EQ(acks, 1u);
  CHECK_EQ(q.dropped(), 0u);
  CHECK_EQ(server.repliesDropped(), 0u);
  // What a full dump costs on the wire: every key plus the end marker. ReplyQueue is statically sized for it.
  CHECK_EQ(bytes, kConfigDumpBytes);
  CHECK_EQ(kConfigDumpBytes, proto::kConfigCount * 15 + 11);
  CHECK(ReplyQueue::kDumpRecordBytes <= ReplyQueue::kCapacityBytes);
}

EPDJ_TEST(a_dump_into_a_full_queue_is_counted_and_has_no_end_marker) {
  Config config;
  ReplyQueue q;
  Actions actions;
  ConfigServer server(config, actions, q);
  const uint8_t all = 0xFF;
  Bytes req(syx::kMaxFrameBytes);
  req.resize(syx::encodeFrame(0, 0x02, &all, 1, req.data(), req.size()));

  // Leave room for only some of the keys: five 200-byte records fill the queue, one is taken out again.
  uint8_t filler[200] = {};
  for (int i = 0; i < 5; ++i) CHECK(q.send(filler, sizeof(filler)));
  uint8_t out[256];
  CHECK_EQ(q.pop(out, sizeof(out)), sizeof(filler));
  CHECK_EQ(q.dropped(), 0u);

  CHECK(server.handle(req.data(), req.size(), 0) == Result::Replied);
  CHECK_EQ(server.repliesDropped(), 1u);  // the refusal is counted, not silent
  CHECK_EQ(q.dropped(), 1u);
  size_t configs = 0, acks = 0;
  syx::Frame f;
  while (size_t n = q.pop(out, sizeof(out))) {
    if (syx::decodeFrame(out, n, 0, &f) != syx::DecodeStatus::Ok) continue;  // a filler record
    configs += f.cmd == 0x42 ? 1u : 0u;
    acks += f.cmd == 0x7E ? 1u : 0u;
  }
  CHECK(configs >= 10 && configs < proto::kConfigCount);  // some keys fit; the dump stopped at the first refusal
  CHECK_EQ(acks, 0u);                                     // so an incomplete dump never ends with the marker

  // Drained: the next dump is complete again.
  q.clear();
  CHECK(server.handle(req.data(), req.size(), 0) == Result::Replied);
  CHECK_EQ(server.repliesDropped(), 1u);
  configs = acks = 0;
  while (size_t n = q.pop(out, sizeof(out))) {
    CHECK(syx::decodeFrame(out, n, 0, &f) == syx::DecodeStatus::Ok);
    configs += f.cmd == 0x42 ? 1u : 0u;
    acks += f.cmd == 0x7E ? 1u : 0u;
  }
  CHECK_EQ(configs, proto::kConfigCount);
  CHECK_EQ(acks, 1u);
}
