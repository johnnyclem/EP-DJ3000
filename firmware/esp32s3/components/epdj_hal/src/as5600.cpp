#include "epdj_hal/as5600.h"

#include "epdj_hal/time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace epdj {
namespace hal {
namespace {
constexpr uint8_t kRegStatus = 0x0B;
constexpr uint8_t kRegRawAngle = 0x0C;
// Each failed read can block for its 2 ms timeout (not less: the driver counts ticks, so 1 ms may expire early),
// so a stuck bus costs this many milliseconds of the 1 kHz loop once per backoff.
constexpr unsigned kFailuresBeforeBackoff = 2;
constexpr uint64_t kBackoffUs = 100000;
constexpr unsigned kMagnetPolls = 6;       // status reads at boot, 5 ms apart, until the magnet reads Ok
constexpr unsigned kTimingReads = 16;      // angle reads timed at boot
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

  magnet_ = sensor_budget::Magnet::Unknown;
  for (unsigned i = 0; i < kMagnetPolls; ++i) {
    const uint8_t reg = kRegStatus;
    uint8_t status = 0;
    if (i2c_master_transmit_receive(dev_, &reg, 1, &status, 1, 5) == ESP_OK) {
      status_ = status;
      magnet_ = sensor_budget::classifyMagnet(status);
      if (magnet_ == sensor_budget::Magnet::Ok) break;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }

  // The cost of one angle read, as the engine task will make it. A failed read is not counted.
  uint64_t total = 0;
  unsigned good = 0;
  for (unsigned i = 0; i < kTimingReads; ++i) {
    const uint8_t reg = kRegRawAngle;
    uint8_t b[2];
    const uint64_t t0 = nowUs();
    if (i2c_master_transmit_receive(dev_, &reg, 1, b, 2, 5) == ESP_OK) {
      total += nowUs() - t0;
      ++good;
    }
  }
  readUs_ = good > 0 ? static_cast<unsigned>((total + good / 2) / good) : 0;
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
