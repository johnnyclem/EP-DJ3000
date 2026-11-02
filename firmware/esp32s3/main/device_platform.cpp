#include "device_platform.h"

#include "epdj_audio/audio_out.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

namespace epdj {
namespace app {
namespace {

const char* TAG = "platform";
constexpr uint64_t kRestartDelayUs = 200000;

void restartNow(void* arg) {
  if (arg != nullptr) {
    // ROM bootloader on the next reset: the same trick esptool's "usb reset" uses, and the only route into
    // download mode on a board whose BOOT button is inside the enclosure.
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  }
  esp_restart();
}

void restartSoon(bool bootloader) {
  static esp_timer_handle_t timer = nullptr;
  if (timer != nullptr) return;  // already on its way
  esp_timer_create_args_t args = {};
  args.callback = restartNow;
  args.arg = bootloader ? reinterpret_cast<void*>(1) : nullptr;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "restart";
  if (esp_timer_create(&args, &timer) != ESP_OK || esp_timer_start_once(timer, kRestartDelayUs) != ESP_OK) {
    ESP_LOGE(TAG, "could not schedule the restart; restarting immediately");
    restartNow(args.arg);
  }
}

}  // namespace

void applyFirstBootDefaults(Config* cfg) {
#if CONFIG_EPDJ_PLATTER_AS5600
  cfg->set(proto::ConfigKey::PlatterSource, static_cast<int32_t>(proto::PlatterSource::As5600));
#elif CONFIG_EPDJ_PLATTER_HOLD_ONLY
  cfg->set(proto::ConfigKey::PlatterSource, static_cast<int32_t>(proto::PlatterSource::TouchHoldOnly));
#else
  cfg->set(proto::ConfigKey::PlatterSource, static_cast<int32_t>(proto::PlatterSource::TouchRing));
#endif
#if CONFIG_EPDJ_FIRST_BOOT_DECK_B
  cfg->set(proto::ConfigKey::DeckId, static_cast<int32_t>(proto::Deck::B));
#else
  cfg->set(proto::ConfigKey::DeckId, static_cast<int32_t>(proto::Deck::A));
#endif
}

void DevicePlatform::reboot(proto::RebootMode mode) {
  ESP_LOGW(TAG, "reboot requested (%s)", mode == proto::RebootMode::Bootloader ? "ROM bootloader" : "normal");
  restartSoon(mode == proto::RebootMode::Bootloader);
}

void DevicePlatform::setDeck(proto::Deck deck) {
  // Change the stored configuration, not the live one: a live tweak the user never saved must not be saved
  // as a side effect of swapping decks.
  Config stored;
  if (loadConfig(store_, &stored) == LoadStatus::Empty) applyFirstBootDefaults(&stored);
  stored.set(proto::ConfigKey::DeckId, static_cast<int32_t>(deck));
  if (!epdj::saveConfig(store_, stored)) ESP_LOGE(TAG, "could not store the new deck");
  ESP_LOGW(TAG, "deck %c stored, restarting to re-enumerate", deck == proto::Deck::A ? 'A' : 'B');
  restartSoon(false);
}

bool DevicePlatform::testTone(bool on, uint16_t freqHz, uint8_t attenDb) {
  return audio_out::testTone(on, freqHz, attenDb);
}

bool DevicePlatform::saveConfig(const Config& cfg) { return epdj::saveConfig(store_, cfg); }

}  // namespace app
}  // namespace epdj
