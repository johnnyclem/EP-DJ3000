/*
 * EP-DJ3000 firmware for the ESP32-S3: boots the hardware, then hands everything to the portable engine
 * (firmware/core) which runs on one task.
 *
 * THREADING MODEL
 * ---------------
 *   task            core  prio  what it does
 *   engine           1     24   every 1 ms: drain input queues -> sample ADC/touch/AS5600/buttons -> Engine::step()
 *                               -> deliver Engine output (USB, TRS, radio) -> LED. The ONLY caller of the core.
 *   tinyusb          1     22   esp_tinyusb's tud_task(): enumeration, control requests (USB audio volume/mute),
 *                               MIDI endpoint servicing. Its callbacks only copy into queues or atomics.
 *   audio            0     22   one 48-frame block per ms: ring -> drift compensator -> volume/tone -> I2S. Paced by
 *                               i2s_channel_write blocking on the DMA ring; computes the USB audio feedback value.
 *   link             0     21   paces ESP-NOW sends (one in flight, waits for the send callback).
 *   trs_rx           0     20   UART1 events -> time-stamped byte chunks.
 *   diag             0      3   the only task that prints in steady state.
 *   (IDF) Wi-Fi 23, esp_timer 22 (restart timer), main task (this function) exits after boot.
 *   interrupts: USB OTG (on the TinyUSB task's core), I2S DMA, UART1, RMT.
 *
 *   What crosses a task boundary, and how
 *     input to the engine   every producer time-stamps at RECEIPT (esp_timer) and pushes to its own FreeRTOS queue;
 *                           the engine task pops a bounded number per millisecond.
 *                             USB MIDI   TinyUSB task, tud_midi_rx_cb        -> usb_dev::popRx
 *                             TRS MIDI   trs_rx task, UART event             -> TrsMidi::pop
 *                             radio      Wi-Fi task, ESP-NOW recv callback   -> radio::pop
 *     output of the engine  USB: tud_midi_n_packet_write straight from the engine task (TinyUSB serialises endpoint
 *                           access with a priority-inheriting mutex; the pattern its own FreeRTOS examples use). A full
 *                           FIFO keeps ONE event in hand and retries next tick; after 100 ms it is dropped, and from then
 *                           on USB is "stalled": later events are dropped on their first refused packet instead of each
 *                           waiting 100 ms, so a host that does not read the port never slows TRS or the radio (output_pump.h).
 *                           TRS: UART TX ring, never blocks (drops when full). Radio: radio::send -> tx queue -> link task.
 *     USB audio             USB interrupt -> lock-free SPSC ring (no float, no locks) -> audio task. Feedback goes back
 *                           as one 32-bit store (tud_audio_n_fb_set). Float is used only in tasks: the S3 does not allow
 *                           FPU instructions in interrupts.
 *     engine <-> audio      atomics only (test tone request, volume, streaming flag).
 *
 *   Pacing: the engine task sleeps until the next 1 ms tick every iteration; after an overrun of more than two ticks
 *   the lost ticks are dropped rather than caught up (tick_pacing.h), so it can never run without sleeping and starve the
 *   TinyUSB task (same core, lower priority), IDLE1 or the watchdog. ADC conversions run with interrupts off; AdcInputs
 *   times them at boot and keeps the engine's share of each millisecond under 400 us.
 *
 *   Priority inversion: the engine task is the highest application priority but waits on nothing a lower task can hold
 *   except TinyUSB's endpoint mutex (inheritance, critical sections of microseconds). Every queue operation it makes is
 *   non-blocking, the UART write checks for room first, and it does not log in normal operation (only when a reboot or
 *   deck swap is requested). The audio task blocks only on the I2S DMA.
 *
 *   Known stall: a flash write (NVS save on SAVE_CONFIG / SET_DECK) disables the flash cache for its duration. The USB
 *   interrupt handler is not in IRAM, so USB audio can glitch and one or more engine ticks run late while a save is in
 *   progress. Saves are user-initiated; nothing in normal playing writes flash.
 */

#include <cstring>
#include <new>

