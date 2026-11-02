// Pacing helper for the 1 kHz engine task. Free of FreeRTOS so tools/host_checks can test it.
#pragma once
#include <cstdint>

namespace epdj {
namespace app {

// vTaskDelayUntil() never sleeps while its wake-up time is already in the past, and it advances that time by one
// tick per call, so a task that is more than a tick behind runs flat out until it has caught up, and one that is
// always behind (every iteration over 1 ms) never sleeps again and starves everything below it. A lag of one or
// two ticks is caught up with back-to-back iterations; beyond that the lost ticks are dropped. Unsigned
// subtraction, so a tick counter that wrapped between the two readings still gives the right lag.
constexpr uint32_t kMaxCatchUpTicks = 2;

inline bool shouldDropLostTicks(uint32_t nowTick, uint32_t lastWakeTick) {
  return static_cast<uint32_t>(nowTick - lastWakeTick) > kMaxCatchUpTicks;
}

}  // namespace app
}  // namespace epdj
