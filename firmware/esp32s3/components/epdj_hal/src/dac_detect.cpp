#include "epdj_hal/dac_detect.h"

#include "driver/gpio.h"
#include "epdj_hal/board_pins.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"

namespace epdj {
namespace hal {
namespace {
#if CONFIG_EPDJ_DAC_PROBE_I2C
// 7-bit control addresses of common I2S codecs/DACs. An ACK means a codec board is attached; configuring it
// is outside this firmware (a PCM5102A needs no control port at all).
constexpr uint8_t kCodecAddrs[] = {0x10, 0x11,                    // ES8388
                                   0x18,                          // TLV320DAC3100, ES8311
                                   0x1A,                          // WM8960
                                   0x4C, 0x4D, 0x4E, 0x4F};       // PCM5122 / PCM5142
#endif
}  // namespace

DacDetection detectDac(I2cBus* bus) {
  DacDetection d;
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << pins::kDacStrap;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&cfg);
  esp_rom_delay_us(500);  // let the pull-up settle through any filter capacitor
  for (uint8_t i = 0; i < DacDetection::kStrapSamples; ++i) {
    if (i != 0) esp_rom_delay_us(DacDetection::kStrapSpacingUs);
    if (gpio_get_level(static_cast<gpio_num_t>(pins::kDacStrap)) == 0) ++d.strapLow;
  }
#if CONFIG_EPDJ_DAC_PROBE_I2C
  if (bus != nullptr && bus->ok()) {
    for (uint8_t a : kCodecAddrs) {
      if (bus->probe(a)) {
        d.codec = true;
        d.codecAddr = a;
        break;
      }
    }
  }
#else
  (void)bus;
#endif
  return d;
}

}  // namespace hal
}  // namespace epdj
