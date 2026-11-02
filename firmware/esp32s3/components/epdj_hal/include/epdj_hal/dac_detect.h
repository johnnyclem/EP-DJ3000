// "Is a DAC attached?" (docs/ARCHITECTURE.md decision 4). The PCM5102A reference part has no I2C, so the
// primary signal is a strap pin; an optional I2C probe finds codec boards that do have a control port.
#pragma once
#include <cstdint>

#include "epdj_hal/i2c_bus.h"

namespace epdj {
namespace hal {

struct DacDetection {
  bool strap = false;       // board_pins.h kDacStrap tied to GND
  bool codec = false;       // a known codec address ACKed (only with EPDJ_DAC_PROBE_I2C)
  uint8_t codecAddr = 0;
  bool present() const { return strap || codec; }
};

// `bus` may be null or not ok: the probe is then skipped.
DacDetection detectDac(I2cBus* bus);

}  // namespace hal
}  // namespace epdj
