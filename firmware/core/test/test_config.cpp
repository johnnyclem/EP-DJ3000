// Config store: table-driven validation, effective channel, blob serialisation and corruption handling.
#include <vector>

#include "epdj/config.h"
#include "epdj/protocol.h"
#include "test_harness.h"

using namespace epdj;
using proto::ConfigKey;
using Bytes = std::vector<uint8_t>;

namespace {

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  uint32_t below(uint32_t n) { return next() % n; }
};

ConfigKey key(size_t i) { return static_cast<ConfigKey>(i); }

Bytes blobOf(const Config& c) {
  Bytes b(Config::kBlobMaxBytes);
  b.resize(c.toBlob(b.data(), b.size()));
  return b;
}

void putCrc(Bytes* b) {
  const uint32_t c = crc32(b->data(), b->size() - 4);
  for (int i = 0; i < 4; ++i) (*b)[b->size() - 4 + static_cast<size_t>(i)] = static_cast<uint8_t>(c >> (8 * i));
}

// Hand-built blob so tests can fake other versions / key counts / out-of-range values with a valid CRC.
Bytes makeBlob(uint8_t version, const std::vector<int32_t>& values) {
  Bytes b = {'E', 'P', 'C', 'F', version, static_cast<uint8_t>(values.size())};
  for (int32_t v : values) {
    const uint32_t u = static_cast<uint32_t>(v);
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(u >> (8 * i)));
  }
  b.resize(b.size() + 4);
  putCrc(&b);
  return b;
}

std::vector<int32_t> defaultValues() {
  std::vector<int32_t> v;
  for (size_t i = 0; i < proto::kConfigCount; ++i) v.push_back(proto::kConfigTable[i].defaultValue);
  return v;
}

bool usable(LoadStatus s) { return s == LoadStatus::Ok || s == LoadStatus::Repaired; }

}  // namespace

EPDJ_TEST(crc32_known_answers) {
  const char* s = "123456789";
  CHECK_EQ(crc32(reinterpret_cast<const uint8_t*>(s), 9), 0xCBF43926u);
  CHECK_EQ(crc32(nullptr, 0), 0u);
  const uint8_t zero4[4] = {0, 0, 0, 0};
  CHECK_EQ(crc32(zero4, 4), 0x2144DF1Cu);
}

EPDJ_TEST(defaults_come_from_the_contract_table) {
  Config c;
  for (size_t i = 0; i < Config::kCount; ++i) CHECK_EQ(c.get(key(i)), proto::kConfigTable[i].defaultValue);
  // Spot checks against PROTOCOL.md 3.1 (guards against the table and the spec drifting apart).
  CHECK_EQ(c.get(ConfigKey::MidiChannel), 16);
  CHECK_EQ(c.get(ConfigKey::PlatterSource), static_cast<int>(proto::PlatterSource::As5600));  // contract v1.3: the free-spinning platter
  CHECK_EQ(proto::kConfigTable[static_cast<size_t>(ConfigKey::PlatterSource)].defaultValue, 0);
  CHECK_EQ(c.get(ConfigKey::FaderRawMax), 4055);
  CHECK_EQ(c.get(ConfigKey::BpmX10), 1200);
  CHECK_EQ(c.get(ConfigKey::ClockOutMask), 3);
  CHECK_EQ(c.get(ConfigKey::MtcFps), 3);
  CHECK_EQ(c.get(ConfigKey::ButtonDebounceMs), 4);
  CHECK_EQ(c.get(ConfigKey::FaderRole), 2);  // PITCH (contract v1.2): audible in every DJ app, and two units never fight over it
  CHECK_EQ(c.get(ConfigKey::LinkGroup), 0);
  CHECK_EQ(proto::kConfigCount, 33u);
  CHECK_EQ(c.revision(), 0u);
}

