#include "epdj/touch_ring.h"

#include <cmath>

#include "epdj/util.h"

namespace epdj {
namespace {

constexpr float kTwoPi = 6.28318530718f;

// Per-sector input smoothing after the median: ~2 ms, halves ADC noise, cheap enough not to hurt scratching.
constexpr float kSmoothTauS = 0.002f;

constexpr uint64_t kLearnUs = 250000;
constexpr uint64_t kSettleUs = 3000000;
constexpr uint64_t kMaxTouchUs = 120000000;
constexpr float kLearnTauS = 0.02f;
constexpr float kSlowTauS = 5.0f;
constexpr float kSettleUpTauS = 0.15f;
constexpr float kDownFastTauS = 0.08f;

// Samples a threshold crossing of the conditioned reading must persist.
constexpr uint8_t kConfirmSamples = 2;

// Gain learning: leaky peak-hold per sector, updated only while that sector is the strongest one.
constexpr float kPeakAttackTauS = 0.01f;
constexpr float kPeakReleaseTauS = 20.0f;   // of dominant time, not wall time
constexpr float kTrustMs = 100.0f;          // dominant time before a sector's peak is believed
constexpr float kGainTauS = 0.5f;           // gains glide so the angle estimate never steps
constexpr float kGainMin = 0.5f, kGainMax = 2.0f;

// Angle estimator floor on a sector's activation: a fraction of the strongest sector (so the estimate
// depends on the footprint's shape, not its amplitude, and a lifting finger does not drag the angle
// towards a sector centre) plus a small absolute part that only rejects idle noise.
constexpr float kFloorRelative = 0.12f;
constexpr float kFloorAbsoluteOfOff = 0.25f;

// Confidence: phasor magnitude vs 2x the touch-on level, and coherence |sum a_i e^j| / sum a_i.
// A single finger gives >= ~0.5 for any ring size; a palm / common-mode shift gives ~0.
constexpr float kCoherenceLo = 0.15f, kCoherenceHi = 0.35f;
constexpr uint8_t kMinConfidence = 32;

// The angle is only reported while the footprint amplitude is steady: the fast peak (already smoothed
// over ~2 ms) must agree with a slower (6 ms) average within +/-20%. A landing or lifting finger fails
// this, which is when the footprint is incomplete and noise dominates its skirt sectors; without the
// test a lift drags the estimate towards the nearest sector centre by tens of ticks.
constexpr float kPeakSlowTauS = 0.006f;

// Idle-noise tracking. Deviations beyond 4 sigma (a finger lifting, a spike) are not learned from.
constexpr float kNoiseTauS = 1.0f;
constexpr float kNoiseFastTauS = 0.2f;  // during the power-up settle window
constexpr float kNoiseInitVar = 4.0f;   // permille^2, until measured
constexpr float kNoiseMinVar = 0.01f;
constexpr float kRadToUnits = 4096.0f / kTwoPi;
constexpr float kSettledLo = 0.8f, kSettledHi = 1.25f;

inline uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
  return a < b ? (b < c ? b : (a < c ? c : a)) : (a < c ? a : (b < c ? c : b));
}

}  // namespace

TouchRing::TouchRing() {
  configure(Config{});
  reset();
}

void TouchRing::configure(const Config& cfg) {
  const uint8_t n = clamp<uint8_t>(cfg.sectors, kMinSectors, kMaxSectors);
  const bool layoutChanged = n != sectors_;
  sectors_ = n;
  on_ = static_cast<float>(cfg.onPermille < 1 ? 1 : cfg.onPermille);
  off_ = static_cast<float>(cfg.offPermille);
  if (off_ > on_) off_ = on_;
  equalize_ = cfg.equalizeGains;
  for (unsigned i = 0; i < n; ++i) {
    const float phi = kTwoPi * static_cast<float>(i) / static_cast<float>(n);
    sin_[i] = std::sin(phi);
    cos_[i] = std::cos(phi);
  }
  if (layoutChanged) reset();
  if (!equalize_) {
    for (unsigned i = 0; i < kMaxSectors; ++i) gain_[i] = 1.0f;
  }
}

