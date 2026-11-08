// Twin deck I/O on top of the single-deck HAL.
//   MCP23017 0x20 on the display bus: 16 buttons, 1 = pressed in buttons().
//   ADS1115  0x48 on the display bus: pitch fader A (A0) and pitch fader B (A1), 12-bit.
//   MT6701 on I2C1 (GPIO6/7): the right platter. encoderB() is the existing reader.
//   GPIO8-11: loop LEDs, sunk (low = lit).
#pragma once
#include <atomic>
#include <cstdint>

#include "driver/i2c_master.h"
#include "epdj_hal/as5600.h"
#include "epdj_hal/i2c_bus.h"

namespace epdj {
namespace hal {

class TwinIo {
 public:
  // shared is the OLED bus (GPIO21/47). encoderBus is I2C1. False when the MCP23017 does not ACK.
  // The pitch faders and the right encoder are reported in the log by the caller via encoderB().
  bool begin(I2cBus& shared, I2cBus& encoderBus);

  As5600& encoderB() { return encB_; }

  // Call from the 1 kHz task. Buttons are read every 2 ms. The ADC advances one step per call.
  void poll(uint64_t nowUs);

  uint16_t buttons() const { return buttons_; }  // raw, 1 = pressed, not debounced
  uint16_t pitchA() const { return pitchA_; }    // 12-bit, 2048 until the first conversion
  uint16_t pitchB() const { return pitchB_; }

  // Bit 0 IN A, 1 OUT A, 2 IN B, 3 OUT B. 1 = lit.
  void setLeds(uint8_t mask);

 private:
  bool writeMcp(uint8_t reg, uint16_t value);
  bool readMcp(uint16_t* pressed);
  bool writeAds(uint8_t cfgHi);
  bool readAds(int16_t* sample);

  i2c_master_dev_handle_t mcp_ = nullptr;
  i2c_master_dev_handle_t ads_ = nullptr;
  As5600 encB_;
  uint16_t buttons_ = 0;
  uint16_t pitchA_ = 2048;
  uint16_t pitchB_ = 2048;
  uint64_t lastButtonsUs_ = 0;
  uint8_t adsPhase_ = 0;
  bool adsChannel_ = false;  // false = A0 (pitch A), true = A1 (pitch B)
  bool ledsReady_ = false;
};

// Debounced mask published by the engine task for the SD browser, which runs on another task.
void publishTwinButtons(uint16_t debounced);
uint16_t publishedTwinButtons();

}  // namespace hal
}  // namespace epdj