EPDJ_TEST(validate_enforces_every_range_at_the_edges) {
  for (size_t i = 0; i < Config::kCount; ++i) {
    const auto& d = proto::kConfigTable[i];
    const uint8_t id = static_cast<uint8_t>(i);
    CHECK(Config::validate(id, d.minValue) == ConfigStatus::Ok);
    CHECK(Config::validate(id, d.maxValue) == ConfigStatus::Ok);
    CHECK(Config::validate(id, d.defaultValue) == ConfigStatus::Ok);
    CHECK(Config::validate(id, d.minValue - 1) == ConfigStatus::OutOfRange);
    CHECK(Config::validate(id, d.maxValue + 1) == ConfigStatus::OutOfRange);
    CHECK(Config::validate(id, INT32_MIN) == ConfigStatus::OutOfRange);
    CHECK(Config::validate(id, INT32_MAX) == ConfigStatus::OutOfRange);
  }
  CHECK(Config::validate(32, 255) == ConfigStatus::Ok);  // linkGroup, the last key
  CHECK(Config::validate(32, 256) == ConfigStatus::OutOfRange);
  CHECK(Config::validate(33, 0) == ConfigStatus::BadKey);
  CHECK(Config::validate(255, 0) == ConfigStatus::BadKey);
  // A few literal ranges from the spec.
  CHECK(Config::validate(1, 16) == ConfigStatus::Ok);
  CHECK(Config::validate(1, 17) == ConfigStatus::OutOfRange);
  CHECK(Config::validate(23, 200) == ConfigStatus::Ok);
  CHECK(Config::validate(23, 199) == ConfigStatus::OutOfRange);
  CHECK(Config::validate(23, 3000) == ConfigStatus::Ok);
  CHECK(Config::validate(23, 3001) == ConfigStatus::OutOfRange);
  CHECK(Config::validate(31, 0) == ConfigStatus::OutOfRange);
  CHECK(Config::validate(5, 2) == ConfigStatus::OutOfRange);
}

EPDJ_TEST(set_validates_and_leaves_value_alone_on_failure) {
  Config c;
  CHECK(c.set(ConfigKey::PlatterSource, 2) == ConfigStatus::Ok);
  CHECK_EQ(c.get(ConfigKey::PlatterSource), 2);
  CHECK(c.set(ConfigKey::PlatterSource, 3) == ConfigStatus::OutOfRange);
  CHECK_EQ(c.get(ConfigKey::PlatterSource), 2);
  CHECK(c.set(ConfigKey::FaderRawMin, -1) == ConfigStatus::OutOfRange);
  CHECK(c.set(ConfigKey::LinkGroup, 255) == ConfigStatus::Ok);
  CHECK_EQ(c.get(ConfigKey::LinkGroup), 255);
  CHECK(c.set(static_cast<ConfigKey>(33), 0) == ConfigStatus::BadKey);
  CHECK(c.set(static_cast<ConfigKey>(200), 0) == ConfigStatus::BadKey);
  CHECK_EQ(c.get(static_cast<ConfigKey>(200)), 0);  // unknown key reads as 0, never out of bounds
}

EPDJ_TEST(revision_counts_real_changes_only) {
  Config c;
  CHECK_EQ(c.revision(), 0u);
  c.set(ConfigKey::JogInvert, 0);  // already 0
  CHECK_EQ(c.revision(), 0u);
  c.set(ConfigKey::JogInvert, 1);
  CHECK_EQ(c.revision(), 1u);
  c.set(ConfigKey::JogInvert, 5);  // rejected
  CHECK_EQ(c.revision(), 1u);
  c.setDefaults();
  CHECK_EQ(c.revision(), 2u);
  c.setDefaults();  // nothing to reset
  CHECK_EQ(c.revision(), 2u);
}

EPDJ_TEST(effective_channel_follows_deck_in_auto) {
  Config c;
  CHECK_EQ(c.effectiveChannel(), 0);  // deck A, auto
  c.set(ConfigKey::DeckId, 1);
  CHECK_EQ(c.effectiveChannel(), 1);  // deck B, auto
  CHECK(c.deck() == proto::Deck::B);
  for (int ch = 0; ch <= 15; ++ch) {
    c.set(ConfigKey::MidiChannel, ch);
    CHECK_EQ(c.effectiveChannel(), ch);  // explicit channel wins over the deck
  }
  c.set(ConfigKey::MidiChannel, 16);
  CHECK_EQ(c.effectiveChannel(), 1);
}

