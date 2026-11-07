#include "epdj_hal/as5600.h"

#include "epdj_hal/mt6701_regs.h"
#include "epdj_hal/time.h"
#include "sdkconfig.h"

namespace epdj {
namespace hal {
namespace {
// A failed read can block for its timeout (not less than 2 ms: the driver counts ticks, so 1 ms may expire early), so a
// stuck bus costs the 1 kHz loop that much per failed read; encoder_retry.h bounds how many.
constexpr int kRunTimeoutMs = 2;
constexpr int kBootTimeoutMs = 5;
constexpr unsigned kTimingReads = 16;  // angle reads timed at boot
}  // namespace

bool As5600::filteredAngle() {
#ifdef CONFIG_EPDJ_AS5600_FILTERED_ANGLE
  return true;
#else
  return false;
#endif
}

bool As5600::readReg(uint8_t reg, uint8_t* out, size_t n, int timeoutMs) {
  return i2c_master_transmit_receive(dev_, &reg, 1, out, n, timeoutMs) == ESP_OK;
}

bool As5600::begin(I2cBus& bus) {
  if (!bus.ok()) return false;
  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = kAddress;
  dc.scl_speed_hz = 400000;
  if (i2c_master_bus_add_device(bus.handle(), &dc, &dev_) != ESP_OK) {
    dev_ = nullptr;
    return false;
  }
  const bool answered = bus.probe(kAddress);
  retry_.start(answered, nowUs());
  if (!answered) return false;
  present_.store(true);

  // The cost of one angle read, as the engine task will make it. A failed read is not counted.
  uint64_t total = 0;
  unsigned good = 0;
  for (unsigned i = 0; i < kTimingReads; ++i) {
    uint8_t b[2];
    const uint64_t t0 = nowUs();
    if (readReg(angleRegister(), b, 2, kBootTimeoutMs)) {
      total += nowUs() - t0;
      ++good;
    }
  }
  readUs_ = good > 0 ? static_cast<unsigned>((total + good / 2) / good) : 0;
  return true;
}

bool As5600::readAngle(uint16_t* angle12, uint64_t nowUs) {
  if (dev_ == nullptr || !retry_.mayRead(nowUs)) return false;
  const bool wasAnswering = retry_.answering();
  uint8_t b[2] = {0, 0};
  const bool ok = readReg(angleRegister(), b, 2, kRunTimeoutMs);
  if (!ok && wasAnswering) failures_.fetch_add(1);
  switch (retry_.report(ok, nowUs)) {
    case EncoderRetry::Change::Lost:
      losses_.fetch_add(1);
      break;
    case EncoderRetry::Change::Recovered:
      if (!present_.load()) {  // the first time it has answered since boot
        appearedLate_.store(true);
        present_.store(true);
      }
      recoveries_.fetch_add(1);
      break;
    case EncoderRetry::Change::None:
      break;
  }
  if (!ok) return false;
  *angle12 = mt6701::angle12(b[0], b[1]);
  return true;
}

}  // namespace hal
}  // namespace epdj
