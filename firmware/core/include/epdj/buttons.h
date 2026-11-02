// CUE / REV buttons: debounce and the CUE+REV chord gestures (PROTOCOL.md 2.5).
#pragma once
#include <cstdint>

#include "epdj/protocol.h"

namespace epdj {

// Edge-triggered debounce: the first edge of a button is accepted on the very sample it arrives
// (zero latency) and then the button is locked for debounceMs, so contact bounce cannot produce
// extra events. If the contact settled on the opposite level when the lock ends, that is reported
// then. Masks use proto::buttons bits (kCue, kRev); 1 = pressed.
class ButtonDebouncer {
 public:
  static constexpr unsigned kButtons = 2;

  void setDebounceMs(uint8_t ms) { lockUs_ = static_cast<uint64_t>(ms) * 1000u; }
  // Returns the debounced mask.
  uint8_t update(uint8_t rawMask, uint64_t nowUs);

 private:
  uint64_t lockUs_ = 4000;
  uint64_t lockUntilUs_[kButtons] = {};
  uint8_t state_ = 0;
};

// Turns the debounced mask into per-button edges (never delayed) and the CUE+REV chord.
//
// Chord: both buttons down at once. The hold time runs from the moment the second one went down to
// the moment the first one is released; at that release it is evaluated:
//   >= 5 s -> SwapDeck, >= 2 s -> CycleMode, otherwise nothing.
// Note that the buttons were already reported down while the chord was building, because single
// presses are never held back. When a chord fires, the button that is still down gets a note-off from
// the engine right then (Events::noteOffMask) and its later release is swallowed, so exactly one
// note-off is ever produced per button.
class ButtonGestures {
 public:
  static constexpr uint32_t kCycleMinMs = 2000;
  static constexpr uint32_t kSwapMinMs = 5000;

  enum class Chord : uint8_t { None, CycleMode, SwapDeck };

  struct Events {
    uint8_t pressed = 0;       // buttons newly reported down this update
    uint8_t released = 0;      // buttons newly reported up this update (swallowed releases excluded)
    Chord chord = Chord::None;
    uint8_t noteOffMask = 0;   // with chord != None: still-down buttons the engine must release now
  };

  Events update(uint8_t downMask, uint64_t nowUs);

  // Buttons the engine currently believes are down (what it has sent note-ons for and no note-off yet).
  uint8_t reportedDown() const { return reported_; }
  // Time both buttons have been held together, 0 when no chord is building. For LED feedback so the
  // user can tell when the 2 s / 5 s thresholds have passed.
  uint32_t chordHeldMs(uint64_t nowUs) const;

 private:
  uint8_t down_ = 0;
  uint8_t reported_ = 0;
  uint8_t swallow_ = 0;
  bool chordActive_ = false;
  uint64_t chordStartUs_ = 0;
};

}  // namespace epdj
