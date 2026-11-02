// CUE and REV push buttons, wired to GND with the internal pull-up. Debouncing is the engine's job.
#pragma once

namespace epdj {
namespace hal {

class ButtonPins {
 public:
  void begin();
  bool cue() const;  // true = pressed
  bool rev() const;
};

}  // namespace hal
}  // namespace epdj
