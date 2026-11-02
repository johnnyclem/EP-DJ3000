#include "epdj/config.h"

namespace epdj {

uint32_t crc32(const uint8_t* data, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

namespace {
constexpr uint8_t kMagic[4] = {'E', 'P', 'C', 'F'};

void putU32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
uint32_t getU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

void Config::setDefaults() {
  bool changed = false;
  for (size_t i = 0; i < kCount; ++i) {
    changed = changed || v_[i] != proto::kConfigTable[i].defaultValue;
    v_[i] = proto::kConfigTable[i].defaultValue;
  }
  if (changed) ++revision_;
}

int32_t Config::get(proto::ConfigKey key) const {
  const size_t i = static_cast<size_t>(key);
  return i < kCount ? v_[i] : 0;
}

ConfigStatus Config::validate(uint8_t keyId, int32_t value) {
  if (keyId >= kCount) return ConfigStatus::BadKey;
  const proto::ConfigDesc& d = proto::kConfigTable[keyId];
  return value < d.minValue || value > d.maxValue ? ConfigStatus::OutOfRange : ConfigStatus::Ok;
}

ConfigStatus Config::set(proto::ConfigKey key, int32_t value) {
  const uint8_t id = static_cast<uint8_t>(key);
  const ConfigStatus s = validate(id, value);
  if (s != ConfigStatus::Ok) return s;
  if (v_[id] != value) {
    v_[id] = value;
    ++revision_;
  }
  return s;
}

uint8_t Config::effectiveChannel() const {
  const int32_t ch = get(proto::ConfigKey::MidiChannel);
  if (ch >= 0 && ch <= 15) return static_cast<uint8_t>(ch);
  return get(proto::ConfigKey::DeckId) == static_cast<int32_t>(proto::Deck::B) ? proto::kDefaultChannelDeckB
                                                                               : proto::kDefaultChannelDeckA;
}

size_t Config::toBlob(uint8_t* out, size_t cap) const {
  if (cap < kBlobMaxBytes) return 0;
  for (size_t i = 0; i < 4; ++i) out[i] = kMagic[i];
  out[4] = kBlobVersion;
  out[5] = static_cast<uint8_t>(kCount);
  for (size_t i = 0; i < kCount; ++i) putU32(out + kBlobHeaderBytes + 4 * i, static_cast<uint32_t>(v_[i]));
  const size_t body = kBlobHeaderBytes + 4 * kCount;
  putU32(out + body, crc32(out, body));
  return body + 4;
}

LoadStatus Config::fromBlob(const uint8_t* blob, size_t len) {
  Config fresh;  // defaults; committed to *this only on success
  LoadStatus result = LoadStatus::Ok;
  do {
    if (len == 0) { result = LoadStatus::Empty; break; }
    if (len < kBlobHeaderBytes + 4) { result = LoadStatus::BadLength; break; }
    for (size_t i = 0; i < 4; ++i) {
      if (blob[i] != kMagic[i]) { result = LoadStatus::BadMagic; break; }
    }
    if (result != LoadStatus::Ok) break;
    const size_t count = blob[5];
    const size_t body = kBlobHeaderBytes + 4 * count;
    if (len != body + 4) { result = LoadStatus::BadLength; break; }
    if (crc32(blob, body) != getU32(blob + body)) { result = LoadStatus::BadCrc; break; }
    if (blob[4] != kBlobVersion || count > kCount) { result = LoadStatus::BadVersion; break; }
    for (size_t i = 0; i < count; ++i) {
      const int32_t v = static_cast<int32_t>(getU32(blob + kBlobHeaderBytes + 4 * i));
      if (validate(static_cast<uint8_t>(i), v) == ConfigStatus::Ok) {
        fresh.v_[i] = v;
      } else {
        result = LoadStatus::Repaired;
      }
    }
  } while (false);

  const bool usable = result == LoadStatus::Ok || result == LoadStatus::Repaired;
  if (!usable) fresh.setDefaults();
  bool changed = false;
  for (size_t i = 0; i < kCount; ++i) {
    changed = changed || v_[i] != fresh.v_[i];
    v_[i] = fresh.v_[i];
  }
  if (changed) ++revision_;
  return result;
}

bool Config::operator==(const Config& o) const {
  for (size_t i = 0; i < kCount; ++i) {
    if (v_[i] != o.v_[i]) return false;
  }
  return true;
}

LoadStatus loadConfig(ConfigStore& store, Config* cfg) {
  uint8_t buf[Config::kBlobMaxBytes];
  const size_t n = store.load(buf, sizeof(buf));
  if (n > sizeof(buf)) {  // larger than any blob we write, so it cannot be ours
    cfg->setDefaults();
    return LoadStatus::BadLength;
  }
  return cfg->fromBlob(buf, n);
}

bool saveConfig(ConfigStore& store, const Config& cfg) {
  uint8_t buf[Config::kBlobMaxBytes];
  const size_t n = cfg.toBlob(buf, sizeof(buf));
  return n != 0 && store.save(buf, n);
}

size_t MemoryConfigStore::load(uint8_t* buf, size_t cap) {
  const size_t n = size < cap ? size : cap;
  for (size_t i = 0; i < n; ++i) buf[i] = data[i];
  return size;
}

bool MemoryConfigStore::save(const uint8_t* blob, size_t len) {
  if (failSaves || len > kCapacity) return false;
  for (size_t i = 0; i < len; ++i) data[i] = blob[i];
  size = len;
  ++saveCount;
  return true;
}

}  // namespace epdj
