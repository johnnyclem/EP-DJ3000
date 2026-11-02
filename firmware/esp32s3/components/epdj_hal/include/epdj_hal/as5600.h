// AS5600 12-bit magnetic encoder (the default platter: a diametric magnet on the free-spinning platter's axle). Raw angle
// register 0x0C, 400 kHz, address 0x36.
#pragma once
#include <cstdint>

#include "epdj_hal/i2c_bus.h"
#include "epdj_hal/sensor_budget.h"

namespace epdj {
namespace hal {

class As5600 {
 public:
  static constexpr uint8_t kAddress = 0x36;

  // Probes the chip and reads the magnet status (polled for up to ~25 ms, so an AGC that is still settling is not
  // reported as a missing magnet), then times the angle read. False = no AS5600 answered; the object then stays inert.
  // Logs nothing: main.cpp prints the one boot line that says what it found.
  bool begin(I2cBus& bus);
  bool present() const { return dev_ != nullptr; }
  sensor_budget::Magnet magnet() const { return magnet_; }  // as found at begin(); not re-read while running
  uint8_t status() const { return status_; }                // raw STATUS register at begin()
  // Mean duration of one angle read, measured at begin() (0 = not measurable). The 1 kHz loop spends this much of every
  // millisecond waiting for the bus; main.cpp takes it off the ADC's share of the sensor time budget.
  unsigned readUs() const { return readUs_; }

  // Raw angle 0..4095. Returns false on a NACK/timeout (the caller reports angleValid = false for that
  // sample). After two failures in a row it stops trying for 100 ms: a dead bus costs the 1 kHz loop one
  // 2 ms timeout per failed read, so at most 4 ms in every 100.
  bool readAngle(uint16_t* angle12, uint64_t nowUs);
  uint32_t failures() const { return failures_; }

 private:
  i2c_master_dev_handle_t dev_ = nullptr;
  sensor_budget::Magnet magnet_ = sensor_budget::Magnet::Unknown;
  uint8_t status_ = 0;
  unsigned readUs_ = 0;
  unsigned failStreak_ = 0;
  uint64_t retryAtUs_ = 0;
  uint32_t failures_ = 0;
};

}  // namespace hal
}  // namespace epdj
