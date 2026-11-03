#include "epdj_hal/adc_inputs.h"

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
  adc_unit_t u1 = ADC_UNIT_1, u2 = ADC_UNIT_1;
  if (adc_oneshot_io_to_channel(pins::kFader, &u1, &chFader_) != ESP_OK || u1 != ADC_UNIT_1 ||
      adc_oneshot_io_to_channel(pins::kKnob, &u2, &chKnob_) != ESP_OK || u2 != ADC_UNIT_1) {
    ESP_LOGE(TAG, "fader/knob pins are not on ADC1");
    return false;
  }
  adc_oneshot_chan_cfg_t chCfg = {};
  chCfg.atten = ADC_ATTEN_DB_12;  // full 0..3.3 V span for a pot between 3V3 and GND
  chCfg.bitwidth = ADC_BITWIDTH_12;
  if (adc_oneshot_config_channel(unit_, chFader_, &chCfg) != ESP_OK ||
      adc_oneshot_config_channel(unit_, chKnob_, &chCfg) != ESP_OK) {
    ESP_LOGE(TAG, "adc_oneshot_config_channel failed");
    return false;
  }
  // Time the conversions (both channels, alternating) instead of assuming a cost: the engine task makes
  // 2 * oversample of them every millisecond.
  constexpr int kProbe = 32;
  oversample_ = 1;
  const uint64_t t0 = nowUs();
  for (int i = 0; i < kProbe; ++i) {
    int v = 0;
    adc_oneshot_read(unit_, (i & 1) ? chKnob_ : chFader_, &v);
  }
  const uint64_t elapsed = nowUs() - t0;
  conversionUs_ = static_cast<unsigned>((elapsed + kProbe / 2) / kProbe);
  bool squeezed = false;
  oversample_ = sensor_budget::adcOversample(i2cReadUs, conversionUs_, CONFIG_EPDJ_ADC_OVERSAMPLE, &squeezed);
  lastFader_ = sample(chFader_, &lastFader_);
  lastKnob_ = sample(chKnob_, &lastKnob_);
  const unsigned sensorUs = sensor_budget::sensorUs(i2cReadUs, conversionUs_, oversample_);
  ESP_LOGI(TAG, "fader GPIO%d raw %u, knob GPIO%d raw %u; %u us per conversion, x%u oversampling; sensor time per ms: ADC %u us + AS5600 read %u us = %u us of %u",
           pins::kFader, lastFader_, pins::kKnob, lastKnob_, conversionUs_, oversample_, 2 * oversample_ * conversionUs_, i2cReadUs,
           sensorUs, kBudgetUs);
  if (squeezed) {
    ESP_LOGW(TAG, "oversampling lowered from %d to %u to keep the sensors within %u us per ms%s", CONFIG_EPDJ_ADC_OVERSAMPLE,
             oversample_, kBudgetUs, i2cReadUs > 0 ? " (the AS5600 read comes off the top)" : " (conversions are slow)");
  }
  if (sensorUs > kBudgetUs) {
    ESP_LOGW(TAG, "even one conversion per channel plus the AS5600 read takes %u us, over the %u us budget: the 1 ms tick has less room", sensorUs,
             kBudgetUs);
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

}  // namespace hal
}  // namespace epdj