void TouchRing::reset() {
  started_ = false;
  peakSlow_ = 0.0f;
  touched_ = false;
  confirm_ = 0;
  lastAngle_ = 0;
  for (unsigned i = 0; i < kMaxSectors; ++i) {
    s_[i] = 0.0f;
    prev1_[i] = prev2_[i] = 0;
    base_[i] = 1.0f;
    peakHold_[i] = 0.0f;
    noiseVar_[i] = kNoiseInitVar;
    observedMs_[i] = 0.0f;
    gain_[i] = 1.0f;
  }
}

void TouchRing::learnGains(float peak, unsigned dominant, float dt) {
  if (!touched_ || !equalize_ || peak < on_) return;
  float& hold = peakHold_[dominant];
  if (hold <= 0.0f || peak > hold) {
    hold += (hold <= 0.0f ? 1.0f : lowPassAlpha(dt, kPeakAttackTauS)) * (peak - hold);
  } else {
    hold += lowPassAlpha(dt, kPeakReleaseTauS) * (peak - hold);
  }
  observedMs_[dominant] += dt * 1000.0f;

  // Reference = mean peak of the sectors seen long enough; needs two to say anything about ratios.
  float sum = 0.0f;
  unsigned trusted = 0;
  for (unsigned i = 0; i < sectors_; ++i) {
    if (observedMs_[i] >= kTrustMs && peakHold_[i] > 0.0f) {
      sum += peakHold_[i];
      ++trusted;
    }
  }
  const float ref = trusted >= 2 ? sum / static_cast<float>(trusted) : 0.0f;
  const float a = lowPassAlpha(dt, kGainTauS);
  for (unsigned i = 0; i < sectors_; ++i) {
    float target = 1.0f;
    if (ref > 0.0f && observedMs_[i] >= kTrustMs && peakHold_[i] > 0.0f) {
      target = clamp(ref / peakHold_[i], kGainMin, kGainMax);
    }
    gain_[i] += a * (target - gain_[i]);
  }
}

float TouchRing::noiseSigma(unsigned sector) const { return std::sqrt(noiseVar_[sector]); }

