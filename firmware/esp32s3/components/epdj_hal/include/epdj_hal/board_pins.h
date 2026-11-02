// Pin plan for the EP-DJ3000 on an ESP32-S3-DevKitC-1 (hardware/WIRING.md has the same table with the
// reasons). The static_asserts below make a bad edit a compile error: no GPIO is used twice and none lands on
// a pin the module needs for itself.
#pragma once
#include <cstddef>

namespace epdj {
namespace pins {

// ---- analog (ADC1 only: ADC2 is unusable while the Wi-Fi radio is on) ----
constexpr int kFader = 1;  // ADC1_CH0, slide pot wiper
constexpr int kKnob = 2;   // ADC1_CH1, orange knob wiper

// ---- capacitive touch: platter sector i is on GPIO kTouchFirst + i (touch pad N is GPIO N on the S3) ----
constexpr int kTouchFirst = 4;
constexpr int kTouchMax = 8;  // pads 4..11 = sectors 0..7. In AS5600 mode only pad 4 (sector 0) is wired: hold pad.

// ---- buttons (internal pull-up, pressed = low) ----
constexpr int kCue = 12;
constexpr int kRev = 13;

// ---- DAC-present strap: pull-up, jumper to GND on the DAC board ----
constexpr int kDacStrap = 14;

// ---- I2S to a PCM5102A-class DAC (no MCLK: SCK on the DAC is tied to GND) ----
constexpr int kI2sBck = 15;
constexpr int kI2sWs = 16;
constexpr int kI2sDout = 17;

// ---- I2C: AS5600 magnetic encoder (optional) and optional codec probe ----
constexpr int kI2cSda = 21;
constexpr int kI2cScl = 47;

// ---- TRS MIDI on UART1 (TX through a 3.3 V Type A driver, RX through an optocoupler) ----
constexpr int kTrsTx = 40;
constexpr int kTrsRx = 41;

// ---- status LED: the WS2812 on DevKitC-1 v1.0 (v1.1 moved it to GPIO38: change this one line) ----
constexpr int kStatusLed = 48;

// ------------------------------------------------------------------------------------------------------
// Compile-time checks
// ------------------------------------------------------------------------------------------------------

constexpr int kTouchLast = kTouchFirst + kTouchMax - 1;

// Every pin the firmware claims, touch pads expanded.
constexpr int kAllPins[] = {
    kFader,  kKnob,  kTouchFirst + 0, kTouchFirst + 1, kTouchFirst + 2, kTouchFirst + 3, kTouchFirst + 4,
    kTouchFirst + 5, kTouchFirst + 6, kTouchFirst + 7, kCue,            kRev,            kDacStrap,
    kI2sBck, kI2sWs, kI2sDout,        kI2cSda,         kI2cScl,         kTrsTx,          kTrsRx,
    kStatusLed,
};
constexpr size_t kPinCount = sizeof(kAllPins) / sizeof(kAllPins[0]);
static_assert(kPinCount == 21, "kAllPins must list every pin above, with all 8 touch pads");
static_assert(kTouchMax == 8, "kAllPins expands exactly 8 touch pads");

constexpr bool pinsAreUnique() {
  for (size_t i = 0; i < kPinCount; ++i)
    for (size_t j = i + 1; j < kPinCount; ++j)
      if (kAllPins[i] == kAllPins[j]) return false;
  return true;
}
static_assert(pinsAreUnique(), "a GPIO is assigned twice");

// ESP32-S3-DevKitC-1 pins the module or the board needs for itself.
constexpr bool isReserved(int gpio) {
  return gpio == 0 ||                // BOOT button, strapping
         gpio == 3 ||                // strapping (JTAG source)
         gpio == 19 || gpio == 20 || // USB D- / D+ (native USB, TinyUSB)
         (gpio >= 26 && gpio <= 37) ||  // SPI flash (26-32) and octal PSRAM / octal flash (33-37)
         gpio == 43 || gpio == 44 ||  // UART0 TX / RX: the log console
         gpio == 45 || gpio == 46;    // strapping (VDD_SPI voltage, boot log)
}
constexpr bool noPinIsReserved() {
  for (size_t i = 0; i < kPinCount; ++i)
    if (isReserved(kAllPins[i])) return false;
  return true;
}
static_assert(noPinIsReserved(), "a GPIO collides with a flash/PSRAM, USB, UART0 or strapping pin");

constexpr bool isAdc1(int gpio) { return gpio >= 1 && gpio <= 10; }  // ESP32-S3: GPIO1..10 = ADC1_CH0..9
static_assert(isAdc1(kFader) && isAdc1(kKnob), "fader and knob must be on ADC1");

constexpr bool isTouchCapable(int gpio) { return gpio >= 1 && gpio <= 14; }  // touch pad N = GPIO N
static_assert(isTouchCapable(kTouchFirst) && isTouchCapable(kTouchLast), "touch pads must be GPIO 1..14");
static_assert(kTouchFirst + kTouchMax - 1 <= 14, "touch pads run past GPIO14");

// Plain GPIO inputs/outputs must be real, output-capable pins (GPIO46 and above 48 are not).
static_assert(kStatusLed <= 48 && kI2cScl <= 48 && kTrsTx <= 48, "GPIO number out of range for the S3");

}  // namespace pins
}  // namespace epdj
