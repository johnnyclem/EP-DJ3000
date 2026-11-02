#include "epdj_hal/as5600.h"

#include "epdj_hal/time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace epdj {
namespace hal {
namespace {
// A failed read can block for its timeout (not less than 2 ms: the driver counts ticks, so 1 ms may expire early), so a
// stuck bus costs the 1 kHz loop that much per failed read; encoder_retry.h bounds how many.
constexpr int kRunTimeoutMs = 2;
constexpr int kBootTimeoutMs = 5;
constexpr unsigned kMagnetPolls = 6;   // status reads at boot, 5 ms apart, until the magnet reads Ok
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

// Polls the STATUS register until the magnet reads Ok or `polls` reads were made. The patient form (boot) sleeps between
// reads; the single read used when the chip turns up while running must not.
void As5600::readMagnet(unsigned polls) {
  magnet_ = sensor_budget::Magnet::Unknown;
  for (unsigned i = 0; i < polls; ++i) {
    uint8_t status = 0;
    if (readReg(as5600::kRegStatus, &status, 1, kBootTimeoutMs)) {
      status_ = status;
      magnet_ = sensor_budget::classifyMagnet(status);
      if (magnet_ == sensor_budget::Magnet::Ok) break;
    }
    if (polls > 1 && i + 1 < polls) vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// CONF is plain RAM until a BURN_SETTING, which is never sent: a chip that loses power forgets it, so this is repeated
// whenever the chip starts answering. Written as register, high byte, low byte in one transaction.
void As5600::applyConf(int timeoutMs) {
  const uint16_t want = as5600::kScratchConfWord;
  const uint8_t tx[3] = {as5600::kRegConf, static_cast<uint8_t>(want >> 8), static_cast<uint8_t>(want & 0xFF)};
  const bool wrote = i2c_master_transmit(dev_, tx, sizeof tx, timeoutMs) == ESP_OK;
  uint8_t rb[2] = {0, 0};
  confKnown_ = wrote && readReg(as5600::kRegConf, rb, 2, timeoutMs);
  confRead_ = confKnown_ ? static_cast<uint16_t>((rb[0] << 8) | rb[1]) : 0;
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

  readMagnet(kMagnetPolls);
  applyConf(kBootTimeoutMs);

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
        readMagnet(1);
        appearedLate_.store(true);
        present_.store(true);
      }
      applyConf(kRunTimeoutMs);
      recoveries_.fetch_add(1);  // last: the diagnostics task reads the state above once this has changed
      break;
    case EncoderRetry::Change::None:
      break;
  }
  if (!ok) return false;
  *angle12 = static_cast<uint16_t>(((b[0] << 8) | b[1]) & 0x0FFF);
  return true;
}

}  // namespace hal
}  // namespace epdj
