// Platter motion -> jog ticks (1024/rev), touch edges, tap detection, and the 500 Hz CC emitter.
#pragma once
#include <cstdint>

#include "epdj/protocol.h"

namespace epdj {

// Turns an angle stream (1/4096 rev, clockwise positive) into signed jog ticks.
//
//  * TouchRing: angle comes from TouchRing while touched and valid. Every (re-)anchor sets the
//    reference to the running mean of the first 8 ms of samples and reports nothing meanwhile, so a
//    landing finger never produces a jump and the reference is not one noisy sample. Validity
//    dropouts shorter than 3 ms keep the anchor. The noisy phasor angle goes through a speed-adaptive
//    low-pass whose noise band follows the angle uncertainty TouchRing reports: heavy smoothing
//    inside the band (a resting finger is silent), pass-through beyond it (a fast scratch is not
//    delayed). A quiet ring therefore gets a tight band and low lag, a noisy one a wide band.
//  * As5600: angle is the 12-bit reading (the chip's filtered ANGLE register by default), free spinning: the
//    tracker counts motion touched or not, with no filtering of its own and exact integer conservation
//    (quarter-tick remainder is carried, never dropped). Whether untouched motion is also SENT is the engine's
//    jogUntouched policy, applied where ticks enter the JogEmitter, so total() always counts everything.
//  * TouchHoldOnly: touch and tap only; ticks are always 0.
//
// Tap = touch down -> up within tapMaxMs where the largest excursion from the touch-down angle stays
// <= tapMoveTicks. Excursion (not path length) is used so sensor jitter cannot disqualify a tap.
class JogTracker {
 public:
  struct Config {
    proto::PlatterSource source = proto::PlatterSource::TouchRing;
    bool invert = false;
    uint16_t tapMaxMs = 180;
    uint8_t tapMoveTicks = 8;
  };

  struct Input {
    bool touched = false;     // TouchRing::Result::touched (all platter sources)
    bool angleValid = false;  // TouchRing::Result::angleValid, or "AS5600 read OK"
    uint16_t angle = 0;       // 1/4096 rev; TouchRing angle or the AS5600 12-bit reading
    float angleSigma = 0.0f;  // TouchRing::Result::angleSigma (ring path); 0 = unknown, tightest band
  };

  struct Output {
    int32_t ticks = 0;        // jog ticks produced this update, jogInvert applied
    bool touchDown = false;   // rising edge of Input::touched
    bool touchUp = false;     // falling edge
    bool tap = false;         // set together with touchUp when the touch qualified as a tap
  };

  JogTracker() = default;

  // Live reconfiguration; changing the source resets the tracker.
  void configure(const Config& cfg);
  void reset();

  Output update(const Input& in, uint64_t nowUs);

  bool touched() const { return touched_; }
  int32_t total() const { return total_; }           // running sum of ticks (wraps), for telemetry
  float velocity() const { return velocity_; }       // smoothed ticks/s, signed, for diagnostics

 private:
  Config cfg_;
  bool touched_ = false;
  uint64_t touchStartUs_ = 0, lastUs_ = 0, lastUsableUs_ = 0, holdUntilUs_ = 0;
  bool anchored_ = false;
  uint32_t holdSamples_ = 0;
  uint16_t prevAngle_ = 0;
  float err_ = 0.0f;        // ring path: unsent part of the raw motion (the filter state)
  float sigma_ = 0.0f;      // ring path: smoothed angleSigma
  float acc_ = 0.0f;        // angle units not yet emitted as whole ticks; |acc_| < 4 after each update
  float cum_ = 0.0f;        // signed motion since touch-down, units
  float excursion_ = 0.0f;  // max |cum_| since touch-down, units
  int32_t total_ = 0;
  float velocity_ = 0.0f;
};

// Motion-sticky release for the hold pad (AS5600 and hold-only platters; the ring has no such problem). The idea is the
// original Field Jog author's: on a free-spinning platter the pad's signal can drop out in the middle of a long contact
// while the platter is still being scratched, and a false touch-up there would drop the DJ app out of scratch mode (note
// off, then note on again a moment later). So once a touch has lasted longer than kStickyAfterMs, a release from the pad
// is only accepted after the platter has made no jog tick for kStickyMotionMs. Shorter touches release at once, and a
// platter that is still releases within kStickyMotionMs of the pad saying so: it can never stick.
//
// The price, by design: a long touch followed by a hand lifted off a SPINNING platter stays "touched" until the platter
// stops (a coast of seconds on a good bearing).
//
// update() sees the pad's verdict before the tracker runs, noteMotion() the tracker's ticks after it, so the motion
// seen by update() is the previous sample's: one millisecond, far below kStickyMotionMs.
class StickyTouch {
 public:
  static constexpr uint32_t kStickyAfterMs = 7000;  // a touch longer than this (strictly) is "long"
  static constexpr uint32_t kStickyMotionMs = 80;   // stillness that confirms a release after a long touch (reached = accepted)

  bool update(bool padTouched, uint64_t nowUs);
  void noteMotion(bool moved, uint64_t nowUs) {
    if (moved) lastMotionUs_ = nowUs;
  }
  void reset() { touched_ = false; }
  bool touched() const { return touched_; }

 private:
  bool touched_ = false;
  uint64_t startUs_ = 0, lastMotionUs_ = 0;
};

// Accumulates ticks and hands out relative-CC values (64 + clamp(accum, -63, 63)) at most once per
// 2 ms. The part that does not fit stays in the accumulator, so motion is delayed, never lost, and
// the value 64 ("no change") is never produced.
class JogEmitter {
 public:
  static constexpr uint32_t kMinIntervalUs = 2000;

  void add(int32_t ticks);
  // True and *value set when a message is due. Call every engine tick.
  bool poll(uint64_t nowUs, uint8_t* value);
  int32_t pending() const { return accum_; }
  void clear() { accum_ = 0; }

 private:
  int32_t accum_ = 0;
  uint64_t nextDueUs_ = 0;
};

}  // namespace epdj
