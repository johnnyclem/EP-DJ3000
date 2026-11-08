// Twin wide deck: the controls the single-deck engine does not already own.
//
// Deck A jog, hold, CUE and pitch stay on Engine (channel 1). This bank emits the rest, on channel 1
// for the left deck and channel 2 for the right deck. The crossfader is one control, on channel 1.
// Loop and pad notes are a twin extension (42..47). They are not in contract v1.4.
//
// No heap, no clock of its own. step() is called at 1 kHz with a timestamp.
#pragma once
#include <cstdint>

#include "epdj/analog.h"
#include "epdj/jog.h"
#include "epdj/midi.h"
#include "epdj/touch_ring.h"

namespace epdj {
namespace twin {

// MCP23017 pin order. 1 = pressed. GPA0 is bit 0, GPB0 is bit 8.
constexpr uint16_t kCueA = 1u << 0;
constexpr uint16_t kPlayA = 1u << 1;
constexpr uint16_t kInA = 1u << 2;
constexpr uint16_t kOutA = 1u << 3;
constexpr uint16_t kPadA0 = 1u << 4;
constexpr uint16_t kPadA1 = 1u << 5;
constexpr uint16_t kPadA2 = 1u << 6;
constexpr uint16_t kPadA3 = 1u << 7;
constexpr uint16_t kCueB = 1u << 8;
constexpr uint16_t kPlayB = 1u << 9;
constexpr uint16_t kInB = 1u << 10;
constexpr uint16_t kOutB = 1u << 11;
constexpr uint16_t kPadB0 = 1u << 12;
constexpr uint16_t kPadB1 = 1u << 13;
constexpr uint16_t kPadB2 = 1u << 14;
constexpr uint16_t kPadB3 = 1u << 15;

// Not in protocol.h. Stable for the twin wiring.
constexpr uint8_t kLoopIn = 42;
constexpr uint8_t kLoopOut = 43;
constexpr uint8_t kPad0 = 44;  // 44, 45, 46, 47

// LED mask, active 1 = lit. Bit 0 IN A, 1 OUT A, 2 IN B, 3 OUT B.
constexpr uint8_t kLedInA = 1u << 0;
constexpr uint8_t kLedOutA = 1u << 1;
constexpr uint8_t kLedInB = 1u << 2;
constexpr uint8_t kLedOutB = 1u << 3;

struct Sample {
  uint64_t nowUs = 0;
  uint16_t buttons = 0;      // bits above, 1 = pressed, not yet debounced
  bool muteButtons = false;  // card browser owns the buttons: track edges, send nothing
  uint16_t touchB = 0;       // hold pad, larger = more touched. 0 when unwired
  bool angleValid = false;
  uint16_t angle12 = 0;  // deck B platter, 0..4095
  uint16_t pitchB = 2048;
  uint16_t cross = 2048;  // crossfader, 12-bit
};

struct Out {
  midi::Message msg[16];
  uint8_t count = 0;
  uint8_t leds = 0;
};

class Bank {
 public:
  Bank();

  void setJogInvert(bool invert);
  void setPitchInvert(bool invert);

  // Host echo of loop notes. Velocity > 0 latches the LED on; 0 clears the latch.
  // A held button lights the LED either way.
  void onMidi(const midi::Message& m);

  void step(const Sample& in, Out* out);

  uint16_t down() const { return down_; }

 private:
  void push(Out* out, const midi::Message& m);
  void emitEdge(Out* out, uint16_t bit, uint64_t nowUs, bool press);
  uint16_t debounce(uint16_t raw, uint64_t nowUs);

  uint16_t down_ = 0;
  uint64_t lockUntil_[16] = {};
  uint64_t playDownAt_[2] = {};
  uint64_t playOffAt_[2] = {};
  bool playPending_[2] = {};
  bool jogInvert_ = false;
  bool pitchInvert_ = false;
  bool touchEver_ = false;
  uint8_t ledLatch_ = 0;

  TouchRing ring_;
  StickyTouch sticky_;
  JogTracker jog_;
  JogEmitter emit_;
  AnalogControl pitch_;
  AnalogControl cross_;
};

}  // namespace twin
}  // namespace epdj
