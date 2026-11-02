#include "epdj_hal/i2c_bus.h"

#include "epdj_hal/board_pins.h"
#include "esp_log.h"

namespace epdj {
namespace hal {
namespace {
const char* TAG = "i2c";
}

bool I2cBus::begin() {
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = -1;
  cfg.sda_io_num = static_cast<gpio_num_t>(pins::kI2cSda);
  cfg.scl_io_num = static_cast<gpio_num_t>(pins::kI2cScl);
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  // The internal pull-ups only keep an unpopulated bus quiet; the AS5600 breakout brings its own 4.7k.
  cfg.flags.enable_internal_pullup = true;
  const esp_err_t err = i2c_new_master_bus(&cfg, &bus_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
    bus_ = nullptr;
    return false;
  }
  return true;
}

bool I2cBus::probe(uint8_t addr7) { return bus_ != nullptr && i2c_master_probe(bus_, addr7, 5) == ESP_OK; }

}  // namespace hal
}  // namespace epdj
