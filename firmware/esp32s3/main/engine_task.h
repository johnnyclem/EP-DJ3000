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
#include "epdj_hal/twin_io.h"

namespace epdj {
namespace app {

struct Runtime {
  Config* cfg = nullptr;
  Engine* engine = nullptr;
  hal::TouchPads* touch = nullptr;
  hal::AdcInputs* adc = nullptr;
  hal::ButtonPins* buttons = nullptr;
  hal::As5600* as5600 = nullptr;  // present() says whether a chip answered
  hal::TwinIo* twin = nullptr;    // non-null only on the twin wide deck
  hal::TrsMidi* trs = nullptr;    // null when compiled out
  hal::StatusLed* led = nullptr;  // null when the LED failed to initialise
  bool radioOn = false;
  uint8_t linkGroupDefault = 0;  // Engine::Identity::linkGroup: the build-time rig id, used while the config key is 0
  bool dacPresent = false;
  uint16_t caps = 0;  // Engine::Identity::caps as built at boot; the AS5600 bit is added to it if the encoder turns up later
};

// The radio group the engine uses right now: the linkGroup config key when non-zero, else the build default
// (docs/LINK.md section 11). Same rule as the engine's, for logging only.
inline uint8_t effectiveLinkGroup(const Runtime& rt) {
  const int32_t g = rt.cfg->get(proto::ConfigKey::LinkGroup);
  return static_cast<uint8_t>(g != 0 ? g : rt.linkGroupDefault);
}

// What the engine task last saw and how well it keeps time. Written by the engine task only; the diagnostics
// task reads it without locking, so a log line can show a value from one iteration and another from the next.
struct LoopStats {
  std::atomic<uint32_t> iterations{0};
  std::atomic<uint32_t> overruns{0};         // iterations that took longer than 1 ms
  std::atomic<uint32_t> maxIterationUs{0};   // since the diagnostics task last cleared it
  std::atomic<uint32_t> lateWakeups{0};      // iterations that began more than 1.5 ms after the previous one
  std::atomic<uint32_t> tickResyncs{0};      // times the schedule was this far behind (tick_pacing.h) and ticks were dropped
  std::atomic<uint32_t> usbStalls{0};        // iterations an output event was held back because the USB FIFO was full
  std::atomic<uint32_t> usbDrops{0};         // output events given up on (no host, or it stopped reading)
  std::atomic<uint16_t> fader{0}, knob{0}, angle{0};
  std::atomic<uint16_t> touchRaw[8] = {};
  std::atomic<bool> angleValid{false};
};

void startEngineTask(Runtime* rt);
LoopStats& loopStats();
size_t engineStackHighWaterBytes();  // smallest free stack the task has seen

}  // namespace app
}  // namespace epdj
