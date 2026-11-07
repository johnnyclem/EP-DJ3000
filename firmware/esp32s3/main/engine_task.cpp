#include "engine_task.h"

#include "epdj_audio/audio_out.h"
#include "epdj_hal/time.h"
#include "epdj_link/radio.h"
#include "epdj_sd/sd_card.h"
#include "epdj_usb/usb_device.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "output_pump.h"
#include "tick_pacing.h"

namespace epdj {
namespace app {
namespace {

const char* TAG = "engine";

// Work per iteration is bounded so a flood on one input cannot starve the sensors or the other inputs.
constexpr unsigned kMaxUsbPerIteration = 32;
constexpr unsigned kMaxTrsPerIteration = 16;
constexpr unsigned kMaxLinkPerIteration = 8;
constexpr uint64_t kLedPeriodUs = 20000;
static_assert(sizeof(TickType_t) == sizeof(uint32_t), "tick_pacing.h assumes 32-bit ticks");

LoopStats g_stats;
TaskHandle_t g_taskHandle = nullptr;

// The platform side of OutputPump (output_pump.h has the delivery policy).
struct EngineIo {
  Runtime& rt;

  bool popOut(OutEvent* ev) { return rt.engine->popOut(ev); }
  bool trsReady() const { return rt.trs != nullptr; }
  void trsWrite(const uint8_t* d, size_t n) { rt.trs->write(d, n); }
  bool radioReady() const { return rt.radioOn; }
  void radioSend(const uint8_t* d, size_t n) { radio::send(d, n); }
  bool usbHostActive() const { return usb_dev::hostActive(); }
  bool usbWrite(const uint8_t pkt[4]) { return usb_dev::writePacket(pkt); }
  void countUsbStall() { g_stats.usbStalls.fetch_add(1, std::memory_order_relaxed); }
  void countUsbDrop() { g_stats.usbDrops.fetch_add(1, std::memory_order_relaxed); }
};

void engineLoop(Runtime* rt) {
  Engine& engine = *rt->engine;
  EngineIo io{*rt};
  OutputPump<EngineIo> pump(io);
  uint64_t lastLedUs = 0;
  uint64_t prevT0 = 0;
  bool encoderCapsSet = rt->as5600->present();
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    // One tick = 1 ms (CONFIG_FREERTOS_HZ = 1000): exact cadence over time, a few microseconds of wake-up jitter.
    // After an overrun, drop the lost ticks instead of running flat out to catch up (tick_pacing.h): the task
    // always sleeps, so the TinyUSB task, IDLE1 and the watchdog keep running.
    const TickType_t tickNow = xTaskGetTickCount();
    if (shouldDropLostTicks(tickNow, lastWake)) {
      lastWake = tickNow;
      g_stats.tickResyncs.fetch_add(1, std::memory_order_relaxed);
    }
    vTaskDelayUntil(&lastWake, 1);
    const uint64_t t0 = hal::nowUs();
    if (prevT0 != 0 && t0 - prevT0 > 1500) g_stats.lateWakeups.fetch_add(1, std::memory_order_relaxed);  // a tick went missing (preempted, or cache off)
    prevT0 = t0;

    // ---- 1. inputs that arrived since last time, stamped at receipt by whoever received them ----
    usb_dev::RxPacket up;
    for (unsigned i = 0; i < kMaxUsbPerIteration && usb_dev::popRx(&up); ++i) {
      engine.onUsbPacket(midi::UsbPacket::fromBytes(up.bytes), up.rxUs);
    }
    if (rt->trs != nullptr) {
      hal::TrsMidi::Chunk tc;
      for (unsigned i = 0; i < kMaxTrsPerIteration && rt->trs->pop(&tc); ++i) {
        engine.onMidiIn(Engine::MidiPort::Trs, tc.data, tc.len, tc.us);
      }
    }
    if (rt->radioOn) {
      radio::RxPacket rp;
      for (unsigned i = 0; i < kMaxLinkPerIteration && radio::pop(&rp); ++i) {
        engine.onLinkPacket(rp.data, rp.len, rp.rssi, rp.rxUs, hal::nowUs());
      }
    }

    // ---- 2. sample the hardware and run the engine ----
    // The encoder first, and every tick: jog is the latency-critical input, so it is sampled right at the timestamp and not
    // after the ADC's conversions. Without an encoder (or when a read fails) angleValid stays false and the engine reports
    // no platter motion at all; angle12 is never used then. A chip that is missing or failing is asked again every 500 ms
    // (As5600::readAngle, encoder_retry.h): in between the call returns at once without touching the bus. When it answers
    // again the engine sees angleValid go true after a gap longer than its 3 ms bridge and re-anchors on that sample, so
    // the platter's position at that moment is not motion. Only the AS5600 platter has a reason to look for an encoder
    // that is missing; any other source keeps reading one that is there, as before.
    RawInputs in;
    in.nowUs = hal::nowUs();
    const bool wantEncoder = rt->as5600->present() || rt->cfg->get(proto::ConfigKey::PlatterSource) ==
                                                          static_cast<int32_t>(proto::PlatterSource::As5600);
    if (wantEncoder) {
      uint16_t a = 0;
      in.angleValid = rt->as5600->readAngle(&a, in.nowUs);
      if (in.angleValid) in.angle12 = a;
    }
    if (!encoderCapsSet && rt->as5600->present()) {  // Info.caps says an AS5600 is there as soon as it is
      encoderCapsSet = true;
      engine.setCaps(static_cast<uint16_t>(rt->caps | proto::caps::kAs5600));
    }
    in.faderRaw = rt->adc->fader();
    in.knobRaw = rt->adc->knob();
    rt->touch->read(in.touchRaw);
    in.touchCount = static_cast<uint8_t>(rt->touch->count());
    if (sd::buttonsCaptured()) {
      in.cue = false;
      in.rev = false;
    } else {
      in.cue = rt->buttons->cue();
      in.rev = rt->buttons->rev();
    }
    in.usbMounted = usb_dev::hostActive();
    in.audioStreaming = usb_dev::audioStreaming();
    in.dacPresent = rt->dacPresent;
    engine.step(in);

    // ---- 3. everything the engine wants to send ----
    pump.drain(in.nowUs);
    if (rt->led != nullptr && in.nowUs - lastLedUs >= kLedPeriodUs) {
      lastLedUs = in.nowUs;
      rt->led->show(engine.led(), in.nowUs);
    }

    // ---- 4. book-keeping for the diagnostics task (no logging here: a console write can block for ms) ----
    const uint64_t t1 = hal::nowUs();
    const uint32_t dt = static_cast<uint32_t>(t1 - t0);
    g_stats.iterations.fetch_add(1, std::memory_order_relaxed);
    if (dt > 1000) g_stats.overruns.fetch_add(1, std::memory_order_relaxed);
    if (dt > g_stats.maxIterationUs.load(std::memory_order_relaxed)) g_stats.maxIterationUs.store(dt, std::memory_order_relaxed);
    g_stats.fader.store(in.faderRaw, std::memory_order_relaxed);
    g_stats.knob.store(in.knobRaw, std::memory_order_relaxed);
    g_stats.angle.store(in.angle12, std::memory_order_relaxed);
    g_stats.angleValid.store(in.angleValid, std::memory_order_relaxed);
    for (unsigned i = 0; i < 8; ++i) g_stats.touchRaw[i].store(in.touchRaw[i], std::memory_order_relaxed);
  }
}

void taskEntry(void* arg) {
  engineLoop(static_cast<Runtime*>(arg));
  vTaskDelete(nullptr);
}

}  // namespace

LoopStats& loopStats() { return g_stats; }

size_t engineStackHighWaterBytes() {
  return g_taskHandle != nullptr ? uxTaskGetStackHighWaterMark(g_taskHandle) * sizeof(StackType_t) : 0;
}

void startEngineTask(Runtime* rt) {
  // Core 1 with the TinyUSB task; the radio, audio and UART tasks live on core 0. Highest application priority:
  // clock placement and jog latency are what this product is.
  if (xTaskCreatePinnedToCore(taskEntry, "engine", 6144, rt, 24, &g_taskHandle, 1) != pdPASS) {
    ESP_LOGE(TAG, "could not start the engine task");
  }
}

}  // namespace app
}  // namespace epdj
