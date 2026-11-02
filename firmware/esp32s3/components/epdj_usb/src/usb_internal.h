// Shared between usb_device.cpp and usb_audio.cpp.
#pragma once
#include <atomic>
#include <cstdint>

namespace epdj {
namespace usb_dev {
namespace detail {

extern std::atomic<uint32_t> g_audioFrames;

void audioOnDetach();     // host went away: stop streaming
bool audioIsStreaming();

}  // namespace detail
}  // namespace usb_dev
}  // namespace epdj