EPDJ_TEST(blob_roundtrips_every_value) {
  Rng rng(42);
  for (int round = 0; round < 50; ++round) {
    Config a;
    for (size_t i = 0; i < Config::kCount; ++i) {
      const auto& d = proto::kConfigTable[i];
      const uint32_t span = static_cast<uint32_t>(d.maxValue - d.minValue) + 1;
      a.set(key(i), d.minValue + static_cast<int32_t>(rng.below(span)));
    }
    const Bytes b = blobOf(a);
    CHECK_EQ(b.size(), Config::kBlobMaxBytes);
    Config c;
    CHECK(c.fromBlob(b.data(), b.size()) == LoadStatus::Ok);
    CHECK(a == c);
  }
}

EPDJ_TEST(blob_layout_is_stable) {
  const Bytes b = blobOf(Config());
  CHECK_EQ(Config::kBlobMaxBytes, 6 + 4 * 33 + 4);
  CHECK_EQ(b.size(), Config::kBlobMaxBytes);
  CHECK_EQ(b[0], 'E'); CHECK_EQ(b[1], 'P'); CHECK_EQ(b[2], 'C'); CHECK_EQ(b[3], 'F');
  CHECK_EQ(b[4], 1);
  CHECK_EQ(b[5], 33);
  CHECK_EQ(b[6 + 4 * 11], 2);       // faderRole = PITCH by default
  CHECK_EQ(b[6 + 4 * 1], 16);       // midiChannel = 16, little-endian
  CHECK_EQ(b[6 + 4 * 13], 4055 & 0xFF);
  CHECK_EQ(b[6 + 4 * 13 + 1], 4055 >> 8);
  uint8_t small[10];
  CHECK_EQ(Config().toBlob(small, sizeof(small)), 0u);
}

EPDJ_TEST(every_single_bit_flip_is_detected_and_falls_back_to_defaults) {
  Config a;
  a.set(ConfigKey::FaderRawMin, 123);
  a.set(ConfigKey::BpmX10, 1285);
  const Bytes good = blobOf(a);
  for (size_t byte = 0; byte < good.size(); ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      Bytes b = good;
      b[byte] = static_cast<uint8_t>(b[byte] ^ (1u << bit));
      Config c = a;
      const LoadStatus s = c.fromBlob(b.data(), b.size());
      CHECK(!usable(s));
      CHECK(c == Config());
    }
  }
}

EPDJ_TEST(truncation_and_extension_are_detected) {
  const Bytes good = blobOf(Config());
  Config c;
  c.set(ConfigKey::JogInvert, 1);
  CHECK(c.fromBlob(good.data(), 0) == LoadStatus::Empty);
  CHECK(c == Config());
  for (size_t n = 1; n < good.size(); ++n) {
    c.set(ConfigKey::JogInvert, 1);
    const LoadStatus s = c.fromBlob(good.data(), n);
    CHECK(!usable(s));
    CHECK(s != LoadStatus::Empty);
    CHECK(c == Config());
  }
  Bytes longer = good;
  longer.push_back(0);
  CHECK(c.fromBlob(longer.data(), longer.size()) == LoadStatus::BadLength);
  Bytes badMagic = good;
  badMagic[0] = 'X';
  CHECK(c.fromBlob(badMagic.data(), badMagic.size()) == LoadStatus::BadMagic);
}

EPDJ_TEST(version_mismatch_is_reported_even_when_the_crc_is_valid) {
  Config c;
  c.set(ConfigKey::JogInvert, 1);
  const Bytes v2 = makeBlob(2, defaultValues());
  CHECK(c.fromBlob(v2.data(), v2.size()) == LoadStatus::BadVersion);
  CHECK(c == Config());

  // A blob from newer firmware with more keys than we know.
  auto more = defaultValues();
  more.push_back(7);
  const Bytes newer = makeBlob(1, more);
  CHECK(c.fromBlob(newer.data(), newer.size()) == LoadStatus::BadVersion);
}

