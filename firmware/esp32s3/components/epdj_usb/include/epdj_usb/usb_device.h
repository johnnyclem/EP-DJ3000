// USB device side: enumerate as the composite MIDI (+ USB Audio 1.0) device and move USB-MIDI packets.
//
// Threads: the TinyUSB task owns the stack. Its MIDI callback only timestamps packets into a queue (popRx,
// drained by the engine task); the engine task writes outgoing packets directly with writePacket(), which is
// TinyUSB's own cross-task pattern (the endpoint FIFOs take a mutex with priority inheritance).
#pragma once
#include <cstdint>

namespace epdj {
namespace usb_dev {

struct Params {
  uint8_t deck = 0;      // 0 = A, 1 = B: product string "EP-DJ3000 Deck A/B"
  bool audio = false;    // include the USB audio function (a DAC is present and EPDJ_USB_AUDIO is on)
  uint8_t mac[6] = {};   // the USB serial number is derived from it
};

struct RxPacket {
  uint64_t rxUs;     // esp_timer time when the TinyUSB task saw it
  uint8_t bytes[4];  // one USB-MIDI event packet: cable << 4 | CIN, 3 data bytes
};

// How long the bus must stay idle before hostActive() turns false.
constexpr uint64_t kSuspendDebounceUs = 1000000;

struct Stats {
  uint32_t rxPackets = 0;
  uint32_t rxDropped = 0;     // rx queue full
  uint32_t txPackets = 0;
  uint32_t txRejected = 0;    // endpoint FIFO full (the caller retries) or no host
  uint32_t audioFrames = 0;   // frames received from the host while streaming
};

bool start(const Params& p);

bool mounted();          // a host has configured the device (and the stack has not seen it leave)
bool suspended();        // the bus is idle: a sleeping host, selective suspend, or the cable was pulled

// A host is there to talk to: configured and not suspended for longer than kSuspendDebounceUs. This is what
// the engine routes by. The PHY has no VBUS sense (usb_device.cpp start()), so pulling the cable is never
// reported as a detach; all the stack sees is the bus going idle, which is also what a host in selective
// suspend looks like, hence the debounce.
bool hostActive();
bool audioStreaming();   // the host has the streaming interface open
bool audioEnabled();     // the audio function is in the descriptors

// What the audio task calls once per block with the rate the host should send at (Q16.16 samples per frame):
// forwards it to the feedback endpoint (TinyUSB sends it as 10.14). nullptr when audio is compiled out.
using FeedbackFn = void (*)(uint32_t samplesPerFrameQ16);
FeedbackFn audioFeedbackSink();

// For REBOOT(bootloader): stops the stack and gives the chip's USB pins back to the USB-Serial-JTAG block, the
// ROM's download port. Starting the stack moves them to the OTG block through RTC registers that a software
// reset does not clear, so without this the ROM boots with a port that has no PHY. Not undoable until the next
// start(); call it right before esp_restart(). Unverified on hardware (docs/BRINGUP.md).
void releaseForRomDownload();

bool popRx(RxPacket* out);               // engine task
bool writePacket(const uint8_t pkt[4]);  // engine task; false = FIFO full or no active host
Stats stats();

}  // namespace usb_dev
}  // namespace epdj
