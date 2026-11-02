// Conformance of the GENERATED protocol header against the independent reference vectors.
#include "epdj/protocol.h"
#include "test_harness.h"
#include "vectors.generated.h"

using namespace epdj::proto;

static std::vector<uint8_t> ser(const Telemetry& t) {
  std::vector<uint8_t> b(Telemetry::kBytes);
  serialize(t, b.data());
  return b;
}

EPDJ_TEST(struct_sizes_match_contract) {
  CHECK_EQ(Info::kBytes, 18);
  CHECK_EQ(Telemetry::kBytes, 36);
  CHECK_EQ(CalResult::kBytes, 6);
  CHECK_EQ(Peer::kBytes, 10);
}

EPDJ_TEST(telemetry_matches_reference_bytes) {
  Telemetry t;
  t.seq = 200; t.buttons = 3; t.flags = 0x0B; t.transport = 1;
  t.faderRaw = 2048; t.knobRaw = 4095; t.faderOut = 8191; t.knobOut = 16383;
  t.angle = 1234; t.bpmX10 = 1285; t.jogTotal = -70000;
  const uint16_t raw[8] = {100, 200, 300, 400, 500, 600, 700, 65535};
  for (int i = 0; i < 8; ++i) t.touchRaw[i] = raw[i];
  CHECK_BYTES(ser(t), epdj_vec::bytesTelemetry());
  Telemetry u;
  CHECK(deserialize(epdj_vec::bytesTelemetry().data(), epdj_vec::bytesTelemetry().size(), &u));
  CHECK_EQ(u.jogTotal, -70000);
  CHECK_EQ(u.touchRaw[7], 65535);
}

EPDJ_TEST(peer_negative_fields_roundtrip) {
  Peer p;
  CHECK(deserialize(epdj_vec::bytesPeer().data(), epdj_vec::bytesPeer().size(), &p));
  CHECK_EQ(p.rssi, -60);
  CHECK_EQ(p.offsetUs, -250);
  CHECK_EQ(p.rttUs, 1500);
}

EPDJ_TEST(config_table_is_dense_and_in_range) {
  for (size_t i = 0; i < kConfigCount; ++i) {
    CHECK_EQ(static_cast<size_t>(kConfigTable[i].key), i);
    CHECK(kConfigTable[i].minValue <= kConfigTable[i].defaultValue);
    CHECK(kConfigTable[i].defaultValue <= kConfigTable[i].maxValue);
  }
}

EPDJ_TEST(control_numbers) {
  CHECK_EQ(cc::kJog, 22);
  CHECK_EQ(cc::kCrossfaderMsb, 23);
  CHECK_EQ(cc::kCrossfaderLsb, 55);
  CHECK_EQ(note::kCue, 36);
  CHECK_EQ(cable::kLinkedDeck, 2);
}
