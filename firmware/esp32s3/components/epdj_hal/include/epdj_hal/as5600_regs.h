// AS5600 register map, the CONF register built from named fields, and the choice between the filtered and the raw angle
// register. Header-only and free of ESP-IDF so that tools/host_checks tests it.
//
// WHERE THE FACTS COME FROM. The ams-OSRAM datasheet PDF could not be fetched while this was written (every mirror
// answered 403 or 404), so none of this was read from the datasheet itself. It was cross-checked between four independent
// implementations that were:
//   * Rob Tillaart, AS5600 Arduino library v0.6.7 (github.com/RobTillaart/AS5600): AS5600.cpp (register addresses
//     CONF 0x07+0x08, RAW_ANGLE 0x0C, ANGLE 0x0E; per-field masks and shifts; readAngle() reads 0x0E, rawAngle() 0x0C),
//     AS5600.h (value tables) and README.md ("Configuration bits": PM 0-1, HYST 2-3, OUTS 4-5, PWMF 6-7, SF 8-9, FTH 10-12,
//     WD 13, 15-14 not used; setConfiguration() rejects values above 0x3FFF).
//   * the Linux IIO driver patch "iio: position: Add support for ams AS5600 angle sensor" (2021): REG_CONF 0x07 with the
//     same field masks, REG_RAW_ANGLE 0x0c, REG_ANGLE 0x0e; channel 0 is "the unscaled angle", channel 1 the angle
//     "adjusted by ZPOS/MPOS/MANG".
//   * TinyGo drivers/as560x: "RAW_ANGLE is the raw unscaled & unadjusted angle", "ANGLE is RAW_ANGLE scaled & adjusted
//     according to ZPOS (and MPOS/MANG on AS5600)"; FTH values 0..7 = slow only, 6, 7, 9, 18, 21, 24, 10 LSB.
//   * Adafruit CircuitPython AS5600: the same FTH table, SF 0..3 = 16x, 8x, 4x, 2x, HYST 0..3 = off, 1, 2, 3 LSB, and
//     "WD true enables the watchdog ... switching into low power mode LPM3".
// What is therefore established: the CONF layout below, the FTH/SF/HYST tables (note FTH 7 is 10 LSB, not "the largest":
// the order is 6, 7, 9, 18, 21, 24, 10), and that RAW ANGLE is documented as unscaled and unmodified/unadjusted while
// ANGLE is the scaled output. What is NOT established by anything read: that the slow and fast filters act on ANGLE and not
// on RAW ANGLE. That is what the datasheet's wording ("unmodified") implies and what the original Field Jog firmware relied
// on when it read 0x0E after configuring the filters; this firmware does the same and gives the bench an A/B switch
// (EPDJ_AS5600_FILTERED_ANGLE) for exactly that reason. Step-response figures seen in datasheet excerpts (slow 16x about
// 2.2 ms, 2x about 0.3 ms) are also from memory of excerpts and unchecked.
//
// CONF is volatile RAM until the chip's one-time BURN_SETTING command, which this firmware never sends. A chip that
// loses power forgets what we wrote; the HAL writes it again whenever the chip starts answering.
#pragma once
#include <cstdint>

namespace epdj {
namespace hal {
namespace as5600 {

constexpr uint8_t kAddress = 0x36;

constexpr uint8_t kRegConf = 0x07;      // 16 bits, big endian on the wire: 0x07 = high byte (SF, FTH, WD), 0x08 = low byte
constexpr uint8_t kRegStatus = 0x0B;
constexpr uint8_t kRegRawAngle = 0x0C;  // 12 bits, big endian: unscaled, documented as unmodified
constexpr uint8_t kRegAngle = 0x0E;     // 12 bits, big endian: scaled output; the filters' register as far as anyone says

enum class PowerMode : uint8_t { Nominal = 0, Low1 = 1, Low2 = 2, Low3 = 3 };
enum class Hysteresis : uint8_t { Off = 0, Lsb1 = 1, Lsb2 = 2, Lsb3 = 3 };
enum class SlowFilter : uint8_t { X16 = 0, X8 = 1, X4 = 2, X2 = 3 };
// The code is what goes into the register; the name is the threshold in LSB. Code 7 is 10 LSB, out of order.
enum class FastFilterThreshold : uint8_t { SlowOnly = 0, Lsb6 = 1, Lsb7 = 2, Lsb9 = 3, Lsb18 = 4, Lsb21 = 5, Lsb24 = 6, Lsb10 = 7 };

// The fields this firmware sets. The output stage (OUTS) and PWM frequency (PWMF) bits are left at 0: nothing is
// connected to the OUT pin.
struct Conf {
  PowerMode power;
  Hysteresis hysteresis;
  SlowFilter slow;
  FastFilterThreshold fast;
  bool watchdog;
};

constexpr unsigned kShiftPower = 0;
constexpr unsigned kShiftHysteresis = 2;
constexpr unsigned kShiftSlow = 8;
constexpr unsigned kShiftFast = 10;
constexpr unsigned kShiftWatchdog = 13;

// Bits 15 and 14 are not used: they are ignored when a written value is compared with what the chip reads back.
constexpr uint16_t kConfMask = 0x3FFF;

constexpr uint16_t confWord(const Conf& c) {
  return static_cast<uint16_t>((static_cast<unsigned>(c.power) << kShiftPower) | (static_cast<unsigned>(c.hysteresis) << kShiftHysteresis) |
                               (static_cast<unsigned>(c.slow) << kShiftSlow) | (static_cast<unsigned>(c.fast) << kShiftFast) |
                               ((c.watchdog ? 1u : 0u) << kShiftWatchdog));
}

// What the original Field Jog firmware writes at start-up: watchdog off, hysteresis 0, slow filter setting 0 (16x), fast
// filter threshold setting 7 (10 LSB). Why those make sense for a jog wheel (the datasheet's description, from memory; none of
// it measured here): hysteresis off adds no dead band to a jog path that already quantises to 4 counts per tick; the 16x slow
// filter takes the noise down while the platter is slow or still; a step bigger than the threshold is passed by the fast
// filter, so a scratch is not held back by the slow one.
constexpr Conf kScratchConf = {PowerMode::Nominal, Hysteresis::Off, SlowFilter::X16, FastFilterThreshold::Lsb10, false};
constexpr uint16_t kScratchConfWord = confWord(kScratchConf);  // 0x1C00

constexpr bool confMatches(uint16_t wanted, uint16_t readBack) { return ((wanted ^ readBack) & kConfMask) == 0; }

// The register the platter's angle is read from: the filtered/scaled ANGLE, or the unscaled RAW ANGLE.
constexpr uint8_t angleRegister(bool filtered) { return filtered ? kRegAngle : kRegRawAngle; }

}  // namespace as5600
}  // namespace hal
}  // namespace epdj
