#include "epdj_hal/buttons.h"

#include "driver/gpio.h"
#include "epdj_hal/board_pins.h"

namespace epdj {
namespace hal {
namespace {
bool pressed(int gpio) { return gpio >= 0 && gpio_get_level(static_cast<gpio_num_t>(gpio)) == 0; }
}  // namespace

void ButtonPins::begin() {
  uint64_t mask = (1ULL << pins::kCue) | (1ULL << pins::kRev) | (1ULL << pins::kPlay) | (1ULL << pins::kLoopIn) |
                  (1ULL << pins::kCueRight) | (1ULL << pins::kPlayRight) | (1ULL << pins::kLoopInRight);
  if (pins::kLoopOutRight >= 0) mask |= 1ULL << pins::kLoopOutRight;
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = mask;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
}

bool ButtonPins::cue() const { return pressed(pins::kCue); }
bool ButtonPins::rev() const { return pressed(pins::kRev); }
bool ButtonPins::play() const { return pressed(pins::kPlay); }
bool ButtonPins::loopIn() const { return pressed(pins::kLoopIn); }
bool ButtonPins::cueRight() const { return pressed(pins::kCueRight); }
bool ButtonPins::playRight() const { return pressed(pins::kPlayRight); }
bool ButtonPins::loopInRight() const { return pressed(pins::kLoopInRight); }
bool ButtonPins::loopOutRight() const { return pressed(pins::kLoopOutRight); }

}  // namespace hal
}  // namespace epdj
