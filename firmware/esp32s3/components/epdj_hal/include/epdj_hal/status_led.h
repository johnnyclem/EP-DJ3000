// The status LED: one WS2812 driven from the RMT peripheral. The engine reports what to show (LedStatus);
// colours and animation live here.
#pragma once
#include <cstdint>

#include "driver/rmt_tx.h"
#include "epdj/engine.h"

namespace epdj {
namespace hal {

class StatusLed {
 public:
  struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
  };

  bool begin();
  // Call every few ms; the strip is only rewritten when the colour changes.
  void show(const LedStatus& s, uint64_t nowUs);

  // The colour for a status at a time (pure; exposed for the boot self-test).
  static Rgb colorFor(const LedStatus& s, uint64_t nowUs);
  void set(Rgb c);

 private:
  rmt_channel_handle_t chan_ = nullptr;
  rmt_encoder_handle_t encoder_ = nullptr;
  Rgb last_;
  bool haveLast_ = false;
};

}  // namespace hal
}  // namespace epdj
