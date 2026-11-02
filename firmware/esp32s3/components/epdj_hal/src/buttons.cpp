#include "epdj_hal/buttons.h"

#include "driver/gpio.h"
#include "epdj_hal/board_pins.h"

namespace epdj {
namespace hal {

void ButtonPins::begin() {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = (1ULL << pins::kCue) | (1ULL << pins::kRev);
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
}

bool ButtonPins::cue() const { return gpio_get_level(static_cast<gpio_num_t>(pins::kCue)) == 0; }
bool ButtonPins::rev() const { return gpio_get_level(static_cast<gpio_num_t>(pins::kRev)) == 0; }

}  // namespace hal
}  // namespace epdj
