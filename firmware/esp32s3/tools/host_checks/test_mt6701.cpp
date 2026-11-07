// mt6701_regs.h: 14-bit angle packing, and the 12-bit value the engine still consumes.
#include <cstdint>

#include "epdj_hal/mt6701_regs.h"
#include "test_harness.h"

using namespace epdj::hal::mt6701;

EPDJ_TEST(address_and_register_are_the_mt6701) {
  CHECK_EQ(kAddress, 0x06);
  CHECK_EQ(kRegAngle, 0x03);
}

EPDJ_TEST(fourteen_bits_sit_in_the_top_of_the_two_registers) {
  CHECK_EQ(angle14(0x00, 0x00), 0);
  CHECK_EQ(angle14(0xFF, 0xFC), 0x3FFF);  // all 14 angle bits set; low 2 of 0x04 ignored
  CHECK_EQ(angle14(0xFF, 0xFF), 0x3FFF);
  CHECK_EQ(angle14(0x00, 0x04), 1);       // ANGLE[0] is bit 2 of the second byte
  CHECK_EQ(angle14(0x00, 0x03), 0);       // the low two bits are not angle
  CHECK_EQ(angle14(0x10, 0x00), 0x0400);
}

EPDJ_TEST(the_engine_keeps_the_top_twelve_bits) {
  CHECK_EQ(angle12(0x00, 0x00), 0);
  CHECK_EQ(angle12(0xFF, 0xFC), 0x0FFF);  // one turn is still 4096 counts
  CHECK_EQ(angle12(0x00, 0x0C), 0);       // angle14 == 3, dropped by the shift
  CHECK_EQ(angle12(0x00, 0x10), 1);       // angle14 == 4
  CHECK_EQ(angle12(0x10, 0x00), 256);     // angle14 == 1024
}
