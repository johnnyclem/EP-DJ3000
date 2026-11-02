#include "epdj/jog.h"

#include <cmath>

#include "epdj/util.h"

namespace epdj {
namespace {

// Ring path tuning, in angle units (1/4096 rev; 4 units = 1 tick).
// The adaptive low-pass has a noise band [lo, hi] around the filter state: inside it the angle is
// only heavily smoothed (a resting finger stays silent), beyond it the motion is passed through.
// lo is sized from the per-sample angle noise TouchRing reports (Result::angleSigma). That figure is
// conservative by ~1.5-1.8x against measured jitter in simulation, so 2 x sigma is ~3.3 real sigma.
// The first things to retune against real printed-PLA hardware.
constexpr float kRestTauS = 0.15f;       // smoothing while the innovation is inside the noise band
constexpr float kBandPerSigma = 2.0f;    // lo = this x angleSigma
constexpr float kBandMinUnits = 8.0f;    // ...but never tighter than 2 ticks
constexpr float kBandMaxUnits = 96.0f;   // ...nor looser than 24 ticks (a hopelessly noisy ring)
constexpr float kHiOverLo = 1.5f;
constexpr float kSigmaTauS = 0.02f;      // sigma varies with finger position; smooth the band
constexpr uint64_t kAnchorHoldUs = 8000; // reference = running mean of the first samples
constexpr uint64_t kGapBridgeUs = 3000;  // validity dropouts shorter than this keep the anchor
constexpr int32_t kMaxStepUnits = 1024;  // > quarter turn in one sample is a glitch, not motion
constexpr float kVelocityTauS = 0.03f;

}  // namespace

void JogTracker::configure(const Config& cfg) {
  const bool sourceChanged = cfg.source != cfg_.source;
  cfg_ = cfg;
  if (sourceChanged) reset();
}

void JogTracker::reset() {
  touched_ = false;
  anchored_ = false;
  err_ = acc_ = cum_ = excursion_ = 0.0f;
  total_ = 0;
  velocity_ = 0.0f;
}

JogTracker::Output JogTracker::update(const Input& in, uint64_t nowUs) {
  Output out;
  const float dt = dtSeconds(nowUs, lastUs_);
  lastUs_ = nowUs;
  const bool ring = cfg_.source == proto::PlatterSource::TouchRing;

  if (in.touched && !touched_) {
    touched_ = true;
    touchStartUs_ = nowUs;
    cum_ = excursion_ = 0.0f;
    out.touchDown = true;
  } else if (!in.touched && touched_) {
    touched_ = false;
    out.touchUp = true;
    out.tap = nowUs - touchStartUs_ <= static_cast<uint64_t>(cfg_.tapMaxMs) * 1000u &&
              excursion_ <= static_cast<float>(cfg_.tapMoveTicks) * 4.0f;
  }

  // A caller bug must not poison the filter: anything not a sane non-negative number means "unknown".
  const float angleSigma = (in.angleSigma >= 0.0f && in.angleSigma < 1e6f) ? in.angleSigma : 0.0f;
  const bool canMove = ring ? in.touched : cfg_.source == proto::PlatterSource::As5600;
  float moved = 0.0f;  // units of motion released by the filter this update
  if (canMove && in.angleValid) {
    lastUsableUs_ = nowUs;
    if (!anchored_) {
      anchored_ = true;
      prevAngle_ = in.angle;
      err_ = 0.0f;
      if (ring) {
        sigma_ = angleSigma;
        acc_ = 0.0f;  // each touch is independent; the AS5600 is absolute, so its carry stays
        holdUntilUs_ = nowUs + kAnchorHoldUs;
        holdSamples_ = 1;
      }
    } else {
      const int32_t delta = angleDiff4096(prevAngle_, in.angle);
      prevAngle_ = in.angle;
      if (delta > kMaxStepUnits || delta < -kMaxStepUnits) {
        anchored_ = false;
      } else if (!ring) {
        moved = static_cast<float>(delta);
      } else {
        err_ += static_cast<float>(delta);
        sigma_ += lowPassAlpha(dt, kSigmaTauS) * (angleSigma - sigma_);
        if (nowUs < holdUntilUs_) {
          // Warm-up: Y becomes the running mean of the first samples (alpha = 1/k) and the movement
          // is not reported, so the reference is not a single noisy sample and nothing jumps.
          err_ -= err_ / static_cast<float>(++holdSamples_);
        } else {
          const float mag = err_ < 0.0f ? -err_ : err_;
          const float lo = clamp(kBandPerSigma * sigma_, kBandMinUnits, kBandMaxUnits);
          const float alpha = adaptiveAlpha(mag, lowPassAlpha(dt, kRestTauS), lo, kHiOverLo * lo);
          moved = alpha * err_;
          err_ -= moved;
        }
      }
    }
  } else if (!canMove || nowUs - lastUsableUs_ > kGapBridgeUs) {
    anchored_ = false;
  }

  acc_ += moved;
  cum_ += moved;
  const float absCum = cum_ < 0.0f ? -cum_ : cum_;
  if (absCum > excursion_) excursion_ = absCum;

  const int32_t whole = static_cast<int32_t>(acc_ * 0.25f);  // truncates toward zero
  acc_ -= static_cast<float>(whole) * 4.0f;
  const int32_t ticks = cfg_.invert ? -whole : whole;
  out.ticks = ticks;
  total_ = static_cast<int32_t>(static_cast<uint32_t>(total_) + static_cast<uint32_t>(ticks));

  const float sign = cfg_.invert ? -0.25f : 0.25f;
  velocity_ += lowPassAlpha(dt, kVelocityTauS) * (sign * moved / dt - velocity_);
  return out;
}

void JogEmitter::add(int32_t ticks) { accum_ = satAdd32(accum_, ticks); }

bool JogEmitter::poll(uint64_t nowUs, uint8_t* value) {
  if (accum_ == 0 || nowUs < nextDueUs_) return false;
  const int32_t step = clamp<int32_t>(accum_, -63, 63);
  accum_ -= step;
  *value = static_cast<uint8_t>(proto::kRelativeCenter + step);
  nextDueUs_ = nowUs + kMinIntervalUs;
  return true;
}

}  // namespace epdj
