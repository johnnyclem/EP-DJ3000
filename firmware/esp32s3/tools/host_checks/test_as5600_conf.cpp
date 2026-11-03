// epdj_hal/as5600_regs.h: the AS5600 register map, the CONF word built from named fields, and the filtered/raw choice.
// No ESP-IDF, no chip. The word the original Field Jog firmware writes is pinned here, and the field positions are checked
// against an independent byte-level model of Rob Tillaart's AS5600 library (the arithmetic of its setPowerMode /
// setHysteresis / setSlowFilter / setFastFilter / setWatchDog, which work on the two CONF bytes separately).
#include <cstdint>

#include "epdj_hal/as5600_regs.h"
#include "test_harness.h"

using namespace epdj::hal::as5600;

namespace {

// Tillaart AS5600.cpp, byte level: register 0x07 holds SF (bits 1:0), FTH (4:2) and WD (5); register 0x08 holds PM (1:0),
// HYST (3:2), OUTS (5:4) and PWMF (7:6). The 16-bit CONF on the wire is register 0x07 first.
struct LibraryModel {
  uint8_t reg07 = 0, reg08 = 0;
  void setSlowFilter(uint8_t m) { reg07 = static_cast<uint8_t>((reg07 & ~0x03) | m); }
  void setFastFilter(uint8_t m) { reg07 = static_cast<uint8_t>((reg07 & ~0x1C) | (m << 2)); }
  void setWatchDog(uint8_t m) { reg07 = static_cast<uint8_t>((reg07 & ~0x20) | (m << 5)); }
  void setPowerMode(uint8_t m) { reg08 = static_cast<uint8_t>((reg08 & ~0x03) | m); }
  void setHysteresis(uint8_t m) { reg08 = static_cast<uint8_t>((reg08 & ~0x0C) | (m << 2)); }
  uint16_t word() const { return static_cast<uint16_t>((reg07 << 8) | reg08); }
};

}  // namespace

EPDJ_TEST(the_original_authors_configuration_is_0x1C00) {
  // Watchdog off, hysteresis 0, slow-filter setting 0 (16x), fast-filter threshold setting 7 (10 LSB), power mode nominal.
  CHECK_EQ(kScratchConfWord, 0x1C00);
  CHECK_EQ(confWord(kScratchConf), 0x1C00);
  CHECK_EQ(confWord({PowerMode::Nominal, Hysteresis::Off, SlowFilter::X16, FastFilterThreshold::Lsb10, false}), (7u << 10));
  CHECK(kScratchConf.watchdog == false);
  CHECK(kScratchConf.hysteresis == Hysteresis::Off);
  CHECK(kScratchConf.slow == SlowFilter::X16);
  CHECK_EQ(static_cast<unsigned>(kScratchConf.fast), 7u);
  // On the wire: register 0x07 = 0x1C, register 0x08 = 0x00.
  CHECK_EQ(kScratchConfWord >> 8, 0x1C);
  CHECK_EQ(kScratchConfWord & 0xFF, 0x00);
}

EPDJ_TEST(each_field_lands_in_its_own_bits) {
  const Conf zero = {PowerMode::Nominal, Hysteresis::Off, SlowFilter::X16, FastFilterThreshold::SlowOnly, false};
  CHECK_EQ(confWord(zero), 0x0000);
  Conf c = zero;
  c.power = PowerMode::Low3;
  CHECK_EQ(confWord(c), 0x0003);
  c = zero;
  c.hysteresis = Hysteresis::Lsb3;
  CHECK_EQ(confWord(c), 0x000C);
  c = zero;
  c.slow = SlowFilter::X2;
  CHECK_EQ(confWord(c), 0x0300);
  c = zero;
  c.fast = FastFilterThreshold::Lsb6;
  CHECK_EQ(confWord(c), 0x0400);
  c.fast = FastFilterThreshold::Lsb10;
  CHECK_EQ(confWord(c), 0x1C00);
  c = zero;
  c.watchdog = true;
  CHECK_EQ(confWord(c), 0x2000);
  // Everything at once; OUTS (5:4), PWMF (7:6) and the unused 15:14 are never touched.
  const Conf all = {PowerMode::Low3, Hysteresis::Lsb3, SlowFilter::X2, FastFilterThreshold::Lsb10, true};
  CHECK_EQ(confWord(all), 0x3F0F);
  CHECK_EQ(confWord(all) & ~kConfMask, 0);
  CHECK_EQ(confWord(all) & 0x00F0, 0);
}

