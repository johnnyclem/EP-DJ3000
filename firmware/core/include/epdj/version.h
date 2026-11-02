#pragma once
namespace epdj {
// Firmware core semantic version (the ESP-IDF app reports it in Info.fwMajor/Minor/Patch).
constexpr int kVersionMajor = 0;
constexpr int kVersionMinor = 1;
constexpr int kVersionPatch = 0;
const char* coreVersion();
}  // namespace epdj
