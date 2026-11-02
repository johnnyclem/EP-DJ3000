// Capacitive platter pads (ESP32-S3 touch sensor v2, legacy driver). One pad per platter sector.
#pragma once
#include <cstdint>

namespace epdj {
namespace hal {

class TouchPads {
 public:
  static constexpr unsigned kMax = 8;

  // Starts the hardware scan of `count` pads (1..8) on board_pins::kTouchFirst... Returns false if the
  // driver refuses. With EPDJ_TOUCH_SCAN_TARGET_US set (and 2+ pads) it also tunes the measurement length until
  // one scan of all pads takes about that long (touch_range.h), which is what makes every pad carry a new value
  // on each 1 ms engine read. Takes 0.3 to 0.6 s: the first measurements after init are not valid, and the
  // scan is timed at two settings.
  bool begin(unsigned count);
  unsigned count() const { return count_; }

  // Latest raw count of every pad, scaled to 16 bits, LARGER = MORE TOUCHED (a finger adds capacitance, which
  // raises the S3's count). raw[count..7] are 0. Call at ~1 kHz.
  void read(uint16_t raw[kMax]);

  // Right shift applied to the hardware count so that it fits 16 bits with 2x headroom (fixed at begin()).
  unsigned shift() const { return shift_; }
  // Charge/discharge cycles per pad in use, and the duration of one scan of all pads measured at begin() with
  // nothing touching the platter (0 = not measurable: a single pad, or the scan register did not move). While a
  // finger is down the touched pad's measurement, and so the scan, is a little longer.
  unsigned cycles() const { return cycles_; }
  unsigned scanPeriodUs() const { return scanUs_; }

 private:
  unsigned count_ = 0;
  unsigned shift_ = 0;
  unsigned cycles_ = 0;
  unsigned scanUs_ = 0;
};

}  // namespace hal
}  // namespace epdj
