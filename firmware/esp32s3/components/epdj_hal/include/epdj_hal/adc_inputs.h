// Fader and knob: ADC1 one-shot reads, oversampled.
#pragma once
#include <cstdint>

#include "esp_adc/adc_oneshot.h"

namespace epdj {
namespace hal {

class AdcInputs {
 public:
  bool begin();
  // 12-bit, mean of EPDJ_ADC_OVERSAMPLE conversions (about 20 us each). A failed conversion keeps the last value.
  uint16_t fader();
  uint16_t knob();

 private:
  uint16_t sample(adc_channel_t ch, uint16_t* last);

  adc_oneshot_unit_handle_t unit_ = nullptr;
  adc_channel_t chFader_ = ADC_CHANNEL_0;
  adc_channel_t chKnob_ = ADC_CHANNEL_1;
  uint16_t lastFader_ = 0, lastKnob_ = 0;
};

}  // namespace hal
}  // namespace epdj