TouchRing::Result TouchRing::update(const uint16_t* raw, uint64_t nowUs) {
  const unsigned n = sectors_;
  if (!started_) {
    for (unsigned i = 0; i < n; ++i) {
      s_[i] = static_cast<float>(raw[i]);
      prev1_[i] = prev2_[i] = raw[i];
      base_[i] = s_[i] < 1.0f ? 1.0f : s_[i];
    }
    started_ = true;
    peakSlow_ = 0.0f;
    startUs_ = lastUs_ = nowUs;
  }
  const float dt = dtSeconds(nowUs, lastUs_);
  lastUs_ = nowUs;

  const float aSmooth = lowPassAlpha(dt, kSmoothTauS);
  float rise[kMaxSectors];
  float peak = -1e9f;
  unsigned dominant = 0;
  for (unsigned i = 0; i < n; ++i) {
    const uint16_t m = median3(raw[i], prev1_[i], prev2_[i]);
    prev2_[i] = prev1_[i];
    prev1_[i] = raw[i];
    s_[i] += aSmooth * (static_cast<float>(m) - s_[i]);
    rise[i] = (s_[i] - base_[i]) * 1000.0f / base_[i];
    if (rise[i] > peak) {
      peak = rise[i];
      dominant = i;
    }
  }

  const float peakPos = peak > 0.0f ? peak : 0.0f;
  peakSlow_ += lowPassAlpha(dt, kPeakSlowTauS) * (peakPos - peakSlow_);
  const bool settled = peakPos >= kSettledLo * peakSlow_ && peakPos <= kSettledHi * peakSlow_;

  Result r;
  const uint64_t age = nowUs - startUs_;
  r.ready = age >= kLearnUs;

  if (!r.ready) {
    const float a = lowPassAlpha(dt, kLearnTauS);
    for (unsigned i = 0; i < n; ++i) {
      base_[i] += a * (s_[i] - base_[i]);
      if (base_[i] < 1.0f) base_[i] = 1.0f;
    }
    touched_ = false;
    confirm_ = 0;
    r.angle = lastAngle_;
    return r;
  }

  // ---- touch state with hysteresis and a short confirmation ----
  const bool crossing = touched_ ? (peak < off_) : (peak >= on_);
  if (crossing) {
    if (++confirm_ >= kConfirmSamples) {
      touched_ = !touched_;
      confirm_ = 0;
      if (touched_) touchStartUs_ = nowUs;
    }
  } else {
    confirm_ = 0;
  }
  if (touched_ && nowUs - touchStartUs_ > kMaxTouchUs) {
    for (unsigned i = 0; i < n; ++i) base_[i] = s_[i] < 1.0f ? 1.0f : s_[i];
    touched_ = false;
    confirm_ = 0;
    for (unsigned i = 0; i < n; ++i) rise[i] = 0.0f;
    peak = 0.0f;
  }

  // ---- baseline ----
  const float margin = 0.5f * off_;
  const float aSlow = lowPassAlpha(dt, kSlowTauS);
  const float aDown = lowPassAlpha(dt, kDownFastTauS);
  const float aSettle = lowPassAlpha(dt, kSettleUpTauS);
  const bool settling = age < kSettleUs;
  for (unsigned i = 0; i < n; ++i) {
    const float d = s_[i] - base_[i];
    float a;
    if (rise[i] < -margin) {
      a = aDown;
    } else if (touched_) {
      continue;
    } else {
      a = (settling && d > 0.0f) ? aSettle : aSlow;
    }
    base_[i] += a * d;
    if (base_[i] < 1.0f) base_[i] = 1.0f;
  }

  if (!touched_) {
    const float aNoise = lowPassAlpha(dt, settling ? kNoiseFastTauS : kNoiseTauS);
    for (unsigned i = 0; i < n; ++i) {
      const float d2 = rise[i] * rise[i];
      if (d2 < 16.0f * noiseVar_[i]) {
        noiseVar_[i] += aNoise * (d2 - noiseVar_[i]);
        if (noiseVar_[i] < kNoiseMinVar) noiseVar_[i] = kNoiseMinVar;
      }
    }
  }

  learnGains(peak, dominant, dt);

  // ---- finger angle: phasor over noise-floored, gain-equalised activations ----
  r.touched = touched_;
  r.peakPermille = static_cast<uint16_t>(clamp(peak, 0.0f, 65535.0f));
  r.angle = lastAngle_;
  if (touched_) {
    const float relFloor = kFloorRelative * peak;
    const float absFloor = kFloorAbsoluteOfOff * off_;
    const float noiseFloor = relFloor > absFloor ? relFloor : absFloor;
    float x = 0.0f, y = 0.0f, sum = 0.0f;
    float act[kMaxSectors];
    for (unsigned i = 0; i < n; ++i) {
      const float a = rise[i] * gain_[i] - noiseFloor;
      act[i] = a > 0.0f ? a : 0.0f;
      x += act[i] * sin_[i];
      y += act[i] * cos_[i];
      sum += act[i];
    }
    if (sum > 0.0f) {
      const float mag = std::sqrt(x * x + y * y);
      const float amp = clamp(mag / (2.0f * on_), 0.0f, 1.0f);
      const float coh = clamp((mag / sum - kCoherenceLo) / (kCoherenceHi - kCoherenceLo), 0.0f, 1.0f);
      r.confidence = static_cast<uint8_t>(255.0f * (amp < coh ? amp : coh) + 0.5f);
      float rev = std::atan2(x, y) / kTwoPi;
      if (rev < 0.0f) rev += 1.0f;
      const uint16_t angle = static_cast<uint16_t>(static_cast<int32_t>(rev * 4096.0f + 0.5f) & 4095);
      if (r.confidence >= kMinConfidence && settled) {
        lastAngle_ = angle;
        r.angle = angle;
        r.angleValid = true;
        // d(angle)/d(a_i) = (y sin_i - x cos_i) / |phasor|^2 ; independent sector noise adds in quadrature.
        float var = 0.0f;
        for (unsigned i = 0; i < n; ++i) {
          if (act[i] <= 0.0f) continue;
          const float w = y * sin_[i] - x * cos_[i];
          var += w * w * gain_[i] * gain_[i] * noiseVar_[i];
        }
        r.angleSigma = std::sqrt(var) / (mag * mag) * kRadToUnits;
      }
    }
  }
  return r;
}

}  // namespace epdj
