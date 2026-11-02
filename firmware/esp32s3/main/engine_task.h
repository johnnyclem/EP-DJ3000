// The 1 kHz engine task: the only task that calls into the portable core.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "epdj/config.h"
#include "epdj/engine.h"
#include "epdj_hal/adc_inputs.h"
#include "epdj_hal/as5600.h"
#include "epdj_hal/buttons.h"
#include "epdj_hal/status_led.h"
#include "epdj_hal/touch_pads.h"
#include "epdj_hal/trs_midi.h"

namespace epdj {
namespace app {

struct Runtime {
  Config* cfg = nullptr;
  Engine* engine = nullptr;
  hal::TouchPads* touch = nullptr;
  hal::AdcInputs* adc = nullptr;
  hal::ButtonPins* buttons = nullptr;
  hal::As5600* as5600 = nullptr;  // present() says whether a chip answered
  hal::TrsMidi* trs = nullptr;    // null when compiled out
  hal::StatusLed* led = nullptr;  // null when the LED failed to initialise
  bool radioOn = false;
  bool dacPresent = false;
};

// What the engine task last saw and how well it keeps time. Written by the engine task only; the diagnostics
// task reads it without locking, so a log line can show a value from one iteration and another from the next.
struct LoopStats {
  std::atomic<uint32_t> iterations{0};
  std::atomic<uint32_t> overruns{0};         // iterations that took longer than 1 ms
  std::atomic<uint32_t> maxIterationUs{0};   // since the diagnostics task last cleared it
  std::atomic<uint32_t> lateWakeups{0};      // iterations that began more than 1.5 ms after the previous one
  std::atomic<uint32_t> usbStalls{0};        // output events held back because the USB FIFO was full
  std::atomic<uint32_t> usbDrops{0};         // output events given up on (host stopped reading)
  std::atomic<uint16_t> fader{0}, knob{0}, angle{0};
  std::atomic<uint16_t> touchRaw[8] = {};
  std::atomic<bool> angleValid{false};
};

void startEngineTask(Runtime* rt);
LoopStats& loopStats();
size_t engineStackHighWaterBytes();  // smallest free stack the task has seen

}  // namespace app
}  // namespace epdj
