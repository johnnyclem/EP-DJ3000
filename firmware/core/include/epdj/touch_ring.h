// Segmented capacitive touch ring (conductive-PLA platter): touch detection + finger angle.
// Real-time safe: fixed arrays, no heap, one atan2f per update while touched.
#pragma once
#include <cstdint>

namespace epdj {

// Input convention: the HAL normalises readings so that LARGER raw = MORE touched.
// Sector i is centred at i/N revolution, clockwise from 12 o'clock, so angle 0 = 12 o'clock and
// angle grows clockwise as seen from above (same convention as the AS5600 path in JogTracker).
//
// Conditioning: a 3-sample median per sector (removes isolated EMI spikes of any size) feeds a ~2 ms
// low-pass; touch decisions and the angle both use that conditioned reading. rise = (reading - baseline)
// / baseline, in per mille.
//
// Touch: touched when the strongest sector's rise stays >= onPermille (down) / < offPermille (up) for
// 2 samples; about 3 ms latency. offPermille is clamped to <= onPermille.
//
// Angle: phasor sum of per-sector activations a_i = rise_i x gain_i - floor, floor = max(12% of the peak,
// a small noise margin), so the estimate depends on the footprint's shape, not its amplitude. Reported
// valid only while touched, confidence >= 32 and the amplitude is steady (peak within +/-20% of its
// 6 ms average), which excludes a landing or lifting fingertip. With a 1.5-sector finger and no noise
// the estimate is monotonic with <= 0.1 sector pitch of systematic error (3..8 sectors).
//
// Gains: unequal sector area / capacitance is equalised by a leaky peak-hold per sector (updated only
// while that sector is the strongest, only once it has been dominant for 100 ms), gains clamped to
// 0.5..2 and glided in over ~0.5 s. Learned at run time and NOT persisted: a fresh boot starts at 1.
//
// Noise: the idle variance of every sector's conditioned reading is tracked (frozen while touched) and
// propagated through the phasor to a per-sample angle uncertainty (Result::angleSigma), which the jog
// filter uses to size its noise band: a quiet ring stays responsive, a noisy one stays silent.
//
// Baseline policy (per sector, all rates are time constants so tick jitter does not matter):
//  * Power-up learn window (250 ms): baseline snaps to the readings; touched is forced false.
//  * Untouched: baseline follows the conditioned reading slowly (5 s) in both directions, and fast
//    (80 ms) when the reading falls clearly below it (a finger that was resting at power-up lifted,
//    so the baseline was contaminated high). A ramp of rate R leaves a residual R x 5 s, so thermal
//    drift up to ~1.5 per mille/s (9% per minute) stays below the touch threshold.
//  * Power-up settle window (first 3 s): upward tracking is faster (150 ms) so the touch-controller
//    start-up transient is absorbed instead of reading as a finger. Bounded: after 3 s only the slow
//    rate applies. (A sub-threshold press in this window is absorbed too.)
//  * Touched: baseline is frozen, except for the same fast downward re-learn (a finger can only raise
//    readings, so a large dip is baseline contamination, never a finger).
//  * Bounded hold: if a touch lasts longer than 120 s the baseline is re-learned from the current
//    readings and touched is cleared. This is the recovery path from a stuck touch caused by drift
//    accumulated during a very long hold; a finger that is genuinely still there reads as released
//    until it is lifted and placed again.
// A finger resting at power-up is therefore not reported as a touch (the device cannot tell it from
// the idle level); it becomes fully functional as soon as it is lifted once.
class TouchRing {
 public:
  static constexpr unsigned kMinSectors = 3;
  static constexpr unsigned kMaxSectors = 8;

  struct Config {
    uint8_t sectors = 8;           // 3..8
    uint16_t onPermille = 12;      // touch-down: strongest sector's rise over baseline
    uint16_t offPermille = 7;      // touch-up (clamped to <= onPermille)
    bool equalizeGains = true;     // learn per-sector sensitivity from observed peaks
  };

  struct Result {
    bool ready = false;            // false during the power-up learn window (touched is forced false)
    bool touched = false;
    bool angleValid = false;       // touched, amplitude stable (not landing / lifting), confidence ok
    uint16_t angle = 0;            // 0..4095 (1/4096 rev); holds the last valid value otherwise
    uint8_t confidence = 0;        // 0..255; low for palms / common-mode shifts / barely-there fingers
    uint16_t peakPermille = 0;     // strongest sector's rise over baseline (diagnostics)
    float angleSigma = 0.0f;       // 1-sigma noise of `angle` in 1/4096 rev, propagated from the measured
                                   // idle noise of each sector; 0 when there is no angle
  };

  TouchRing();

  // Live reconfiguration. Changing the sector count resets all learned state.
  void configure(const Config& cfg);
  void reset();

  // raw[0..sectors-1], nowUs = monotonic microseconds. Call at ~1 kHz.
  Result update(const uint16_t* raw, uint64_t nowUs);

  // Diagnostics.
  float gain(unsigned sector) const { return gain_[sector]; }
  float baseline(unsigned sector) const { return base_[sector]; }
  float smoothed(unsigned sector) const { return s_[sector]; }
  float noiseSigma(unsigned sector) const;  // measured idle noise of the conditioned reading, permille rms
  bool touched() const { return touched_; }

 private:
  void learnGains(float peak, unsigned dominant, float dt);

  uint8_t sectors_ = 0;
  float on_ = 12.0f, off_ = 7.0f;
  bool equalize_ = true;
  float sin_[kMaxSectors] = {};
  float cos_[kMaxSectors] = {};

  bool started_ = false;
  uint64_t startUs_ = 0, lastUs_ = 0, touchStartUs_ = 0;
  bool touched_ = false;
  uint8_t confirm_ = 0;
  uint16_t prev1_[kMaxSectors] = {};  // previous two raw readings, for the median
  uint16_t prev2_[kMaxSectors] = {};
  float s_[kMaxSectors] = {};      // conditioned readings
  float base_[kMaxSectors] = {};   // baselines
  float peakHold_[kMaxSectors] = {};
  float observedMs_[kMaxSectors] = {};
  float gain_[kMaxSectors] = {};
  float noiseVar_[kMaxSectors] = {};  // idle variance of the conditioned reading, permille^2
  float peakSlow_ = 0.0f;          // slow average of the strongest sector's rise, for the stability test
  uint16_t lastAngle_ = 0;
};

}  // namespace epdj
