// Platter encoder. The class name is As5600 because the protocol, the caps bit and the engine all still say AS5600
// (a free-spinning platter, 12-bit angle). The chip on the board is a MagnTek MT6701: address 0x06, 14-bit angle in
// registers 0x03/0x04, no AS5600 CONF and no STATUS magnet nibble. readAngle() returns the top 12 of those 14 bits.
// 400 kHz. The retry timing is encoder_retry.h. This class is the part that touches the I2C driver.
#pragma once
#include <atomic>
#include <cstdint>

#include "epdj_hal/as5600_regs.h"
#include "epdj_hal/encoder_retry.h"
#include "epdj_hal/i2c_bus.h"
#include "epdj_hal/mt6701_regs.h"
#include "epdj_hal/sensor_budget.h"

namespace epdj {
namespace hal {

class As5600 {
 public:
  static constexpr uint8_t kAddress = mt6701::kAddress;

  // Registers the chip with the bus driver (no bus traffic, so this works for a chip that is not there yet), probes it, and
  // if it answers: times the angle read. Returns whether the chip answered.
  // Not answering is not final: readAngle() asks again every 500 ms (encoder_retry.h). Logs nothing: main.cpp prints the
  // one boot line that says what was found, the diagnostics task the later events.
  bool begin(I2cBus& bus);

  // A chip has answered since boot (at boot, or found later by the re-probe).
  bool present() const { return present_.load(); }
  // The chip is registered with the bus driver, so readAngle() can find it later even if it did not answer at boot. False
  // only when the I2C bus did not start or the driver refused the device: then nothing will ever be read.
  bool canRetry() const { return dev_ != nullptr; }
  sensor_budget::Magnet magnet() const { return magnet_; }  // as found when the chip first answered; not re-read while running
  uint8_t status() const { return status_; }                // raw STATUS register at that moment

  // CONF as the chip read it back after our write; confKnown() is false when the write or the read-back failed.
  bool confKnown() const { return confKnown_; }
  uint16_t conf() const { return confRead_; }
  bool confOk() const { return confKnown_ && as5600::confMatches(as5600::kScratchConfWord, confRead_); }

  // The MT6701 has no AS5600 CONF register and no STATUS magnet nibble. These stay false so the boot log does not
  // warn about a write this chip cannot take.
  static bool writesConf() { return false; }
  static bool reportsMagnet() { return false; }

  // Angle comes from MT6701 register 0x03 (two bytes). The AS5600 filtered/raw switch does not apply.
  static bool filteredAngle();
  static uint8_t angleRegister() { return mt6701::kRegAngle; }

  // Mean duration of one angle read, measured when the chip first answered at boot (0 = not measurable, or it did not
  // answer then). The 1 kHz loop spends this much of every millisecond waiting for the bus; main.cpp takes it off the ADC's
  // share of the sensor time budget (or a reserve, sensor_budget.h, if the chip was not there).
  unsigned readUs() const { return readUs_; }

  // Angle 0..4095, false if there is none this call: a NACK/timeout, or the chip is lost or absent and it is not yet time
  // to ask again (no bus traffic then). The caller reports angleValid = false for that sample. Also the re-probe: when
  // the chip answers again, CONF is written again, present() turns true if it is the first time, and recoveries() counts it.
  bool readAngle(uint16_t* angle12, uint64_t nowUs);

  uint32_t failures() const { return failures_.load(); }        // failed reads while the chip was answering
  uint32_t losses() const { return losses_.load(); }            // times it stopped answering (two failed reads in a row)
  uint32_t recoveries() const { return recoveries_.load(); }    // times it started answering again, or answered for the first time after boot
  bool appearedAfterBoot() const { return appearedLate_.load(); }

 private:
  bool readReg(uint8_t reg, uint8_t* out, size_t n, int timeoutMs);

  i2c_master_dev_handle_t dev_ = nullptr;
  EncoderRetry retry_;
  std::atomic<bool> present_{false};
  std::atomic<bool> appearedLate_{false};
  sensor_budget::Magnet magnet_ = sensor_budget::Magnet::Unknown;
  uint8_t status_ = 0;
  bool confKnown_ = false;
  uint16_t confRead_ = 0;
  unsigned readUs_ = 0;
  std::atomic<uint32_t> failures_{0}, losses_{0}, recoveries_{0};
};

}  // namespace hal
}  // namespace epdj
