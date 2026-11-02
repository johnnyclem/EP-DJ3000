// Persistent config blob in NVS (namespace "epdj", key "cfg"). Implements the core's ConfigStore.
#pragma once
#include "epdj/config.h"

namespace epdj {
namespace hal {

class NvsConfigStore final : public ConfigStore {
 public:
  // Initialises the NVS partition (erasing it if it is from an incompatible IDF version).
  bool begin();
  size_t load(uint8_t* buf, size_t cap) override;
  bool save(const uint8_t* blob, size_t len) override;
};

}  // namespace hal
}  // namespace epdj
