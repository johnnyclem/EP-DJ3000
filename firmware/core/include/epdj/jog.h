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
//  * As5600: angle is the raw 12-bit reading, free spinning: motion is reported touched or not, with
//    no filtering and exact integer conservation (quarter-tick remainder is carried, never dropped).
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
