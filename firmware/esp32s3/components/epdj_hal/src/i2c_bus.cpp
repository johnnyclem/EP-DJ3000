#include "epdj_hal/i2c_bus.h"

#include "driver/gpio.h"
#include "epdj_hal/board_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace epdj {
namespace hal {
namespace {
const char* TAG = "i2c";
}

bool openBus(int sda, int scl, i2c_master_bus_handle_t* out) {
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = -1;
  cfg.sda_io_num = static_cast<gpio_num_t>(sda);
  cfg.scl_io_num = static_cast<gpio_num_t>(scl);
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;
  const esp_err_t err = i2c_new_master_bus(&cfg, out);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2c_new_master_bus SDA %d SCL %d: %s", sda, scl, esp_err_to_name(err));
    *out = nullptr;
    return false;
  }
  return true;
}

bool answers(i2c_master_bus_handle_t bus, uint8_t addr) {
  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = addr;
  dc.scl_speed_hz = 100000;
  i2c_master_dev_handle_t dev = nullptr;
  if (i2c_master_bus_add_device(bus, &dc, &dev) != ESP_OK) return false;
  const uint8_t b = 0x00;
  const esp_err_t err = i2c_master_transmit(dev, &b, 1, 20);
  i2c_master_bus_rm_device(dev);
  return err == ESP_OK;
}

bool anyDevice(i2c_master_bus_handle_t bus) {
  return answers(bus, 0x3C) || answers(bus, 0x3D) || answers(bus, 0x36);
}

// Drop the previous master and claim `sda`/`scl`. A failed open leaves the bus null.
bool reopen(i2c_master_bus_handle_t* bus, int sda, int scl) {
  if (*bus != nullptr) {
    i2c_del_master_bus(*bus);
    *bus = nullptr;
  }
  return openBus(sda, scl, bus);
}

bool I2cBus::begin() {
  // Sample the wires before the I2C driver claims them. A 0 means that line is held low.
  const gpio_config_t idle = {
      .pin_bit_mask = (1ULL << pins::kI2cSda) | (1ULL << pins::kI2cScl),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&idle);
  vTaskDelay(pdMS_TO_TICKS(2));
  const int sdaLevel = gpio_get_level(static_cast<gpio_num_t>(pins::kI2cSda));
  const int sclLevel = gpio_get_level(static_cast<gpio_num_t>(pins::kI2cScl));
  ESP_LOGW(TAG, "idle levels: SDA GPIO%d=%d  SCL GPIO%d=%d  (both should be 1)", pins::kI2cSda, sdaLevel,
           pins::kI2cScl, sclLevel);

  // A 0.91" module can still be in reset on the first scan. Ask the labelled pins again before swapping,
  // and if nothing answers either way put those pins back. Leaving the swap in place made a late ACK
  // look like a successful init on the wrong pair.
  sda_ = pins::kI2cSda;
  scl_ = pins::kI2cScl;
  if (!reopen(&bus_, sda_, scl_)) return false;
  if (anyDevice(bus_)) return true;
  vTaskDelay(pdMS_TO_TICKS(200));
  if (anyDevice(bus_)) {
    ESP_LOGW(TAG, "answered on the second try, SDA GPIO%d SCL GPIO%d", sda_, scl_);
    return true;
  }

  ESP_LOGW(TAG, "no device with SDA GPIO%d SCL GPIO%d; trying the pair crossed", sda_, scl_);
  sda_ = pins::kI2cScl;
  scl_ = pins::kI2cSda;
  if (!reopen(&bus_, sda_, scl_)) return false;
  bool crossed = anyDevice(bus_);
  if (!crossed) {
    vTaskDelay(pdMS_TO_TICKS(200));
    crossed = anyDevice(bus_);
  }
  if (crossed) {
    ESP_LOGW(TAG, "answered with the wires crossed: SDA is on GPIO%d, SCL is on GPIO%d. Swap those two wires",
             sda_, scl_);
    return true;
  }

  ESP_LOGW(TAG, "no device with the wires crossed either; restoring SDA GPIO%d SCL GPIO%d", pins::kI2cSda,
           pins::kI2cScl);
  sda_ = pins::kI2cSda;
  scl_ = pins::kI2cScl;
  return reopen(&bus_, sda_, scl_);
}

// A one-byte write, not i2c_master_probe. On this IDF the probe is only START+STOP, and that transaction
// never completes here (the wait expires with no NACK and no done), so a chip that is present looks absent
// and the panel is never initialised. Writing 0x00 is harmless: the SSD1306 treats it as a command control
// byte, the AS5600 treats it as "next read is the angle register".
bool I2cBus::probe(uint8_t addr7) {
  if (bus_ == nullptr) return false;
  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = addr7;
  dc.scl_speed_hz = 100000;
  i2c_master_dev_handle_t dev = nullptr;
  if (i2c_master_bus_add_device(bus_, &dc, &dev) != ESP_OK) return false;
  const uint8_t b = 0x00;
  const esp_err_t err = i2c_master_transmit(dev, &b, 1, 20);
  i2c_master_bus_rm_device(dev);
  return err == ESP_OK;
}

}  // namespace hal
}  // namespace epdj