EPDJ_TEST(older_blob_with_fewer_keys_loads_and_defaults_the_rest) {
  auto vals = defaultValues();
  vals.resize(30);
  vals[static_cast<size_t>(ConfigKey::BpmX10)] = 1285;
  const Bytes b = makeBlob(1, vals);
  Config c;
  c.set(ConfigKey::LedBrightness, 1);
  c.set(ConfigKey::ButtonDebounceMs, 9);
  CHECK(c.fromBlob(b.data(), b.size()) == LoadStatus::Ok);
  CHECK_EQ(c.get(ConfigKey::BpmX10), 1285);
  CHECK_EQ(c.get(ConfigKey::LedBrightness), 64);  // keys 30 and 31 are not in the blob: defaults
  CHECK_EQ(c.get(ConfigKey::ButtonDebounceMs), 4);
}

EPDJ_TEST(a_blob_saved_before_linkgroup_existed_loads_with_group_zero_and_keeps_its_fader_role) {
  // Contract v1.0 wrote 32 keys, with faderRole defaulting to CROSSFADER (0). A unit that saved such a
  // config keeps what it stored; only never-saved units get the new default.
  auto vals = defaultValues();
  vals.resize(32);
  vals[static_cast<size_t>(ConfigKey::FaderRole)] = 0;
  vals[static_cast<size_t>(ConfigKey::DeckId)] = 1;
  const Bytes b = makeBlob(1, vals);
  Config c;
  c.set(ConfigKey::LinkGroup, 9);
  CHECK(c.fromBlob(b.data(), b.size()) == LoadStatus::Ok);
  CHECK_EQ(c.get(ConfigKey::FaderRole), 0);
  CHECK_EQ(c.get(ConfigKey::DeckId), 1);
  CHECK_EQ(c.get(ConfigKey::LinkGroup), 0);
}

EPDJ_TEST(out_of_range_values_are_repaired_individually) {
  auto vals = defaultValues();
  vals[static_cast<size_t>(ConfigKey::MidiChannel)] = 99;
  vals[static_cast<size_t>(ConfigKey::BpmX10)] = 1285;
  vals[static_cast<size_t>(ConfigKey::TouchSectors)] = -4;
  const Bytes b = makeBlob(1, vals);
  Config c;
  CHECK(c.fromBlob(b.data(), b.size()) == LoadStatus::Repaired);
  CHECK_EQ(c.get(ConfigKey::MidiChannel), 16);
  CHECK_EQ(c.get(ConfigKey::TouchSectors), 8);
  CHECK_EQ(c.get(ConfigKey::BpmX10), 1285);  // the good value survives
}

EPDJ_TEST(from_blob_bumps_revision_only_when_something_changes) {
  Config a;
  a.set(ConfigKey::BpmX10, 900);
  const Bytes b = blobOf(a);
  Config c;
  c.fromBlob(b.data(), b.size());
  const uint32_t r = c.revision();
  CHECK(r > 0);
  c.fromBlob(b.data(), b.size());
  CHECK_EQ(c.revision(), r);
}

EPDJ_TEST(store_roundtrip_and_failure_modes) {
  MemoryConfigStore store;
  Config c;
  CHECK(loadConfig(store, &c) == LoadStatus::Empty);
  CHECK(c == Config());

  Config a;
  a.set(ConfigKey::FaderRawMin, 77);
  a.set(ConfigKey::DeckId, 1);
  CHECK(saveConfig(store, a));
  CHECK_EQ(store.saveCount, 1u);
  CHECK_EQ(store.size, Config::kBlobMaxBytes);
  CHECK(loadConfig(store, &c) == LoadStatus::Ok);
  CHECK(c == a);

  // Flash corruption.
  store.data[40] = static_cast<uint8_t>(store.data[40] ^ 0x10);
  c.set(ConfigKey::JogInvert, 1);
  CHECK(loadConfig(store, &c) == LoadStatus::BadCrc);
  CHECK(c == Config());

  // A stored blob bigger than anything we write.
  store.size = MemoryConfigStore::kCapacity;
  CHECK(loadConfig(store, &c) == LoadStatus::BadLength);
  CHECK(c == Config());

  // Failed save is reported and leaves the previous blob in place.
  store.size = 0;
  CHECK(saveConfig(store, a));
  store.failSaves = true;
  Config other;
  other.set(ConfigKey::FaderRawMin, 5);
  CHECK(!saveConfig(store, other));
  store.failSaves = false;
  CHECK(loadConfig(store, &c) == LoadStatus::Ok);
  CHECK_EQ(c.get(ConfigKey::FaderRawMin), 77);
}
