// epdj_hal/sensor_budget.h: the AS5600 magnet status decode and the split of the engine task's 400 us sensor time
// between the ADC and the I2C read. No ESP-IDF, no chip.
#include <cstdint>

#include "epdj_hal/sensor_budget.h"
#include "test_harness.h"

using namespace epdj::hal::sensor_budget;

EPDJ_TEST(magnet_status_register_decodes_the_three_bits) {
  CHECK(classifyMagnet(0x20) == Magnet::Ok);          // MD only: detected, gain in range
  CHECK(classifyMagnet(0x00) == Magnet::Missing);     // no bits: nothing in range
  CHECK(classifyMagnet(0x10) == Magnet::TooWeak);     // ML
  CHECK(classifyMagnet(0x30) == Magnet::TooWeak);     // MD + ML: detected, but the AGC is out of gain
  CHECK(classifyMagnet(0x08) == Magnet::TooStrong);   // MH
  CHECK(classifyMagnet(0x28) == Magnet::TooStrong);   // MD + MH: detected, but the AGC is at its minimum gain
  CHECK(classifyMagnet(0xC7) == Magnet::Missing);     // the other bits mean nothing here
  CHECK(classifyMagnet(0xE0 | 0x20) == Magnet::Ok);
  for (Magnet m : {Magnet::Unknown, Magnet::Missing, Magnet::Ok, Magnet::TooWeak, Magnet::TooStrong}) CHECK(magnetText(m)[0] != '?');
}

EPDJ_TEST(adc_oversampling_gives_way_to_the_i2c_read) {
  bool squeezed = true;
  // No encoder: the old rule, 400 us over two channels.
  CHECK_EQ(adcOversample(0, 20, 8, &squeezed), 8);  // 400 / 40 = 10, capped at the Kconfig limit
  CHECK(!squeezed);
  CHECK_EQ(adcOversample(0, 30, 8, &squeezed), 6);  // 400 / 60
  CHECK(squeezed);
  // With a 150 us encoder read the ADC gets 250 us.
  CHECK_EQ(adcOversample(150, 20, 8, &squeezed), 6);  // 250 / 40
  CHECK(squeezed);
  CHECK_EQ(adcOversample(150, 30, 8, &squeezed), 4);  // 250 / 60
  CHECK(squeezed);
  CHECK_EQ(adcOversample(150, 10, 8, &squeezed), 8);  // 250 / 20 = 12, capped
  CHECK(!squeezed);
  // The budget gone, or more than gone: one conversion per channel, flagged.
  CHECK_EQ(adcOversample(400, 20, 8, &squeezed), 1);
  CHECK(squeezed);
  CHECK_EQ(adcOversample(900, 20, 8, &squeezed), 1);
  CHECK(squeezed);
  // Degenerate inputs.
  CHECK_EQ(adcOversample(150, 0, 8, &squeezed), 8);  // an unmeasurable conversion: trust the Kconfig limit
  CHECK(!squeezed);
  CHECK_EQ(adcOversample(0, 20, 1, &squeezed), 1);   // a limit of 1 is never "squeezed"
  CHECK(!squeezed);
}

EPDJ_TEST(oversampling_never_overspends_unless_one_conversion_already_does) {
  for (unsigned i2c = 0; i2c <= 600; i2c += 25) {
    for (unsigned conv = 5; conv <= 120; conv += 5) {
      for (unsigned max = 1; max <= 32; max *= 2) {
        bool squeezed = false;
        const unsigned n = adcOversample(i2c, conv, max, &squeezed);
        CHECK(n >= 1 && n <= max);
        if (n > 1) CHECK(sensorUs(i2c, conv, n) <= kTotalUs);  // more than the minimum only if it fits
        if (squeezed) CHECK(sensorUs(i2c, conv, n + 1) > kTotalUs || n == 1);  // and one more would not have fitted
      }
    }
  }
  CHECK_EQ(sensorUs(150, 30, 4), 390);
}

EPDJ_TEST(an_encoder_that_may_turn_up_later_keeps_its_share_of_the_budget_reserved) {
  using namespace epdj::hal::sensor_budget;
  CHECK_EQ(i2cReserveUs(true, true, 130), 130u);    // answered at boot: the measured read
  CHECK_EQ(i2cReserveUs(true, true, 0), 0u);        // answered but not measurable: as before
  CHECK_EQ(i2cReserveUs(true, false, 0), kAbsentReadReserveUs);  // not there, but asked again while running: a reserve
  CHECK_EQ(i2cReserveUs(false, false, 0), 0u);      // not the AS5600 platter: nothing to reserve
  CHECK_EQ(i2cReserveUs(false, true, 150), 150u);   // a chip that is there is read whatever the platter source
  // The point: size the ADC for the reserve, boot without the chip, find it later with a read no slower than the reserve:
  // the sensors still fit in kTotalUs whenever more than the minimum oversampling was chosen.
  for (unsigned conv = 5; conv <= 120; conv += 5) {
    bool squeezed = false;
    const unsigned n = adcOversample(i2cReserveUs(true, false, 0), conv, 8, &squeezed);
    for (unsigned realRead = 0; realRead <= kAbsentReadReserveUs; realRead += 10) {
      if (n > 1) CHECK(sensorUs(realRead, conv, n) <= kTotalUs);
    }
  }
  // Without the reserve the same late chip would have overspent: the ADC fills the whole budget and the read comes on top.
  bool squeezed = false;
  const unsigned greedy = adcOversample(0, 20, 8, &squeezed);
  CHECK(sensorUs(150, 20, greedy) > kTotalUs);
}
