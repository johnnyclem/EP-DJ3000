#include "epdj/buttons.h"

namespace epdj {
namespace {
constexpr uint8_t kBoth = proto::buttons::kCue | proto::buttons::kRev;
}

uint8_t ButtonDebouncer::update(uint8_t rawMask, uint64_t nowUs) {
  for (unsigned i = 0; i < kButtons; ++i) {
    const uint8_t bit = static_cast<uint8_t>(1u << i);
    if (nowUs < lockUntilUs_[i] || (rawMask & bit) == (state_ & bit)) continue;
    state_ = static_cast<uint8_t>((state_ & ~bit) | (rawMask & bit));
    lockUntilUs_[i] = nowUs + lockUs_;
  }
  return state_;
}

ButtonGestures::Events ButtonGestures::update(uint8_t downMask, uint64_t nowUs) {
  Events ev;
  const uint8_t prev = down_;
  down_ = static_cast<uint8_t>(downMask & kBoth);
  ev.pressed = static_cast<uint8_t>(down_ & ~prev);
  uint8_t released = static_cast<uint8_t>(prev & ~down_);

  const uint8_t swallowed = static_cast<uint8_t>(released & swallow_);
  released = static_cast<uint8_t>(released & ~swallowed);
  swallow_ = static_cast<uint8_t>(swallow_ & down_);
  reported_ = static_cast<uint8_t>((reported_ | ev.pressed) & ~released);
  ev.released = released;

  if ((down_ & kBoth) == kBoth && (prev & kBoth) != kBoth) {
    chordActive_ = true;
    chordStartUs_ = nowUs;
  } else if (chordActive_ && (down_ & kBoth) != kBoth) {
    chordActive_ = false;
    const uint64_t heldMs = (nowUs - chordStartUs_) / 1000u;
    if (heldMs >= kSwapMinMs) ev.chord = Chord::SwapDeck;
    else if (heldMs >= kCycleMinMs) ev.chord = Chord::CycleMode;
    if (ev.chord != Chord::None) {
      ev.noteOffMask = static_cast<uint8_t>(down_ & reported_);
      swallow_ = static_cast<uint8_t>(swallow_ | ev.noteOffMask);
      reported_ = static_cast<uint8_t>(reported_ & ~ev.noteOffMask);
    }
  }
  return ev;
}

uint32_t ButtonGestures::chordHeldMs(uint64_t nowUs) const {
  return chordActive_ ? static_cast<uint32_t>((nowUs - chordStartUs_) / 1000u) : 0u;
}

}  // namespace epdj
