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

// What the re-probe found since the last look, once per event (the engine task counts, this task talks: it must not log).
// The state behind a count (present, magnet, CONF) is written before the count moves.
void encoderEvents(const Runtime& rt) {
  using hal::sensor_budget::Magnet;
  static uint32_t seenLosses = 0, seenRecoveries = 0;  // this task only
  const uint32_t losses = rt.as5600->losses();
  const uint32_t recoveries = rt.as5600->recoveries();
  if (losses != seenLosses) {
    seenLosses = losses;
    ESP_LOGW(TAG, "as5600: encoder stopped answering at 0x%02X (two failed reads in a row): jog is silent until it does; asking again every 500 ms",
             hal::As5600::kAddress);
  }
  if (recoveries != seenRecoveries) {
    seenRecoveries = recoveries;
    char conf[24];
    if (rt.as5600->confKnown()) std::snprintf(conf, sizeof conf, "conf=0x%04X", static_cast<unsigned>(rt.as5600->conf()));
    else std::snprintf(conf, sizeof conf, "conf=?");
    if (rt.as5600->appearedAfterBoot() && recoveries == 1) {
      ESP_LOGI(TAG, "as5600: encoder appeared at 0x%02X after boot (magnet %s, %s, angle reg 0x%02X): jog is live, no reboot needed",
               hal::As5600::kAddress, hal::sensor_budget::magnetText(rt.as5600->magnet()), conf,
               static_cast<unsigned>(hal::As5600::angleRegister()));
      if (rt.as5600->magnet() != Magnet::Ok) {
        ESP_LOGW(TAG, "AS5600 magnet %s (STATUS 0x%02X)", hal::sensor_budget::magnetText(rt.as5600->magnet()), rt.as5600->status());
      }
    } else {
      ESP_LOGI(TAG, "as5600: encoder answering again at 0x%02X (%s): jog resumed from the platter's present position, no jump",
               hal::As5600::kAddress, conf);
    }
    if (!rt.as5600->confOk()) ESP_LOGW(TAG, "as5600: CONF %s", rt.as5600->confKnown() ? "read back differently from what was written" : "could not be written or read back");
  }
}

void sensorLine(const Runtime& rt) {
  LoopStats& s = loopStats();
  char touch[8 * 7 + 1] = {};
  size_t o = 0;
  for (unsigned i = 0; i < rt.touch->count() && o + 7 < sizeof touch; ++i) {
    o += static_cast<size_t>(snprintf(touch + o, sizeof touch - o, "%u%s", static_cast<unsigned>(s.touchRaw[i].load()),
                                      i + 1 < rt.touch->count() ? " " : ""));
  }
  ESP_LOGI(TAG, "sens: fader=%u knob=%u angle=%u%s cue=%d rev=%d touch=[%s]",
           static_cast<unsigned>(s.fader.load()), static_cast<unsigned>(s.knob.load()), static_cast<unsigned>(s.angle.load()),
           rt.as5600->present() ? (s.angleValid.load() ? "" : "(bad)") : "(none)", rt.buttons->cue() ? 1 : 0,
           rt.buttons->rev() ? 1 : 0, touch);
  static uint32_t reportedFailures = 0;  // this task only
  const uint32_t failures = rt.as5600->failures();
  if (failures != reportedFailures) {
    reportedFailures = failures;
    ESP_LOGW(TAG, "as5600: %lu failed I2C reads since boot (loose wire or a bus fault: each costs the 1 kHz loop up to 2 ms)",
             static_cast<unsigned long>(failures));
  }
}

void statsLines(const Runtime& rt) {
  LoopStats& s = loopStats();
  const Engine::Stats& es = rt.engine->stats();
  ESP_LOGI(TAG, "loop: %lu iterations, %lu over 1 ms (max %lu us), %lu late wake-ups, %lu resyncs; out queue %u B (peak %lu), dropped %lu",
           static_cast<unsigned long>(s.iterations.load()), static_cast<unsigned long>(s.overruns.load()),
           static_cast<unsigned long>(s.maxIterationUs.load()), static_cast<unsigned long>(s.lateWakeups.load()),
           static_cast<unsigned long>(s.tickResyncs.load()),
           static_cast<unsigned>(rt.engine->queuedBytes()), static_cast<unsigned long>(es.outHighWaterBytes),
           static_cast<unsigned long>(es.outDropped));
  s.maxIterationUs.store(0);

  const usb_dev::Stats us = usb_dev::stats();
  ESP_LOGI(TAG, "usb: %s%s midi rx %lu (dropped %lu) tx %lu (rejected %lu), tx stalls %lu drops %lu",
           usb_dev::mounted() ? "mounted" : "not mounted",
           usb_dev::suspended() ? (usb_dev::hostActive() ? " suspended" : " suspended (no host)") : "",
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
    ESP_LOGI(TAG, "link: group %u %s, peers %u, radio rx %lu (dropped %lu) tx %lu (queue full %lu, failed %lu), core rx ok %lu corrupt %lu other-group %lu",
             static_cast<unsigned>(effectiveLinkGroup(rt)),
             lk.linkUp() ? (lk.bridging() ? "up, bridging" : "up") : "searching", static_cast<unsigned>(lk.peers().count()),
             static_cast<unsigned long>(rs.rxPackets), static_cast<unsigned long>(rs.rxDropped),
             static_cast<unsigned long>(rs.txPackets), static_cast<unsigned long>(rs.txQueueFull),
             static_cast<unsigned long>(rs.txFailed), static_cast<unsigned long>(lk.stats().rxPackets),
             static_cast<unsigned long>(lk.stats().rxCorrupt), static_cast<unsigned long>(lk.stats().rxForeign));
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
    encoderEvents(*rt);  // events, not statistics: logged whatever the periodic lines are set to
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