#include "device_platform.h"
#include "diag_task.h"
#include "engine_task.h"
#include "epdj/config.h"
#include "epdj/engine.h"
#include "epdj/protocol.h"
#include "epdj/version.h"
#include "epdj_audio/audio_out.h"
#include "epdj_hal/dac_detect.h"
#include "epdj_hal/i2c_bus.h"
#include "epdj_hal/nvs_store.h"
#include "epdj_link/radio.h"
#include "epdj_usb/usb_device.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_random.h"
#include "sdkconfig.h"

using namespace epdj;

namespace {

const char* TAG = "main";

hal::NvsConfigStore g_store;
Config g_cfg;
hal::TouchPads g_touch;
hal::AdcInputs g_adc;
hal::ButtonPins g_buttons;
hal::I2cBus g_i2c;
hal::As5600 g_as5600;
hal::TrsMidi g_trs;
hal::StatusLed g_led;
app::DevicePlatform g_platform(g_store);
app::Runtime g_rt;
alignas(Engine) uint8_t g_engineStorage[sizeof(Engine)];  // constructed once the identity is known; never freed

const char* loadStatusText(LoadStatus s) {
  switch (s) {
    case LoadStatus::Ok: return "ok";
    case LoadStatus::Repaired: return "ok, some out-of-range values reset";
    case LoadStatus::Empty: return "nothing saved";
    case LoadStatus::BadLength: return "bad length";
    case LoadStatus::BadMagic: return "bad magic";
    case LoadStatus::BadVersion: return "written by another firmware version";
    case LoadStatus::BadCrc: return "bad CRC";
  }
  return "?";
}

}  // namespace

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "EP-DJ3000 core %d.%d.%d, protocol v%u, ESP-IDF %s", kVersionMajor, kVersionMinor, kVersionPatch,
           static_cast<unsigned>(proto::kProtocolVersion), esp_get_idf_version());

  // ---- configuration ----
  if (!g_store.begin()) ESP_LOGE(TAG, "NVS unavailable: running on defaults, nothing can be saved");
  const LoadStatus ls = loadConfig(g_store, &g_cfg);
  if (ls == LoadStatus::Empty) app::applyFirstBootDefaults(&g_cfg);
  ESP_LOGI(TAG, "config: %s; deck %c, platter source %d", loadStatusText(ls), g_cfg.deck() == proto::Deck::A ? 'A' : 'B',
           static_cast<int>(g_cfg.get(proto::ConfigKey::PlatterSource)));

  uint8_t mac[6];
  radio::readStationMac(mac);

  // ---- sensors ----
  g_buttons.begin();
  if (!g_adc.begin()) ESP_LOGE(TAG, "ADC failed: fader and knob will read 0");
  if (CONFIG_EPDJ_TOUCH_PAD_COUNT > 0 && !g_touch.begin(CONFIG_EPDJ_TOUCH_PAD_COUNT)) {
    ESP_LOGE(TAG, "touch pads failed: no platter input");
  }
#if CONFIG_EPDJ_AS5600_ENABLE || CONFIG_EPDJ_DAC_PROBE_I2C
  g_i2c.begin();
#endif
#if CONFIG_EPDJ_AS5600_ENABLE
  if (g_i2c.ok() && !g_as5600.begin(g_i2c)) ESP_LOGI(TAG, "no AS5600 on I2C");
