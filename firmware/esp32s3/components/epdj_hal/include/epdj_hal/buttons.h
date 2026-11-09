// Push buttons, wired to GND with the internal pull-up. Debouncing is the engine's job.
// CUE and REV are this deck. The rest are the Twin shell: left PLAY and LOOP IN on this deck's
// MIDI channel, and the right deck's CUE, PLAY, LOOP IN and LOOP OUT on the other channel.
#pragma once

namespace epdj {
namespace hal {

class ButtonPins {
 public:
  void begin();
  bool cue() const;  // true = pressed
  bool rev() const;
  bool play() const;
  bool loopIn() const;
  bool cueRight() const;
  bool playRight() const;
  bool loopInRight() const;
  bool loopOutRight() const;  // false on a v1.0 board, whose status LED owns GPIO48
};

}  // namespace hal
}  // namespace epdj
