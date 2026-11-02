// Fader and knob: ADC1 one-shot reads, oversampled.
#pragma once
#include <cstdint>

#include "esp_adc/adc_oneshot.h"

namespace epdj {
namespace hal {

class AdcInputs {
 public:
  // Times a few conversions and sets the oversampling: EPDJ_ADC_OVERSAMPLE, or fewer if that would take
  // more than kBudgetUs of every millisecond for the two channels together (each conversion runs with
  // interrupts off, on the core that also serves USB).
  bool begin();
  static constexpr unsigned kBudgetUs = 400;

  // 12-bit, mean of oversample() conversions. A failed conversion keeps the last value.
  uint16_t fader();
  uint16_t knob();
  unsigned oversample() const { return oversample_; }
  unsigned conversionUs() const { return conversionUs_; }  // measured at begin()

 private:
  uint16_t sample(adc_channel_t ch, uint16_t* last);

  adc_oneshot_unit_handle_t unit_ = nullptr;
  adc_channel_t chFader_ = ADC_CHANNEL_0;
  adc_channel_t chKnob_ = ADC_CHANNEL_1;
  uint16_t lastFader_ = 0, lastKnob_ = 0;
  unsigned oversample_ = 1;
  unsigned conversionUs_ = 0;
};

}  // namespace hal
}  // namespace epdj