#endif
  if (static_cast<proto::PlatterSource>(g_cfg.get(proto::ConfigKey::PlatterSource)) == proto::PlatterSource::As5600 &&
      !g_as5600.present()) {
    ESP_LOGW(TAG, "platterSource is AS5600 but no encoder answered: the platter will not turn");
  }

  // ---- DAC ----
  // dacMode decides at boot because the USB descriptors cannot change afterwards. Only AUTO looks at the strap
  // (16 ms of samples, so noise on an open wire cannot enable USB audio); ON and OFF are not second-guessed.
  const auto dacMode = static_cast<proto::DacMode>(g_cfg.get(proto::ConfigKey::DacMode));
  bool dacPresent = false;
  if (dacMode == proto::DacMode::On) {
    dacPresent = true;
    ESP_LOGI(TAG, "dac: mode ON (forced) -> present");
  } else if (dacMode == proto::DacMode::Off) {
    ESP_LOGI(TAG, "dac: mode OFF -> absent (MIDI only)");
  } else {
    const hal::DacDetection dd = hal::detectDac(&g_i2c);
    dacPresent = dd.present();
    ESP_LOGI(TAG, "dac: mode AUTO, strap low %u/%u samples%s, codec %s -> %s", static_cast<unsigned>(dd.strapLow),
             static_cast<unsigned>(hal::DacDetection::kStrapSamples), dd.strapUnstable() ? " (UNSTABLE: wire or contact?)" : "",
             dd.codec ? "found" : "none", dacPresent ? "present" : "absent (MIDI only)");
  }
  if (dacPresent && !audio_out::start(usb_dev::audioFeedbackSink())) {
    ESP_LOGE(TAG, "I2S output failed to start: continuing without audio");
    dacPresent = false;
  }

  // ---- USB ----
  usb_dev::Params up;
  up.deck = static_cast<uint8_t>(g_cfg.deck());
  up.audio = dacPresent;
  std::memcpy(up.mac, mac, sizeof mac);
  const bool usbOk = usb_dev::start(up);
  if (!usbOk) ESP_LOGE(TAG, "USB failed to start");

  // ---- radio, TRS MIDI, LED ----
  bool radioOn = false;
#if CONFIG_EPDJ_LINK
  radioOn = radio::start(CONFIG_EPDJ_LINK_CHANNEL);
#endif
  bool trsOn = false;
#if CONFIG_EPDJ_TRS_MIDI
  trsOn = g_trs.begin();
#endif
  const bool ledOn = g_led.begin();

  // ---- engine ----
  Engine::Identity id;
  id.hwRev = CONFIG_EPDJ_HW_REV;
  id.serial = (static_cast<uint32_t>(mac[2]) << 24) | (static_cast<uint32_t>(mac[3]) << 16) |
              (static_cast<uint32_t>(mac[4]) << 8) | mac[5];
  if (g_as5600.present()) id.caps |= proto::caps::kAs5600;
  if (g_touch.count() >= 3) id.caps |= proto::caps::kTouchRing;
  if (trsOn) id.caps |= proto::caps::kTrsMidi;
  if (radioOn) id.caps |= proto::caps::kLink;
  if (usbOk && usb_dev::audioEnabled()) id.caps |= proto::caps::kUsbAudio;
  if (ledOn) id.caps |= proto::caps::kStatusLed;
  std::memcpy(id.mac.b, mac, sizeof mac);
  id.epoch = static_cast<uint16_t>(esp_random() & 0xFFFFu);
  if (id.epoch == 0) id.epoch = 1;
#ifdef CONFIG_EPDJ_LINK_GROUP
  id.linkGroup = CONFIG_EPDJ_LINK_GROUP;  // build-time default: the engine uses it while the linkGroup config key is 0
#endif

  Engine* engine = new (g_engineStorage) Engine(g_cfg, g_platform, id);

  g_rt.cfg = &g_cfg;
  g_rt.engine = engine;
  g_rt.touch = &g_touch;
  g_rt.adc = &g_adc;
  g_rt.buttons = &g_buttons;
  g_rt.as5600 = &g_as5600;
  g_rt.trs = trsOn ? &g_trs : nullptr;
  g_rt.led = ledOn ? &g_led : nullptr;
  g_rt.radioOn = radioOn;
  g_rt.linkGroupDefault = id.linkGroup;
  g_rt.dacPresent = dacPresent;

  if (radioOn) ESP_LOGI(TAG, "link group %u (%s)", static_cast<unsigned>(app::effectiveLinkGroup(g_rt)),
                        g_cfg.get(proto::ConfigKey::LinkGroup) != 0 ? "linkGroup config key" : "build default");

  app::startEngineTask(&g_rt);
  app::startDiagTask(&g_rt);
  ESP_LOGI(TAG, "ready: caps 0x%04X, serial %08lX, MIDI channel %u", static_cast<unsigned>(id.caps),
           static_cast<unsigned long>(id.serial), static_cast<unsigned>(g_cfg.effectiveChannel()) + 1u);
}
