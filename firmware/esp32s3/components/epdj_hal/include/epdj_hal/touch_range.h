// Choosing how long the touch sensor measures each pad. Header-only and free of ESP-IDF so tools/host_checks
// tests it.
//
// The S3 measures its pads one after another, and the raw count is the number of RTC_FAST clock cycles the
// charge/discharge cycles of one pad took, so a full scan lasts   scan = fixed + perCycle * cycles   where the
// fixed part is the per-pad power-up wait and sleep, and perCycle grows with the pads' capacitance. Neither is
// known in advance (nominal clock rates, pad and wire capacitance), so the scan is measured at two settings.
#pragma once
#include <cstdint>

namespace epdj {
namespace hal {
namespace touch_range {

struct Probe {
  uint32_t cycles = 0;  // charge/discharge cycles per pad
  uint32_t scanUs = 0;  // measured duration of one full scan; 0 = could not be measured
};

// The cycle count whose scan should take `targetUs`, from two probes at different settings, limited to
// [minCycles, maxCycles]. 0 if the probes cannot be used (a missing measurement, or a longer setting that did not
// scan more slowly). *clamped says the limits, not the target, decided the result.
inline uint32_t cyclesForScan(const Probe& lo, const Probe& hi, uint32_t targetUs, uint32_t minCycles, uint32_t maxCycles,
                              bool* clamped) {
  *clamped = false;
  if (lo.scanUs == 0 || hi.scanUs == 0 || hi.cycles <= lo.cycles || hi.scanUs <= lo.scanUs || minCycles > maxCycles) return 0;
  const int64_t dCycles = static_cast<int64_t>(hi.cycles) - lo.cycles;
  const int64_t dUs = static_cast<int64_t>(hi.scanUs) - lo.scanUs;
  // scan(c) = fixed + dUs/dCycles * c, with `fixed` held at 0 or above (measurement noise can push the fit negative).
  int64_t fixedUs = static_cast<int64_t>(hi.scanUs) - dUs * hi.cycles / dCycles;
  if (fixedUs < 0) fixedUs = 0;
  const int64_t room = static_cast<int64_t>(targetUs) - fixedUs;
  int64_t cycles = room > 0 ? room * dCycles / dUs : 0;
  if (cycles < minCycles) {
    cycles = minCycles;
    *clamped = true;
  }
  if (cycles > maxCycles) {
    cycles = maxCycles;
    *clamped = true;
  }
  return static_cast<uint32_t>(cycles);
}

}  // namespace touch_range
}  // namespace hal
}  // namespace epdj
