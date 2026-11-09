// Pin plan for the EP-DJ3000 on an ESP32-S3-DevKitC-1 (hardware/WIRING.md has the same table with the
// reasons). The static_asserts below make a bad edit a compile error: no GPIO is used twice and none lands on
// a pin the module needs for itself. They hold for both board revisions of the status LED.
#pragma once
#include <cstddef>

#include "sdkconfig.h"

namespace epdj {
namespace pins {

// ---- analog (ADC1 only: ADC2 is unusable while the Wi-Fi radio is on) ----
constexpr int kFader = 1;        // ADC1_CH0, left pitch slide (the fader whose role is configurable; default PITCH)
constexpr int kKnob = 2;         // ADC1_CH1, orange knob wiper
constexpr int kPitchRight = 5;   // ADC1_CH4, right deck's pitch slide. Always CC 25/57 on the other MIDI channel.
constexpr int kCrossfader = 6;   // ADC1_CH5, the Twin crossfader. Always CC 23/55 on this unit's channel.

// ---- capacitive touch: only the hold pad. GPIO5-11 are the Twin pots and buttons, so the experimental
// touch ring cannot be built on this pin plan (EPDJ_TOUCH_PAD_COUNT is 0 or 1). ----
constexpr int kTouchFirst = 4;
constexpr int kTouchMax = 8;  // the chip's pad numbers 4..11. This firmware wires only pad 4 (the hold pad).

// ---- buttons (internal pull-up, pressed = low) ----
// Left deck, this unit's MIDI channel: CUE, REV (the left toggle), PLAY, LOOP IN.
// Right deck, the other MIDI channel: CUE, PLAY, LOOP IN, LOOP OUT. There is no left LOOP OUT on this plan.
constexpr int kCue = 12;
constexpr int kRev = 13;
constexpr int kPlay = 7;
constexpr int kLoopIn = 8;
constexpr int kCueRight = 9;
constexpr int kPlayRight = 10;
constexpr int kLoopInRight = 11;
// GPIO48 is free on v1.1 (the LED is GPIO38). On v1.0 the LED owns GPIO48, so that revision has no right OUT.
#ifdef CONFIG_EPDJ_BOARD_DEVKITC_1_V1_0
constexpr int kLoopOutRight = -1;
#else
constexpr int kLoopOutRight = 48;
#endif

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

// ---- microSD, SDMMC 1-bit (CLK / CMD / D0). DAT3 is not a GPIO: pull it up to 3V3 on the socket. ----
constexpr int kSdClk = 18;
constexpr int kSdCmd = 39;
constexpr int kSdD0 = 42;

// ---- status LED: the on-board WS2812. v1.1 boards have it on GPIO38, v1.0 on GPIO48 (Kconfig EPDJ_BOARD_REV,
// default v1.1). For another board, replace this constant. ----
#ifdef CONFIG_EPDJ_BOARD_DEVKITC_1_V1_0
constexpr int kStatusLed = 48;
constexpr const char* kBoardRev = "DevKitC-1 v1.0";
#else
constexpr int kStatusLed = 38;
constexpr const char* kBoardRev = "DevKitC-1 v1.1";
#endif

// ------------------------------------------------------------------------------------------------------
// Compile-time checks
// ------------------------------------------------------------------------------------------------------

constexpr int kTouchLast = kTouchFirst + kTouchMax - 1;

// Every pin the firmware claims. The hold pad only: GPIO5-11 are pots and buttons, not touch pads.
constexpr int kAllPins[] = {
    kFader,       kKnob,        kPitchRight,  kCrossfader, kTouchFirst, kCue,           kRev,
    kPlay,        kLoopIn,      kCueRight,    kPlayRight,  kLoopInRight,
#ifdef CONFIG_EPDJ_BOARD_DEVKITC_1_V1_0
#else
    kLoopOutRight,
#endif
    kDacStrap,    kI2sBck,      kI2sWs,       kI2sDout,    kI2cSda,     kI2cScl,        kTrsTx,
    kTrsRx,       kSdClk,       kSdCmd,       kSdD0,       kStatusLed,
};
constexpr size_t kPinCount = sizeof(kAllPins) / sizeof(kAllPins[0]);
#ifdef CONFIG_EPDJ_BOARD_DEVKITC_1_V1_0
static_assert(kPinCount == 24, "kAllPins must list every pin above (v1.0 has no right LOOP OUT)");
#else
static_assert(kPinCount == 25, "kAllPins must list every pin above, including right LOOP OUT");
#endif
static_assert(CONFIG_EPDJ_TOUCH_PAD_COUNT <= 1, "GPIO5-11 are Twin controls; only the hold pad on GPIO4 is a touch input");

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
static_assert(isAdc1(kFader) && isAdc1(kKnob) && isAdc1(kPitchRight) && isAdc1(kCrossfader),
              "slide pots must be on ADC1");
static_assert(kLoopOutRight < 0 || kLoopOutRight != kStatusLed, "right LOOP OUT collides with the status LED");

constexpr bool isTouchCapable(int gpio) { return gpio >= 1 && gpio <= 14; }  // touch pad N = GPIO N
static_assert(isTouchCapable(kTouchFirst) && isTouchCapable(kTouchLast), "touch pads must be GPIO 1..14");
static_assert(kTouchFirst + kTouchMax - 1 <= 14, "touch pads run past GPIO14");

// Plain GPIO inputs/outputs must be real, output-capable pins (GPIO46 and above 48 are not).
static_assert(kStatusLed <= 48 && kI2cScl <= 48 && kTrsTx <= 48, "GPIO number out of range for the S3");

}  // namespace pins
}  // namespace epdj
