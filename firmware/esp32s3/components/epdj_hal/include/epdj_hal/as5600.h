// AS5600 12-bit magnetic encoder (the free-spinning platter option). Raw angle register 0x0C, 400 kHz.
#pragma once
#include <cstdint>

#include "epdj_hal/i2c_bus.h"

namespace epdj {
namespace hal {

class As5600 {
 public:
  static constexpr uint8_t kAddress = 0x36;

  // Probes the chip and checks the magnet. False = no AS5600 answered; the object then stays inert.
  bool begin(I2cBus& bus);
  bool present() const { return dev_ != nullptr; }
  // Raw angle 0..4095. Returns false on a NACK/timeout (the caller reports angleValid = false for that
  // sample). After two failures in a row it stops trying for 100 ms: a dead bus costs the 1 kHz loop one
  // 2 ms timeout per failed read, so at most 4 ms in every 100.
  bool readAngle(uint16_t* angle12, uint64_t nowUs);
  uint32_t failures() const { return failures_; }

 private:
  i2c_master_dev_handle_t dev_ = nullptr;
  unsigned failStreak_ = 0;
  uint64_t retryAtUs_ = 0;
  uint32_t failures_ = 0;
};

}  // namespace hal
}  // namespace epdj
