// Pure arithmetic of the sensor front end: how the AS5600 magnet status register is read, and how the engine task's
// per-millisecond sensor time is split between the ADC and the I2C read. Header-only and free of ESP-IDF so that
// tools/host_checks tests it.
#pragma once
#include <cstdint>

namespace epdj {
namespace hal {
namespace sensor_budget {

// ---- AS5600 STATUS register (0x0B) --------------------------------------------------------------------------------
// MD: a magnet was detected. ML: the AGC ran out of gain, the magnet is too weak. MH: the AGC is at minimum gain, the
// magnet is too strong. (Datasheet bit meanings, from memory: check them against the part you hold.)
constexpr uint8_t kStatusMd = 0x20;
constexpr uint8_t kStatusMl = 0x10;
constexpr uint8_t kStatusMh = 0x08;

enum class Magnet : uint8_t {
  Unknown,    // the register could not be read
  Missing,    // none of the bits: no magnet in range
  Ok,         // detected, gain in range
  TooWeak,    // too far, too small, or the wrong way round: move it closer
  TooStrong,  // too close: move it away
};

// A magnet can be "detected" and still be too strong or too weak for the AGC, so the overflow bits decide first.
inline Magnet classifyMagnet(uint8_t status) {
  if ((status & kStatusMh) != 0) return Magnet::TooStrong;
  if ((status & kStatusMl) != 0) return Magnet::TooWeak;
  if ((status & kStatusMd) != 0) return Magnet::Ok;
  return Magnet::Missing;
}

inline const char* magnetText(Magnet m) {
  switch (m) {
    case Magnet::Unknown: return "unknown (status register unreadable)";
    case Magnet::Missing: return "not detected";
    case Magnet::Ok: return "detected";
    case Magnet::TooWeak: return "too weak";
    case Magnet::TooStrong: return "too strong";
  }
  return "?";
}

// ---- time budget -----------------------------------------------------------------------------------------------------
// Every millisecond the engine task samples the slide pots and the knob (ADC conversions, each with interrupts off,
// on the core that also serves USB) and, in the default build, reads the platter encoder over I2C (interrupt-driven,
// the task sleeps on a semaphore meanwhile). The two together are held to kTotalUs of each millisecond so the engine
// step and the output still fit with room to spare; the I2C read comes off the top and the ADC gets what is left.
// `channels` is how many ADC pins are converted each sample: 2 in the arithmetic the host tests grew up with, 4 on
// the Twin (left pitch, knob, right pitch, crossfader).
constexpr unsigned kTotalUs = 400;

// Conversions of each ADC channel per sample: as many as fit after the I2C read, at most `maxOversample`, at least 1.
// *squeezed is set when that is fewer than maxOversample (or the budget is already spent).
inline unsigned adcOversample(unsigned i2cReadUs, unsigned conversionUs, unsigned maxOversample, bool* squeezed,
                              unsigned channels = 2) {
  if (channels < 1) channels = 1;
  const unsigned left = kTotalUs > i2cReadUs ? kTotalUs - i2cReadUs : 0;
  unsigned n = conversionUs > 0 ? left / (channels * conversionUs) : maxOversample;
  if (n > maxOversample) n = maxOversample;
  if (n < 1) n = 1;
  *squeezed = n < maxOversample;
  return n;
}

// The I2C read time the ADC's share is computed against. The measured read when the encoder answered at boot. When it
// did not but may still turn up (the runtime re-probe, encoder_retry.h), a reserve instead: the ADC oversampling is
// fixed at boot, so finding the encoder later must not push the two past kTotalUs. About 150 to 200 us is what a 400 kHz
// register read costs with the IDF driver (docs/BRINGUP.md); a chip that turns out slower than the reserve shows up in
// the diagnostics (as5600.cpp) rather than silently.
constexpr unsigned kAbsentReadReserveUs = 200;
inline unsigned i2cReserveUs(bool encoderExpected, bool answered, unsigned measuredUs) {
  if (answered) return measuredUs;
  return encoderExpected ? kAbsentReadReserveUs : 0;
}

// Microseconds the sensors take in one engine tick with this oversampling. May exceed kTotalUs when even one conversion
// per channel does not fit; callers report that.
inline unsigned sensorUs(unsigned i2cReadUs, unsigned conversionUs, unsigned oversample, unsigned channels = 2) {
  if (channels < 1) channels = 1;
  return i2cReadUs + channels * oversample * conversionUs;
}

}  // namespace sensor_budget
}  // namespace hal
}  // namespace epdj
