// The one I2C master bus (SDA/SCL from board_pins.h): AS5600 encoder, optional SSD1306, optional DAC codec probe.
#pragma once
#include <cstdint>

#include "driver/i2c_master.h"

namespace epdj {
namespace hal {

class I2cBus {
 public:
  bool begin();
  bool ok() const { return bus_ != nullptr; }
  // True if a device ACKs its address. Short timeout; never blocks the caller for long.
  bool probe(uint8_t addr7);
  i2c_master_bus_handle_t handle() const { return bus_; }

 private:
  i2c_master_bus_handle_t bus_ = nullptr;
};

}  // namespace hal
}  // namespace epdj
