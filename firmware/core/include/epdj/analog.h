// Slide pot / knob: 12-bit ADC reading -> calibrated, filtered, gated 14-bit output.
#pragma once
#include <cstdint>

#include "epdj/protocol.h"

namespace epdj {

// Pipeline (PROTOCOL.md 2.4):
//   raw counts -> speed-adaptive low-pass -> change gate        (float, in ADC counts)
//   -> normalise to rawMin..rawMax (swapped min/max = inverted) -> dead zones -> curve -> invert
//   -> 14-bit                                                    (integer, Q16, monotonic by construction)
//
// Filter: heavy smoothing (tau 30 ms) while the reading stays within the ADC noise band of the
// filtered value, no smoothing once it is clearly moving, so a step of >= 8 counts is passed on the
// same sample.
//
// Gate: while still, the output is frozen and nothing is reported. It opens when the filtered value
// leaves the frozen one by >= 7 counts. 7 > 2 x 3: with +/-3 counts of bounded ADC noise the filtered
// value can never be 7 counts from the frozen one, so a still pot cannot chatter. While open the
// output follows at full resolution, and it closes again after 40 ms without movement. Consequences:
// a step of >= 8 counts is reported on the same tick; a slow drift is reported once it has moved
// 7 counts (about 0.17% of travel), which takes longer than 2 ms for speeds below ~4 counts/ms.
class AnalogControl {
 public:
  static constexpr uint16_t kMaxOut = 16383;

  struct Config {
    uint16_t rawMin = 40;
    uint16_t rawMax = 4055;  // rawMin > rawMax means the pot is wired the other way round
    bool invert = false;     // applied after the curve
    uint16_t deadLoPermille = 15;
    uint16_t deadHiPermille = 15;
    proto::FaderCurve curve = proto::FaderCurve::Linear;
  };

  struct Result {
    uint16_t value = 0;     // 14-bit
    bool changed = false;   // differs from the previously returned value (always true on the first update)
  };

  AnalogControl();

  // Live reconfiguration keeps the filter state; the next update re-evaluates the output.
  void configure(const Config& cfg);
  void reset();

  Result update(uint16_t raw12, uint64_t nowUs);
  uint16_t value() const { return value_; }

 private:
  uint16_t map(float counts) const;

  Config cfg_;
  uint32_t deadLoQ_ = 0, deadHiQ_ = 0;  // dead zones in Q16 of the travel
  bool started_ = false;
  bool open_ = false;
  float filtered_ = 0.0f;
  float held_ = 0.0f;       // gated value the output is computed from
  float calmS_ = 0.0f;      // seconds the filtered value has been (nearly) still while the gate is open
  uint64_t lastUs_ = 0;
  uint16_t value_ = 0;
};

// Min/max recorder for CAL_BEGIN / CAL_END (PROTOCOL.md 3.2).
// Single-sample ADC glitches are removed by a 3-sample median before the extremes are taken.
class CalibrationTracker {
 public:
  static constexpr uint16_t kMinSpan = 1500;

  enum class Status : uint8_t { Ok, NotActive, SpanTooSmall };
  struct Result {
    uint16_t measuredMin = 0;  // the extremes seen (after the 3-sample median)
    uint16_t measuredMax = 0;
    uint16_t rawMin = 0;  // measuredMin moved inward by 1% of the span: what CAL_END(apply = 1) writes
    uint16_t rawMax = 0;  // measuredMax moved inward by 1% of the span
  };

  void begin();
  void feed(uint16_t raw12);
  // Always leaves the tracker inactive. *out is written only on Ok.
  Status end(Result* out);
  bool active() const { return active_; }

 private:
  bool active_ = false;
  uint8_t filled_ = 0;
  uint16_t win_[3] = {};
  uint16_t min_ = 0, max_ = 0;
  bool haveExtremes_ = false;
};

}  // namespace epdj
