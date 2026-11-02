// Capacitive platter pads (ESP32-S3 touch sensor v2, legacy driver). One pad per platter sector.
#pragma once
#include <cstdint>

namespace epdj {
namespace hal {

class TouchPads {
 public:
  static constexpr unsigned kMax = 8;

  // Starts the hardware scan of `count` pads (1..8) on board_pins::kTouchFirst... Returns false if the
  // driver refuses. Takes ~100 ms: the first measurements after init are not valid.
  bool begin(unsigned count);
  unsigned count() const { return count_; }

  // Latest raw count of every pad, scaled to 16 bits, LARGER = MORE TOUCHED (a finger adds capacitance, which
  // raises the S3's count). raw[count..7] are 0. Call at ~1 kHz.
  void read(uint16_t raw[kMax]);

  // Right shift applied to the hardware count so that it fits 16 bits with 2x headroom (fixed at begin()).
  unsigned shift() const { return shift_; }
  // Share of the last reads in which at least one pad had a new value, 0..100. Well below 100 means the
  // hardware scan is slower than the 1 kHz read rate (lower EPDJ_TOUCH_CHARGE_DISCHARGE_TIMES).
  unsigned freshPercent() const { return fresh_; }

 private:
  unsigned count_ = 0;
  unsigned shift_ = 0;
  unsigned fresh_ = 100;
  uint16_t last_[kMax] = {};
  uint32_t reads_ = 0, changed_ = 0;
};

}  // namespace hal
}  // namespace epdj