EPDJ_TEST(the_fast_filter_codes_are_the_datasheets_out_of_order_table) {
  // 000 slow only, 001 6, 010 7, 011 9, 100 18, 101 21, 110 24, 111 10 LSB: the code 7 is NOT the largest threshold.
  CHECK_EQ(static_cast<unsigned>(FastFilterThreshold::SlowOnly), 0u);
  CHECK_EQ(static_cast<unsigned>(FastFilterThreshold::Lsb6), 1u);
  CHECK_EQ(static_cast<unsigned>(FastFilterThreshold::Lsb7), 2u);
  CHECK_EQ(static_cast<unsigned>(FastFilterThreshold::Lsb9), 3u);
  CHECK_EQ(static_cast<unsigned>(FastFilterThreshold::Lsb18), 4u);
  CHECK_EQ(static_cast<unsigned>(FastFilterThreshold::Lsb21), 5u);
  CHECK_EQ(static_cast<unsigned>(FastFilterThreshold::Lsb24), 6u);
  CHECK_EQ(static_cast<unsigned>(FastFilterThreshold::Lsb10), 7u);
  CHECK_EQ(static_cast<unsigned>(SlowFilter::X16), 0u);
  CHECK_EQ(static_cast<unsigned>(SlowFilter::X8), 1u);
  CHECK_EQ(static_cast<unsigned>(SlowFilter::X4), 2u);
  CHECK_EQ(static_cast<unsigned>(SlowFilter::X2), 3u);
}

EPDJ_TEST(every_combination_matches_the_arduino_library_byte_for_byte) {
  int n = 0;
  for (unsigned pm = 0; pm < 4; ++pm) {
    for (unsigned hy = 0; hy < 4; ++hy) {
      for (unsigned sf = 0; sf < 4; ++sf) {
        for (unsigned fth = 0; fth < 8; ++fth) {
          for (unsigned wd = 0; wd < 2; ++wd) {
            LibraryModel lib;
            lib.setPowerMode(static_cast<uint8_t>(pm));
            lib.setHysteresis(static_cast<uint8_t>(hy));
            lib.setSlowFilter(static_cast<uint8_t>(sf));
            lib.setFastFilter(static_cast<uint8_t>(fth));
            lib.setWatchDog(static_cast<uint8_t>(wd));
            const Conf c = {static_cast<PowerMode>(pm), static_cast<Hysteresis>(hy), static_cast<SlowFilter>(sf),
                            static_cast<FastFilterThreshold>(fth), wd != 0};
            CHECK_EQ(confWord(c), lib.word());
            ++n;
          }
        }
      }
    }
  }
  CHECK_EQ(n, 4 * 4 * 4 * 8 * 2);
  // and the pinned configuration is what the library's setters would build
  LibraryModel lib;
  lib.setWatchDog(0);
  lib.setHysteresis(0);
  lib.setSlowFilter(0);
  lib.setFastFilter(7);
  CHECK_EQ(lib.word(), kScratchConfWord);
}

EPDJ_TEST(read_back_comparison_ignores_the_unused_bits_and_nothing_else) {
  CHECK(confMatches(0x1C00, 0x1C00));
  CHECK(confMatches(0x1C00, 0xDC00));  // bits 15:14 are not used
  CHECK(confMatches(0x1C00, 0x5C00));
  for (unsigned bit = 0; bit < 14; ++bit) CHECK(!confMatches(0x1C00, static_cast<uint16_t>(0x1C00 ^ (1u << bit))));
  CHECK(!confMatches(0x1C00, 0x0000));  // a chip that powered up with its defaults (all zero) did not take the write
}

EPDJ_TEST(the_register_map_and_the_filtered_raw_switch) {
  CHECK_EQ(kAddress, 0x36);
  CHECK_EQ(kRegConf, 0x07);
  CHECK_EQ(kRegStatus, 0x0B);
  CHECK_EQ(kRegRawAngle, 0x0C);
  CHECK_EQ(kRegAngle, 0x0E);
  CHECK_EQ(angleRegister(true), 0x0E);   // EPDJ_AS5600_FILTERED_ANGLE = y: the filtered ANGLE
  CHECK_EQ(angleRegister(false), 0x0C);  // n: RAW ANGLE
}
