#include "epdj_hal/as5600.h"

#include "esp_log.h"

namespace epdj {
namespace hal {
namespace {
const char* TAG = "as5600";
constexpr uint8_t kRegStatus = 0x0B;  // bit 5 MD magnet detected, bit 4 ML too weak, bit 3 MH too strong
constexpr uint8_t kRegRawAngle = 0x0C;
// Each failed read can block for its 2 ms timeout (not less: the driver counts ticks, so 1 ms may expire early),
// so a stuck bus costs this many milliseconds of the 1 kHz loop once per backoff.
constexpr unsigned kFailuresBeforeBackoff = 2;
constexpr uint64_t kBackoffUs = 100000;
}  // namespace

bool As5600::begin(I2cBus& bus) {
  if (!bus.probe(kAddress)) return false;
  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = kAddress;
  dc.scl_speed_hz = 400000;
  if (i2c_master_bus_add_device(bus.handle(), &dc, &dev_) != ESP_OK) {
    dev_ = nullptr;
    return false;
  }
  uint8_t reg = kRegStatus, status = 0;
  if (i2c_master_transmit_receive(dev_, &reg, 1, &status, 1, 5) == ESP_OK) {
    ESP_LOGI(TAG, "found at 0x%02X, status 0x%02X (%s)", kAddress, status,
             (status & 0x20) ? "magnet detected" : ((status & 0x10) ? "magnet too weak" : ((status & 0x08) ? "magnet too strong" : "no magnet")));
  }
  return true;
}

bool As5600::readAngle(uint16_t* angle12, uint64_t nowUs) {
  if (dev_ == nullptr || nowUs < retryAtUs_) return false;
  const uint8_t reg = kRegRawAngle;
  uint8_t b[2] = {0, 0};
  if (i2c_master_transmit_receive(dev_, &reg, 1, b, 2, 2) != ESP_OK) {
    ++failures_;
    if (++failStreak_ >= kFailuresBeforeBackoff) {
      retryAtUs_ = nowUs + kBackoffUs;
      failStreak_ = 0;
    }
    return false;
  }
  failStreak_ = 0;
  *angle12 = static_cast<uint16_t>(((b[0] << 8) | b[1]) & 0x0FFF);
  return true;
}

}  // namespace hal
}  // namespace epdj
