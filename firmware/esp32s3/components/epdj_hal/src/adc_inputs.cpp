#include "epdj_hal/adc_inputs.h"

#include "driver/gpio.h"
#include "epdj_hal/board_pins.h"
#include "epdj_hal/time.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace epdj {
namespace hal {
namespace {
const char* TAG = "adc";
}

bool AdcInputs::begin(unsigned i2cReadUs) {
  adc_oneshot_unit_init_cfg_t unitCfg = {};
  unitCfg.unit_id = ADC_UNIT_1;
  unitCfg.ulp_mode = ADC_ULP_MODE_DISABLE;
  if (adc_oneshot_new_unit(&unitCfg, &unit_) != ESP_OK) {
    ESP_LOGE(TAG, "adc_oneshot_new_unit failed");
    return false;
  }
  adc_unit_t units[kChannels] = {ADC_UNIT_1, ADC_UNIT_1, ADC_UNIT_1, ADC_UNIT_1};
  const int gpios[kChannels] = {pins::kFader, pins::kKnob, pins::kPitchRight, pins::kCrossfader};
  adc_channel_t* channels[kChannels] = {&chFader_, &chKnob_, &chPitchRight_, &chCross_};
  for (unsigned i = 0; i < kChannels; ++i) {
    if (adc_oneshot_io_to_channel(gpios[i], &units[i], channels[i]) != ESP_OK || units[i] != ADC_UNIT_1) {
      ESP_LOGE(TAG, "GPIO%d is not on ADC1", gpios[i]);
      return false;
    }
  }
  adc_oneshot_chan_cfg_t chCfg = {};
  chCfg.atten = ADC_ATTEN_DB_12;  // full 0..3.3 V span for a pot between 3V3 and GND
  chCfg.bitwidth = ADC_BITWIDTH_12;
  for (unsigned i = 0; i < kChannels; ++i) {
    if (adc_oneshot_config_channel(unit_, *channels[i], &chCfg) != ESP_OK) {
      ESP_LOGE(TAG, "adc_oneshot_config_channel failed");
      return false;
    }
  }
  // An open right-pitch or crossfader pin would wander and spam MIDI. The internal pulldown holds it near 0; a
  // 10 k pot between 3V3 and GND swamps that pulldown. The left pitch and the knob keep no pulldown, so a board
  // that was already calibrated on those two is unchanged.
  gpio_pullup_dis(static_cast<gpio_num_t>(pins::kPitchRight));
  gpio_pulldown_en(static_cast<gpio_num_t>(pins::kPitchRight));
  gpio_pullup_dis(static_cast<gpio_num_t>(pins::kCrossfader));
  gpio_pulldown_en(static_cast<gpio_num_t>(pins::kCrossfader));
  // Time the conversions instead of assuming a cost. The engine task makes kChannels * oversample of them
  // every millisecond.
  constexpr int kProbe = 32;
  oversample_ = 1;
  const uint64_t t0 = nowUs();
  for (int i = 0; i < kProbe; ++i) {
    int v = 0;
    adc_oneshot_read(unit_, *channels[static_cast<unsigned>(i) % kChannels], &v);
  }
  const uint64_t elapsed = nowUs() - t0;
  conversionUs_ = static_cast<unsigned>((elapsed + kProbe / 2) / kProbe);
  bool squeezed = false;
  oversample_ = sensor_budget::adcOversample(i2cReadUs, conversionUs_, CONFIG_EPDJ_ADC_OVERSAMPLE, &squeezed, kChannels);
  lastFader_ = sample(chFader_, &lastFader_);
  lastKnob_ = sample(chKnob_, &lastKnob_);
  lastPitchRight_ = sample(chPitchRight_, &lastPitchRight_);
  lastCross_ = sample(chCross_, &lastCross_);
  const unsigned sensorUs = sensor_budget::sensorUs(i2cReadUs, conversionUs_, oversample_, kChannels);
  ESP_LOGI(TAG,
           "fader GPIO%d raw %u, knob GPIO%d raw %u, right pitch GPIO%d raw %u, crossfader GPIO%d raw %u; "
           "%u us per conversion, x%u oversampling; sensor time per ms: ADC %u us + encoder read %u us = %u us of %u",
           pins::kFader, lastFader_, pins::kKnob, lastKnob_, pins::kPitchRight, lastPitchRight_, pins::kCrossfader,
           lastCross_, conversionUs_, oversample_, kChannels * oversample_ * conversionUs_, i2cReadUs, sensorUs,
           kBudgetUs);
  if (squeezed) {
    ESP_LOGW(TAG, "oversampling lowered from %d to %u to keep the sensors within %u us per ms%s", CONFIG_EPDJ_ADC_OVERSAMPLE,
             oversample_, kBudgetUs, i2cReadUs > 0 ? " (the encoder read comes off the top)" : " (conversions are slow)");
  }
  if (sensorUs > kBudgetUs) {
    ESP_LOGW(TAG, "even one conversion per channel plus the encoder read takes %u us, over the %u us budget: the 1 ms tick has less room",
             sensorUs, kBudgetUs);
  }
  return true;
}

uint16_t AdcInputs::sample(adc_channel_t ch, uint16_t* last) {
  uint32_t sum = 0;
  int good = 0;
  for (unsigned i = 0; i < oversample_; ++i) {
    int v = 0;
    if (adc_oneshot_read(unit_, ch, &v) == ESP_OK) {
      sum += static_cast<uint32_t>(v);
      ++good;
    }
  }
  if (good == 0) return *last;
  *last = static_cast<uint16_t>((sum + static_cast<uint32_t>(good) / 2) / static_cast<uint32_t>(good));
  return *last;
}

uint16_t AdcInputs::fader() { return unit_ ? sample(chFader_, &lastFader_) : 0; }
uint16_t AdcInputs::knob() { return unit_ ? sample(chKnob_, &lastKnob_) : 0; }
uint16_t AdcInputs::pitchRight() { return unit_ ? sample(chPitchRight_, &lastPitchRight_) : 0; }
uint16_t AdcInputs::crossfader() { return unit_ ? sample(chCross_, &lastCross_) : 0; }

}  // namespace hal
}  // namespace epdj
