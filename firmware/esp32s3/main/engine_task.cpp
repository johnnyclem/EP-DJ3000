#include "engine_task.h"

#include "epdj_audio/audio_out.h"
#include "epdj_hal/time.h"
#include "epdj_link/radio.h"
#include "epdj_usb/usb_device.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace epdj {
namespace app {
namespace {

const char* TAG = "engine";

// Work per iteration is bounded so a flood on one input cannot starve the sensors or the other inputs.
constexpr unsigned kMaxUsbPerIteration = 32;
constexpr unsigned kMaxTrsPerIteration = 16;
constexpr unsigned kMaxLinkPerIteration = 8;
constexpr unsigned kMaxOutPerIteration = 64;
constexpr uint64_t kUsbGiveUpUs = 100000;  // a host that stops reading must not wedge the TRS and radio output behind it
constexpr uint64_t kLedPeriodUs = 20000;
constexpr size_t kMaxUsbPackets = (OutEvent::kMaxBytes + 2) / 3;

LoopStats g_stats;
TaskHandle_t g_taskHandle = nullptr;

// One output event the engine produced, possibly half delivered: the USB endpoint FIFO can be full, in which
// case the event stays "in hand" and is retried on the next iteration (README: "keep one event in hand").
struct InHand {
  bool valid = false;
  OutEvent ev;
  bool otherDone = false;  // TRS and radio parts are sent exactly once
  size_t usbSent = 0;
  uint64_t sinceUs = 0;
};

// Delivers `h`. True = finished (or abandoned), false = USB is busy, try again next iteration.
bool deliver(Runtime& rt, InHand& h, uint64_t now) {
  OutEvent& ev = h.ev;
  if (!h.otherDone) {
    h.otherDone = true;
    if ((ev.portMask & proto::clockPorts::kTrs) != 0 && rt.trs != nullptr) rt.trs->write(ev.data, ev.len);
    if (ev.kind == OutEvent::Kind::Link && (ev.portMask & proto::clockPorts::kLink) != 0 && rt.radioOn) {
      radio::send(ev.data, ev.len);  // a refused send is just packet loss, which the link layer tolerates
    }
  }
  if ((ev.portMask & proto::clockPorts::kUsb) == 0 ||
      (ev.kind != OutEvent::Kind::Message && ev.kind != OutEvent::Kind::SysEx)) {
    return true;
  }
  midi::UsbPacket pkts[kMaxUsbPackets];
  const size_t n = encodeUsb(ev, pkts, kMaxUsbPackets);
  while (h.usbSent < n) {
    uint8_t raw[4];
    pkts[h.usbSent].toBytes(raw);
    if (!usb_dev::writePacket(raw)) {
      if (!usb_dev::mounted() || now - h.sinceUs > kUsbGiveUpUs) {
        g_stats.usbDrops.fetch_add(1, std::memory_order_relaxed);
        return true;  // no host, or it stopped reading: drop the rest of this event
      }
      g_stats.usbStalls.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    ++h.usbSent;
  }
  return true;
}

void drainOutputs(Runtime& rt, InHand& h, uint64_t now) {
  for (unsigned i = 0; i < kMaxOutPerIteration; ++i) {
    if (!h.valid) {
      if (!rt.engine->popOut(&h.ev)) return;
      h.valid = true;
      h.otherDone = false;
      h.usbSent = 0;
      h.sinceUs = now;
    }
    if (!deliver(rt, h, now)) return;
    h.valid = false;
  }
}

void engineLoop(Runtime* rt) {
  Engine& engine = *rt->engine;
  InHand inHand;
  uint64_t lastLedUs = 0;
  uint64_t prevT0 = 0;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    // One tick = 1 ms (CONFIG_FREERTOS_HZ = 1000): exact cadence over time, a few microseconds of wake-up jitter.
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
    RawInputs in;
    in.nowUs = hal::nowUs();
    in.faderRaw = rt->adc->fader();
    in.knobRaw = rt->adc->knob();
    rt->touch->read(in.touchRaw);
    in.touchCount = static_cast<uint8_t>(rt->touch->count());
    if (rt->as5600->present()) {
      uint16_t a = 0;
      in.angleValid = rt->as5600->readAngle(&a, in.nowUs);
      if (in.angleValid) in.angle12 = a;
    }
    in.cue = rt->buttons->cue();
    in.rev = rt->buttons->rev();
    in.usbMounted = usb_dev::mounted();
    in.audioStreaming = usb_dev::audioStreaming();
    in.dacPresent = rt->dacPresent;
    engine.step(in);

    // ---- 3. everything the engine wants to send ----
    drainOutputs(*rt, inHand, in.nowUs);
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
