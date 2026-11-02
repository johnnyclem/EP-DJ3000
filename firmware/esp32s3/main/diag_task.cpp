#include "diag_task.h"

#include "epdj_audio/audio_out.h"
#include "epdj_link/radio.h"
#include "epdj_usb/usb_device.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <cstdio>

namespace epdj {
namespace app {
namespace {

const char* TAG = "diag";

void sensorLine(const Runtime& rt) {
  LoopStats& s = loopStats();
  char touch[8 * 7 + 1] = {};
  size_t o = 0;
  for (unsigned i = 0; i < rt.touch->count() && o + 7 < sizeof touch; ++i) {
    o += static_cast<size_t>(snprintf(touch + o, sizeof touch - o, "%u%s", static_cast<unsigned>(s.touchRaw[i].load()),
                                      i + 1 < rt.touch->count() ? " " : ""));
  }
  ESP_LOGI(TAG, "sens: fader=%u knob=%u angle=%u%s cue=%d rev=%d touch=[%s] fresh=%u%%",
           static_cast<unsigned>(s.fader.load()), static_cast<unsigned>(s.knob.load()), static_cast<unsigned>(s.angle.load()),
           rt.as5600->present() ? (s.angleValid.load() ? "" : "(bad)") : "(none)", rt.buttons->cue() ? 1 : 0,
           rt.buttons->rev() ? 1 : 0, touch, rt.touch->freshPercent());
}

void statsLines(const Runtime& rt) {
  LoopStats& s = loopStats();
  const Engine::Stats& es = rt.engine->stats();
  ESP_LOGI(TAG, "loop: %lu iterations, %lu over 1 ms (max %lu us), %lu late wake-ups; out queue %u B (peak %lu), dropped %lu",
           static_cast<unsigned long>(s.iterations.load()), static_cast<unsigned long>(s.overruns.load()),
           static_cast<unsigned long>(s.maxIterationUs.load()), static_cast<unsigned long>(s.lateWakeups.load()),
           static_cast<unsigned>(rt.engine->queuedBytes()), static_cast<unsigned long>(es.outHighWaterBytes),
           static_cast<unsigned long>(es.outDropped));
  s.maxIterationUs.store(0);

  const usb_dev::Stats us = usb_dev::stats();
  ESP_LOGI(TAG, "usb: %s%s midi rx %lu (dropped %lu) tx %lu (rejected %lu), tx stalls %lu drops %lu",
           usb_dev::mounted() ? "mounted" : "not mounted", usb_dev::suspended() ? " suspended" : "",
           static_cast<unsigned long>(us.rxPackets), static_cast<unsigned long>(us.rxDropped),
           static_cast<unsigned long>(us.txPackets), static_cast<unsigned long>(us.txRejected),
           static_cast<unsigned long>(s.usbStalls.load()), static_cast<unsigned long>(s.usbDrops.load()));

  if (audio_out::running()) {
    const audio_out::Stats as = audio_out::stats();
    ESP_LOGI(TAG, "audio: %s%s fill %lu frames, usb frames %lu, over %lu under %lu, reprimes %lu, drift %.1f ppm, fb %.4f smp/frame, slips -%lu +%lu",
             as.streaming ? "streaming" : "idle", as.streaming ? (as.primed ? " running" : " priming") : "",
             static_cast<unsigned long>(as.fillFrames), static_cast<unsigned long>(us.audioFrames),
             static_cast<unsigned long>(as.overrunFrames), static_cast<unsigned long>(as.underrunFrames),
             static_cast<unsigned long>(as.reprimes), static_cast<double>(as.driftPpm),
             static_cast<double>(as.feedbackQ16) / 65536.0, static_cast<unsigned long>(as.driftDropped),
             static_cast<unsigned long>(as.driftDuplicated));
  }
  if (rt.radioOn) {
    const radio::Stats rs = radio::stats();
    const link::Link& lk = rt.engine->link();
    ESP_LOGI(TAG, "link: %s, peers %u, radio rx %lu (dropped %lu) tx %lu (queue full %lu, failed %lu), core rx ok %lu corrupt %lu",
             lk.linkUp() ? (lk.bridging() ? "up, bridging" : "up") : "searching", static_cast<unsigned>(lk.peers().count()),
             static_cast<unsigned long>(rs.rxPackets), static_cast<unsigned long>(rs.rxDropped),
             static_cast<unsigned long>(rs.txPackets), static_cast<unsigned long>(rs.txQueueFull),
             static_cast<unsigned long>(rs.txFailed), static_cast<unsigned long>(lk.stats().rxPackets),
             static_cast<unsigned long>(lk.stats().rxCorrupt));
  }
  if (rt.trs != nullptr) {
    ESP_LOGI(TAG, "trs: rx overflows %lu, tx dropped %lu", static_cast<unsigned long>(rt.trs->rxOverflows()),
             static_cast<unsigned long>(rt.trs->txDropped()));
  }
  ESP_LOGI(TAG, "heap: %u free, %u lowest ever; engine task stack: %u bytes never used",
           static_cast<unsigned>(esp_get_free_heap_size()), static_cast<unsigned>(esp_get_minimum_free_heap_size()),
           static_cast<unsigned>(engineStackHighWaterBytes()));
}

void diagLoop(const Runtime* rt) {
  unsigned seconds = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    ++seconds;
#if CONFIG_EPDJ_LOG_SENSORS
    sensorLine(*rt);
#endif
#if CONFIG_EPDJ_LOG_PERIOD_S > 0
    if (seconds % CONFIG_EPDJ_LOG_PERIOD_S == 0) statsLines(*rt);
#else
    (void)seconds;
#endif
  }
}

void taskEntry(void* arg) {
  diagLoop(static_cast<const Runtime*>(arg));
  vTaskDelete(nullptr);
}

}  // namespace

void startDiagTask(const Runtime* rt) {
  if (xTaskCreatePinnedToCore(taskEntry, "diag", 4096, const_cast<Runtime*>(rt), 3, nullptr, 0) != pdPASS) {
    ESP_LOGE(TAG, "could not start the diagnostics task");
  }
}

}  // namespace app
}  // namespace epdj
