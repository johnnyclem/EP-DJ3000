// SSD1306 128x32 on the AS5600's I2C bus (address 0x3C, else 0x3D). A full frame is 512 bytes, about 12 ms at
// 400 kHz, so this runs once at boot, before the 1 kHz engine task exists. Nothing here is called from that task.
#pragma once
#include <cstdint>

#include "epdj_hal/i2c_bus.h"

namespace epdj {
namespace hal {

struct OledBringup {
  bool present = false;
  uint8_t address = 0;  // 0 when nothing answered
};

// Turns the panel on and paints "EP-DJ3000", the deck letter, and `line2` (a short encoder line). No-op when the
// bus did not start or neither address ACKs. `deck` is 'A' or 'B'.
OledBringup oledBringup(I2cBus& bus, char deck, const char* line2);

}  // namespace hal
}  // namespace epdj
