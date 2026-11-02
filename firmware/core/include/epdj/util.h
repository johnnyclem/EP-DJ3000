// Small helpers shared by the sensor-processing modules. Header-only, no heap.
#pragma once
#include <cstdint>

namespace epdj {

template <typename T>
constexpr T clamp(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

constexpr int32_t satAdd32(int32_t a, int32_t b) {
  const int64_t s = static_cast<int64_t>(a) + b;
  return s > INT32_MAX ? INT32_MAX : (s < INT32_MIN ? INT32_MIN : static_cast<int32_t>(s));
}

// Angles are 12-bit (1/4096 revolution), the native unit of the AS5600 and of TouchRing.
constexpr int32_t kAngleUnits = 4096;

// Shortest signed distance from `from` to `to`, in [-2048, 2047]. Inputs may carry stray high bits.
constexpr int32_t angleDiff4096(uint16_t from, uint16_t to) {
  const uint32_t d = (static_cast<uint32_t>(to) - static_cast<uint32_t>(from)) & 4095u;
  return static_cast<int32_t>((d + 2048u) & 4095u) - 2048;
}

// Seconds between two microsecond timestamps, clamped to [100 us, 20 ms] so a stalled tick or a repeated
// timestamp cannot blow up a filter. The clamp is done in integers: 32-bit int -> float is a single
// FPU instruction on the ESP32-S3, 64-bit is a library call.
inline float dtSeconds(uint64_t nowUs, uint64_t lastUs) {
  const uint64_t d = nowUs > lastUs ? nowUs - lastUs : 0;
  const uint32_t c = d < 100 ? 100u : (d > 20000 ? 20000u : static_cast<uint32_t>(d));
  return static_cast<float>(c) * 1e-6f;
}

// First-order low-pass coefficient for a sample interval dtSec and time constant tauSec.
inline float lowPassAlpha(float dtSec, float tauSec) { return dtSec / (tauSec + dtSec); }

// Speed-adaptive smoothing factor (the One-Euro idea, driven by the innovation x - y instead of a
// filtered derivative so it reacts on the very first sample of a step): `alphaRest` while the
// innovation is inside the noise band (<= lo), 1 (no smoothing, zero latency) once it is clearly real
// motion (>= hi), linear in between. Inputs and thresholds share one unit.
inline float adaptiveAlpha(float absInnovation, float alphaRest, float lo, float hi) {
  if (absInnovation <= lo) return alphaRest;
  if (absInnovation >= hi) return 1.0f;
  return alphaRest + (1.0f - alphaRest) * ((absInnovation - lo) / (hi - lo));
}

}  // namespace epdj
