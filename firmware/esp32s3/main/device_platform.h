// What only the platform can do for the engine: restart, persist, play the test tone (epdj::EnginePlatform).
#pragma once
#include "epdj/config.h"
#include "epdj/engine.h"
#include "epdj_hal/nvs_store.h"

namespace epdj {
namespace app {

// Values a unit with nothing saved starts with, from Kconfig (first-boot platter source and deck).
void applyFirstBootDefaults(Config* cfg);

class DevicePlatform final : public EnginePlatform {
 public:
  explicit DevicePlatform(hal::NvsConfigStore& store) : store_(store) {}

  // Restarts in ~200 ms so the ACK that is already queued can leave on the USB endpoint first.
  // BOOTLOADER sets the ROM "force download boot" flag first (esptool over the native USB port).
  void reboot(proto::RebootMode mode) override;
  // Persists the new deck (only the deck: unsaved live tweaks stay unsaved) and restarts to re-enumerate.
  void setDeck(proto::Deck deck) override;
  bool testTone(bool on, uint16_t freqHz, uint8_t attenDb) override;
  bool saveConfig(const Config& cfg) override;

 private:
  hal::NvsConfigStore& store_;
};

}  // namespace app
}  // namespace epdj
