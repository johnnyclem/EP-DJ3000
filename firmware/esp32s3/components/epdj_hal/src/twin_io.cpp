#include "epdj_hal/twin_io.h"

#include "driver/gpio.h"
#include "epdj_hal/twin_pins.h"
#include "esp_log.h"

namespace epdj {
namespace hal {
namespace {

const char* TAG = "twin";

std::atomic<uint16_t> g_published{0};

uint16_t counts12(int16_t sample) {
  if (sample < 0) return 0;
  // PGA ±4.096 V, so 3.3 V is 26400 counts. Scale that span onto 0..4095.
  constexpr int kRail = 26400;
  if (sample > kRail) sample = kRail;
  return static_cast<uint16_t>((static_cast<int32_t>(sample) * 4095) / kRail);
}

bool addDev(I2cBus& bus, uint8_t addr, i2c_master_dev_handle_t* out) {
  if (!bus.ok()) return false;
  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = addr;
  dc.scl_speed_hz = 400000;
  if (i2c_master_bus_add_device(bus.handle(), &dc, out) != ESP_OK) {
    *out = nullptr;
    return false;
  }
  return true;
}

}  // namespace

void publishTwinButtons(uint16_t debounced) { g_published.store(debounced, std::memory_order_relaxed); }
uint16_t publishedTwinButtons() { return g_published.load(std::memory_order_relaxed); }

bool TwinIo::writeMcp(uint8_t reg, uint16_t value) {
  const uint8_t buf[3] = {reg, static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF)};
  return i2c_master_transmit(mcp_, buf, 3, 20) == ESP_OK;
}

bool TwinIo::readMcp(uint16_t* pressed) {
  const uint8_t reg = 0x12;  // GPIOA, then GPIOB
  uint8_t b[2] = {0xFF, 0xFF};
  if (i2c_master_transmit_receive(mcp_, &reg, 1, b, 2, 2) != ESP_OK) return false;
  const uint16_t raw = static_cast<uint16_t>(static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8));
  *pressed = static_cast<uint16_t>(~raw);  // switch to GND, pressed = 0 on the wire
  return true;
}

bool TwinIo::writeAds(uint8_t cfgHi) {
  // Config register 0x01. Single shot, ±4.096 V, 860 SPS, comparator off.
  const uint8_t buf[3] = {0x01, cfgHi, 0xE3};
  return i2c_master_transmit(ads_, buf, 3, 2) == ESP_OK;
}

bool TwinIo::readAds(int16_t* sample) {
  const uint8_t reg = 0x00;
  uint8_t b[2] = {0, 0};
  if (i2c_master_transmit_receive(ads_, &reg, 1, b, 2, 2) != ESP_OK) return false;
  *sample = static_cast<int16_t>((static_cast<uint16_t>(b[0]) << 8) | b[1]);
  return true;
}

bool TwinIo::begin(I2cBus& shared, I2cBus& encoderBus) {
  const gpio_config_t leds = {
      .pin_bit_mask = (1ULL << twin_pins::kLedInA) | (1ULL << twin_pins::kLedOutA) | (1ULL << twin_pins::kLedInB) |
                      (1ULL << twin_pins::kLedOutB),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&leds);
  setLeds(0);
  ledsReady_ = true;

  const bool mcpOk = addDev(shared, twin_pins::kMcpAddress, &mcp_) && writeMcp(0x00, 0xFFFF) && writeMcp(0x0C, 0xFFFF);
  if (!mcpOk) {
    mcp_ = nullptr;
    ESP_LOGW(TAG, "MCP23017 not answering at 0x%02X on the display bus", twin_pins::kMcpAddress);
  } else {
    ESP_LOGI(TAG, "MCP23017 at 0x%02X, 16 buttons, pull-ups on", twin_pins::kMcpAddress);
  }

  if (addDev(shared, twin_pins::kAdsAddress, &ads_) && writeAds(0xC3)) {
    ESP_LOGI(TAG, "ADS1115 at 0x%02X, pitch A on A0, pitch B on A1", twin_pins::kAdsAddress);
    adsPhase_ = 1;  // the presence write already started a conversion on A0
  } else {
    ads_ = nullptr;
    ESP_LOGW(TAG, "ADS1115 not answering at 0x%02X; pitch faders stay centered", twin_pins::kAdsAddress);
  }

  if (encoderBus.ok()) encB_.begin(encoderBus);
  return mcpOk;
}

void TwinIo::poll(uint64_t nowUs) {
  if (mcp_ != nullptr && (lastButtonsUs_ == 0 || nowUs - lastButtonsUs_ >= 2000)) {
    lastButtonsUs_ = nowUs;
    uint16_t keys = buttons_;
    if (readMcp(&keys)) buttons_ = keys;
  }
  if (ads_ == nullptr) return;
  if (adsPhase_ == 0) {
    writeAds(adsChannel_ ? 0xD3 : 0xC3);  // AIN1 or AIN0, single-ended to GND
    adsPhase_ = 1;
    return;
  }
  if (adsPhase_ == 1) {
    adsPhase_ = 2;  // one more millisecond: the 860 SPS conversion is 1.2 ms
    return;
  }
  int16_t sample = 0;
  if (readAds(&sample)) {
    const uint16_t counts = counts12(sample);
    if (adsChannel_) pitchB_ = counts;
    else pitchA_ = counts;
  }
  adsChannel_ = !adsChannel_;
  adsPhase_ = 0;
}

void TwinIo::setLeds(uint8_t mask) {
  if (!ledsReady_) return;
  // Cathode to the pin, anode to 3V3 through 330 ohm. Low = lit.
  const int pins[4] = {twin_pins::kLedInA, twin_pins::kLedOutA, twin_pins::kLedInB, twin_pins::kLedOutB};
  for (int i = 0; i < 4; ++i) {
    gpio_set_level(static_cast<gpio_num_t>(pins[i]), (mask & (1u << i)) ? 0 : 1);
  }
}

}  // namespace hal
}  // namespace epdj
