// Left pitch, knob, right pitch and crossfader: ADC1 one-shot reads, oversampled.
#pragma once
#include <cstdint>

#include "epdj_hal/sensor_budget.h"
#include "esp_adc/adc_oneshot.h"

namespace epdj {
namespace hal {

class AdcInputs {
 public:
  static constexpr unsigned kChannels = 4;

  // Times a few conversions and sets the oversampling: EPDJ_ADC_OVERSAMPLE, or fewer if all four channels together
  // plus the encoder read (`i2cReadUs`, 0 without an encoder) would take more than kBudgetUs of every millisecond.
  // Each conversion runs with interrupts off, on the core that also serves USB; the I2C read does not, but it is the
  // same engine task's time (sensor_budget.h).
  bool begin(unsigned i2cReadUs = 0);
  static constexpr unsigned kBudgetUs = sensor_budget::kTotalUs;

  // 12-bit, mean of oversample() conversions. A failed conversion keeps the last value.
  uint16_t fader();
  uint16_t knob();
  uint16_t pitchRight();
  uint16_t crossfader();
  unsigned oversample() const { return oversample_; }
  unsigned conversionUs() const { return conversionUs_; }  // measured at begin()

 private:
  uint16_t sample(adc_channel_t ch, uint16_t* last);

  adc_oneshot_unit_handle_t unit_ = nullptr;
  adc_channel_t chFader_ = ADC_CHANNEL_0;
  adc_channel_t chKnob_ = ADC_CHANNEL_1;
  adc_channel_t chPitchRight_ = ADC_CHANNEL_4;
  adc_channel_t chCross_ = ADC_CHANNEL_5;
  uint16_t lastFader_ = 0, lastKnob_ = 0, lastPitchRight_ = 0, lastCross_ = 0;
  unsigned oversample_ = 1;
  unsigned conversionUs_ = 0;
};

}  // namespace hal
}  // namespace epdj
