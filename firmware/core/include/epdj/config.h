// Live device configuration: one int32 per proto::ConfigKey, validated against the contract table,
// plus a versioned, CRC-protected blob form for non-volatile storage.
#pragma once
#include <cstddef>
#include <cstdint>

#include "epdj/protocol.h"

namespace epdj {

// IEEE 802.3 CRC-32 (reflected, init/xorout 0xFFFFFFFF); crc32("123456789") == 0xCBF43926.
uint32_t crc32(const uint8_t* data, size_t n);

enum class ConfigStatus : uint8_t { Ok, BadKey, OutOfRange };

// Result of restoring a Config from a stored blob. Everything except Ok and Repaired leaves the
// Config at its defaults.
enum class LoadStatus : uint8_t {
  Ok,
  Repaired,    // valid blob, but some values were out of range and were reset to their defaults
  Empty,       // nothing stored (first boot)
  BadLength,   // truncated, or not the size its header promises
  BadMagic,
  BadVersion,  // written by an incompatible layout version, or by firmware with more keys than this one
  BadCrc,
};

class Config {
 public:
  static constexpr size_t kCount = proto::kConfigCount;

  // Blob layout (little-endian): "EPCF" | version u8 | count u8 | count x i32 | crc32 of all before.
  // The header and the trailing CRC are common to every version, so a future layout is recognised.
  // A blob with fewer keys than this firmware knows (an older firmware appended nothing yet) loads
  // fine: the missing keys take their defaults. The contract only ever appends keys.
  static constexpr uint8_t kBlobVersion = 1;
  static constexpr size_t kBlobHeaderBytes = 6;
  static constexpr size_t kBlobMaxBytes = kBlobHeaderBytes + 4 * kCount + 4;

  Config() {
    for (size_t i = 0; i < kCount; ++i) v_[i] = proto::kConfigTable[i].defaultValue;
  }

  void setDefaults();

  int32_t get(proto::ConfigKey key) const;
  // Rejects unknown keys and values outside the contract range; the stored value is then unchanged.
  ConfigStatus set(proto::ConfigKey key, int32_t value);
  static ConfigStatus validate(uint8_t keyId, int32_t value);

  // Zero-based channel to use: midiChannel 0..15, or 16 = auto (Deck A -> 0, Deck B -> 1).
  uint8_t effectiveChannel() const;
  proto::Deck deck() const { return static_cast<proto::Deck>(get(proto::ConfigKey::DeckId)); }

  // Incremented whenever a value actually changes (set, setDefaults, fromBlob), so a consumer can
  // cheaply tell that it must re-read the config.
  uint32_t revision() const { return revision_; }

  // Writes the blob and returns its length, or 0 if cap < kBlobMaxBytes.
  size_t toBlob(uint8_t* out, size_t cap) const;
  LoadStatus fromBlob(const uint8_t* blob, size_t len);

  bool operator==(const Config& o) const;
  bool operator!=(const Config& o) const { return !(*this == o); }

 private:
  int32_t v_[kCount] = {};
  uint32_t revision_ = 0;
};

// Persistent storage for the blob (NVS on the device, memory in tests).
class ConfigStore {
 public:
  // Copies up to cap bytes of the stored blob into buf and returns the stored size (so a result
  // larger than cap means the blob did not fit), or 0 if nothing is stored.
  virtual size_t load(uint8_t* buf, size_t cap) = 0;
  virtual bool save(const uint8_t* blob, size_t len) = 0;

 protected:
  ~ConfigStore() = default;  // never deleted through the interface: keeps operator delete out of the firmware
};

// Convenience around Config::toBlob/fromBlob. loadConfig leaves *cfg at defaults on any failure.
LoadStatus loadConfig(ConfigStore& store, Config* cfg);
bool saveConfig(ConfigStore& store, const Config& cfg);

// RAM-backed store for host tests. Public members let a test corrupt the blob or make saves fail.
class MemoryConfigStore final : public ConfigStore {
 public:
  static constexpr size_t kCapacity = 2 * Config::kBlobMaxBytes;

  size_t load(uint8_t* buf, size_t cap) override;
  bool save(const uint8_t* blob, size_t len) override;

  uint8_t data[kCapacity] = {};
  size_t size = 0;
  bool failSaves = false;
  unsigned saveCount = 0;
};

}  // namespace epdj
