#include "epdj/audio_dsp.h"

#include <cmath>

namespace epdj {
namespace audio {

namespace {

// ---- sine table, evaluated by the compiler ----

struct QuarterSine {
  int16_t v[258];  // sin(i * pi/2 / 256) in Q15 for i = 0..256, v[257] repeats v[256]
};

constexpr double kPi = 3.14159265358979323846;

// Taylor series, |x| <= pi/2: the truncation error after x^19 is ~1e-12.
constexpr double sinTaylor(double x) {
  const double x2 = x * x;
  double term = x, sum = x;
  for (int i = 1; i <= 9; ++i) {
    term *= -x2 / static_cast<double>((2 * i) * (2 * i + 1));
    sum += term;
  }
  return sum;
}

constexpr QuarterSine makeQuarterSine() {
  QuarterSine t{};
  for (int i = 0; i <= 256; ++i) {
    const double s = sinTaylor(kPi / 2.0 * i / 256.0);
    t.v[i] = static_cast<int16_t>(static_cast<int32_t>(s * 32767.0 + 0.5));
  }
  t.v[257] = t.v[256];
  return t;
}

constexpr QuarterSine kSine = makeQuarterSine();

// sin(2*pi*phase/2^32) in Q15.
int32_t sineQ15(uint32_t phase) {
  const uint32_t quadrant = phase >> 30;
  uint32_t pos = phase & 0x3FFFFFFFu;  // 8 bits of table index, 22 bits of fraction
  if (quadrant & 1u) pos = (1u << 30) - pos;
  const uint32_t idx = pos >> 22;
  const int32_t frac = static_cast<int32_t>(pos & 0x3FFFFFu);
  const int32_t a = kSine.v[idx], b = kSine.v[idx + 1];
  const int32_t v = a + (((b - a) * frac) >> 22);  // b >= a, so the shift never sees a negative
  return quadrant >= 2 ? -v : v;
}

int32_t clampI32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Q15 smoothstep of x in 0..32768: 3x^2 - 2x^3.
int32_t smoothstepQ15(int32_t x) {
  const int32_t t = (x * x) >> 15;
  return (t * (49152 - x)) >> 14;
}

constexpr int32_t kGainStepQ15 = 32768 / static_cast<int32_t>(ToneGenerator::kRampFrames) + 1;

inline int16_t sat16(int64_t v) {
  return static_cast<int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

}  // namespace

// ---- ToneGenerator ------------------------------------------------------------------------------

int32_t ToneGenerator::peakFor(uint8_t attenuationDb) {
  const float g = std::pow(10.0f, -static_cast<float>(attenuationDb) * 0.05f);
  return clampI32(static_cast<int32_t>(g * 32767.0f + 0.5f), 0, 32767);
}

void ToneGenerator::start(uint16_t freqHz, uint8_t attenuationDb) {
  uint32_t f = freqHz;
  const uint32_t maxF = rate_ / 2 - 1;
  f = f < 1 ? 1 : (f > maxF ? maxF : f);
  inc_ = static_cast<uint32_t>(((static_cast<uint64_t>(f) << 32) + rate_ / 2) / rate_);
  gainTarget_ = peakFor(attenuationDb);
  if (!active()) {
    phase_ = 0;
    gain_ = gainTarget_;
  }
  running_ = true;
}

void ToneGenerator::stop() { running_ = false; }

void ToneGenerator::render(int16_t* out, size_t frames) {
  for (size_t i = 0; i < frames; ++i) {
    if (running_) {
      if (level_ < kRampFrames) ++level_;
    } else if (level_ > 0) {
      --level_;
    }
    if (level_ == 0) {
      phase_ = 0;
      out[2 * i] = out[2 * i + 1] = 0;
      continue;
    }
    if (gain_ < gainTarget_) {
      gain_ = clampI32(gain_ + kGainStepQ15, 0, gainTarget_);
    } else if (gain_ > gainTarget_) {
      gain_ = clampI32(gain_ - kGainStepQ15, gainTarget_, 32767);
    }
    const int32_t env = smoothstepQ15(static_cast<int32_t>((level_ * 32768u) / kRampFrames));
    const int32_t s1 = (sineQ15(phase_) * gain_ + 16384) >> 15;
    const int32_t s2 = (s1 * env + 16384) >> 15;
    out[2 * i] = out[2 * i + 1] = static_cast<int16_t>(s2);
    phase_ += inc_;
  }
}

// ---- VolumeProcessor ----------------------------------------------------------------------------

int32_t VolumeProcessor::gainForVolume(int16_t v) {
  if (v == kSilence) return 0;
  const float db = static_cast<float>(v) * (1.0f / 256.0f);
  if (db < -100.0f) return 0;
  const float cdb = db > 24.0f ? 24.0f : db;
  const float g = std::pow(10.0f, cdb * 0.05f);
  return static_cast<int32_t>(g * 65536.0f + 0.5f);
}

void VolumeProcessor::setVolume(int16_t volume256Db) {
  volume_ = volume256Db;
  target_ = muted_ ? 0 : gainForVolume(volume_);
}

void VolumeProcessor::setMute(bool muted) {
  muted_ = muted;
  target_ = muted_ ? 0 : gainForVolume(volume_);
}

void VolumeProcessor::process(int16_t* io, size_t frames) {
  constexpr int32_t kStep = 65536 / static_cast<int32_t>(kRampFrames) + 1;
  for (size_t i = 0; i < frames; ++i) {
    if (gain_ < target_) {
      gain_ = gain_ + kStep > target_ ? target_ : gain_ + kStep;
    } else if (gain_ > target_) {
      gain_ = gain_ - kStep < target_ ? target_ : gain_ - kStep;
    }
    if (gain_ == 65536) continue;
    if (gain_ == 0) {
      io[2 * i] = io[2 * i + 1] = 0;
      continue;
    }
    io[2 * i] = sat16((static_cast<int64_t>(io[2 * i]) * gain_ + 32768) >> 16);
    io[2 * i + 1] = sat16((static_cast<int64_t>(io[2 * i + 1]) * gain_ + 32768) >> 16);
  }
}

}  // namespace audio
}  // namespace epdj
