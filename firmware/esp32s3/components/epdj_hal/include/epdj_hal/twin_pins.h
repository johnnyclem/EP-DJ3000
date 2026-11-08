// Pins and addresses that only the twin (two-platter) deck uses.
// GPIO6/7 are touch pads in the experimental ring. The twin scans two hold pads (GPIO4 and GPIO5)
// and spends 6/7 on the second encoder. GPIO8-11 are the loop LEDs, sunk. See hardware/TWIN.md.
#pragma once

namespace epdj {
namespace twin_pins {

constexpr int kEncSda = 6;
constexpr int kEncScl = 7;

constexpr int kLedInA = 8;
constexpr int kLedOutA = 9;
constexpr int kLedInB = 10;
constexpr int kLedOutB = 11;

constexpr uint8_t kMcpAddress = 0x20;  // A2 A1 A0 = GND
constexpr uint8_t kAdsAddress = 0x48;  // ADDR = GND

}  // namespace twin_pins
}  // namespace epdj
