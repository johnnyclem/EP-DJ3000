// "Is a DAC attached?" (docs/ARCHITECTURE.md decision 4). The PCM5102A reference part has no I2C, so the
// primary signal is a strap pin; an optional I2C probe finds codec boards that do have a control port.
#pragma once
#include <cstdint>

#include "epdj_hal/i2c_bus.h"

namespace epdj {
namespace hal {

struct DacDetection {
  // The strap (board_pins.h kDacStrap tied to GND) is read kStrapSamples times, kStrapSpacingUs apart, so a
  // floating wire picking up noise or a contact that is still bouncing cannot decide the USB descriptors.
  static constexpr uint8_t kStrapSamples = 16;
  static constexpr uint32_t kStrapSpacingUs = 1000;

  uint8_t strapLow = 0;     // samples that read low (strap to GND)
  bool codec = false;       // a known codec address ACKed (only with EPDJ_DAC_PROBE_I2C)
  uint8_t codecAddr = 0;

  bool strapSet() const { return strapLow == kStrapSamples; }  // every sample low
  bool strapUnstable() const { return strapLow != 0 && strapLow != kStrapSamples; }
  // An unstable strap counts as absent: MIDI-only is the safe outcome, and the log says why.
  bool present() const { return strapSet() || codec; }
};

// Takes about kStrapSamples x kStrapSpacingUs (16 ms): call it once at boot, before USB starts.
// `bus` may be null or not ok: the probe is then skipped.
DacDetection detectDac(I2cBus* bus);

}  // namespace hal
}  // namespace epdj
