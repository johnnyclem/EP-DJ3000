// Monotonic microsecond clock. The engine takes the 64-bit value; the 32-bit one is the same clock
// truncated, for code that only needs differences (the engine and the radio link already handle its wrap).
#pragma once
#include <cstdint>

#include "esp_timer.h"

namespace epdj {
namespace hal {

inline uint64_t nowUs() { return static_cast<uint64_t>(esp_timer_get_time()); }
inline uint32_t micros32() { return static_cast<uint32_t>(esp_timer_get_time()); }

}  // namespace hal
}  // namespace epdj
