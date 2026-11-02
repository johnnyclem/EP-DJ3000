#include "epdj/analog.h"

#include "epdj/util.h"

namespace epdj {
namespace {

constexpr float kRestTauS = 0.03f;
constexpr float kNoiseLoCounts = 4.0f;   // innovation <= this: pure smoothing
constexpr float kMotionHiCounts = 8.0f;  // innovation >= this: pass-through
constexpr float kGateOpenCounts = 7.0f;
constexpr float kCalmStepCounts = 0.3f;  // per-sample change of the filtered value counted as "still"
constexpr float kCloseAfterS = 0.04f;

constexpr uint32_t kQ = 65536;          // Q16 unit
constexpr uint32_t kSharpKnee = 6554;   // SHARP_CUT reaches full scale at 10% of the post-dead-zone travel

}  // namespace

AnalogControl::AnalogControl() { configure(Config{}); }

void AnalogControl::configure(const Config& cfg) {
  cfg_ = cfg;
  // The contract allows 0..400 per mille per end; clamping keeps the live span >= 20% of the travel.
  deadLoQ_ = clamp<uint32_t>(cfg.deadLoPermille, 0, 400) * kQ / 1000u;
  deadHiQ_ = clamp<uint32_t>(cfg.deadHiPermille, 0, 400) * kQ / 1000u;
}

void AnalogControl::reset() {
  started_ = false;
  open_ = false;
  calmS_ = 0.0f;
}

// Everything after the float normalisation is 32-bit integer: all intermediates stay below 2^32
// (Q16 values are <= 65536, products are formed against 16-bit constants), the one 64-bit product is
// the smoothstep, which is divided by 2^32 with a shift. No 64-bit division on the target.
uint16_t AnalogControl::map(float counts) const {
  // Normalise. (counts - min) / (max - min) is already reversed when min > max.
  const int32_t span = static_cast<int32_t>(cfg_.rawMax) - static_cast<int32_t>(cfg_.rawMin);
  float pos = span == 0 ? 0.0f : (counts - static_cast<float>(cfg_.rawMin)) / static_cast<float>(span);
  pos = clamp(pos, 0.0f, 1.0f);
  uint32_t p = static_cast<uint32_t>(pos * static_cast<float>(kQ) + 0.5f);

  // Dead zones, rescaling the live part to the full range so the output stays continuous.
  const uint32_t lo = deadLoQ_, hi = deadHiQ_;
  const uint32_t live = kQ - lo - hi;  // >= 0.2 * kQ for the contract's 0..400 per-mille ranges
  if (p <= lo) p = 0;
  else if (p >= kQ - hi) p = kQ;
  else p = (p - lo) * kQ / live;  // (p - lo) < 2^16, so the product fits in 32 bits

  switch (cfg_.curve) {
    case proto::FaderCurve::SharpCut:
      p = p >= kSharpKnee ? kQ : p * kQ / kSharpKnee;
      break;
    case proto::FaderCurve::Smooth:  // smoothstep 3x^2 - 2x^3, exact
      p = static_cast<uint32_t>((static_cast<uint64_t>(p) * p * (3u * kQ - 2u * p)) >> 32);
      break;
    case proto::FaderCurve::Linear:
      break;
  }
  if (cfg_.invert) p = kQ - p;
  return static_cast<uint16_t>((p * kMaxOut + kQ / 2) >> 16);
}

AnalogControl::Result AnalogControl::update(uint16_t raw12, uint64_t nowUs) {
  const float x = static_cast<float>(raw12);
  if (!started_) {
    started_ = true;
    filtered_ = held_ = x;
    lastUs_ = nowUs;
    value_ = map(held_);
    return {value_, true};
  }
  const float dt = dtSeconds(nowUs, lastUs_);
  lastUs_ = nowUs;

  const float innovation = x - filtered_;
  const float mag = innovation < 0.0f ? -innovation : innovation;
  const float step = adaptiveAlpha(mag, lowPassAlpha(dt, kRestTauS), kNoiseLoCounts, kMotionHiCounts) * innovation;
  filtered_ += step;

  if (!open_) {
    const float away = filtered_ - held_;
    if (away >= kGateOpenCounts || away <= -kGateOpenCounts) {
      open_ = true;
      calmS_ = 0.0f;
    }
  }
  if (open_) {
    held_ = filtered_;
    const float stepMag = step < 0.0f ? -step : step;
    calmS_ = stepMag < kCalmStepCounts ? calmS_ + dt : 0.0f;
    if (calmS_ >= kCloseAfterS) open_ = false;
  }

  const uint16_t out = map(held_);
  const bool changed = out != value_;
  value_ = out;
  return {out, changed};
}

void CalibrationTracker::begin() {
  active_ = true;
  filled_ = 0;
  haveExtremes_ = false;
  min_ = max_ = 0;
}

void CalibrationTracker::feed(uint16_t raw12) {
  if (!active_) return;
  if (filled_ < 3) {
    win_[filled_++] = raw12;
    if (filled_ < 3) return;
  } else {
    win_[0] = win_[1];
    win_[1] = win_[2];
    win_[2] = raw12;
  }
  const uint16_t a = win_[0], b = win_[1], c = win_[2];
  const uint16_t med = a < b ? (b < c ? b : (a < c ? c : a)) : (a < c ? a : (b < c ? c : b));
  if (!haveExtremes_) {
    min_ = max_ = med;
    haveExtremes_ = true;
  } else {
    if (med < min_) min_ = med;
    if (med > max_) max_ = med;
  }
}

CalibrationTracker::Status CalibrationTracker::end(Result* out) {
  if (!active_) return Status::NotActive;
  active_ = false;
  if (!haveExtremes_ || max_ - min_ < kMinSpan) return Status::SpanTooSmall;
  const uint16_t inset = static_cast<uint16_t>((max_ - min_ + 50) / 100);
  out->rawMin = static_cast<uint16_t>(min_ + inset);
  out->rawMax = static_cast<uint16_t>(max_ - inset);
  return Status::Ok;
}

}  // namespace epdj
