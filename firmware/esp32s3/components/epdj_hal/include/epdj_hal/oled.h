// SSD1306 128x32 on the AS5600's I2C bus (address 0x3C, else 0x3D). A frame is 512 bytes. The first paint
// runs at boot, before the 1 kHz engine task exists. Later redraws are oledLines, and not from that task.
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

// Redraws the glass with two lines. No-op until oledBringup has succeeded.
// A frame is about 100 ms on the 100 kHz bus, and that bus is also the encoder's, so call it when the
// text changes, not on a timer.
void oledLines(const char* top, const char* bottom);

}  // namespace hal
}  // namespace epdj
