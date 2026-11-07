// MagnTek MT6701, the platter encoder on the Field Jog boards (a 23 mm square breakout, header soldered by hand).
// Header-only so the host tests can check the angle packing without a chip.
//
// I2C address 0x06 (7-bit). MODE is strapped high on this board, which is the I2C boot. Analog/PWM is not wired.
// Angle is 14 bits in two registers: 0x03 is ANGLE[13:6], 0x04 bits 7:2 are ANGLE[5:0]. The low two bits of 0x04
// are not angle. The engine still speaks 12-bit angles (0..4095, 1024 jog ticks a turn), so the reader keeps the
// top 12 of those 14.
#pragma once
#include <cstdint>

namespace epdj {
namespace hal {
namespace mt6701 {

constexpr uint8_t kAddress = 0x06;
constexpr uint8_t kRegAngle = 0x03;  // two bytes, big-endian, then shift

constexpr uint16_t angle14(uint8_t reg03, uint8_t reg04) {
  return static_cast<uint16_t>(((static_cast<unsigned>(reg03) << 8) | reg04) >> 2) & 0x3FFFu;
}

constexpr uint16_t angle12(uint8_t reg03, uint8_t reg04) {
  return static_cast<uint16_t>(angle14(reg03, reg04) >> 2);
}

}  // namespace mt6701
}  // namespace hal
}  // namespace epdj
