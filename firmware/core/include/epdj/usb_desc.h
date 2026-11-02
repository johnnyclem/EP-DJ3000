// USB descriptor builder: device + configuration descriptor and string table of the EP-DJ3000 composite device,
// as plain bytes. Pure (no TinyUSB, no heap, no globals) so the descriptor layout is unit-tested on the host. The
// ESP-IDF layer hands the result to TinyUSB; firmware/esp32s3/tools/crosscheck_usb_desc compares these bytes
// against TinyUSB's own TUD_* macros and feeds them to TinyUSB's audio class driver.
//
// Identity (VID/PID, product name, deck names, cable names) is the contract's: proto::kUsbVendorId etc. in the
// generated protocol.h. Nothing here repeats it.
//
// Layout (interface numbers and endpoint addresses are fixed, so the MIDI function looks the same with and
// without the DAC):
//
//   IAD  if 0-1  USB-MIDI 1.0: AudioControl (0) + MIDIStreaming (1), kMidiCables embedded jack pairs,
//                bulk OUT 0x02 / bulk IN 0x82, 64 B
//   IAD  if 2-3  (audio only) USB Audio Class 1.0 speaker, stereo 48 kHz 16-bit: AudioControl (2) + AudioStreaming
//                (3), alt 0 = idle, alt 1 = isochronous OUT 0x01 (asynchronous) + synch IN 0x81 (3-byte 10.14
//                feedback). Terminals: USB streaming -> feature unit (mute, volume) -> line out.
//
// Why UAC1 and not UAC2: the ESP32-S3 is a full-speed device. TinyUSB's own full-speed example uses UAC1, its
// UAC2 driver sends the feedback value as four bytes 16.16 whatever the bus speed (full-speed hosts expect three
// bytes 10.14) and its UAC1 driver sends exactly that. docs/ARCHITECTURE.md decision 5.
//
// Because both functions sit behind an IAD the device class is Miscellaneous/Common/IAD.
#pragma once
#include <cstddef>
#include <cstdint>

#include "epdj/protocol.h"

namespace epdj {
namespace usb {

// Jack strings = cable names (PROTOCOL.md section 1). Cable n <-> USB-MIDI cable number n.
constexpr uint8_t kMidiCables = sizeof(proto::kCableNames) / sizeof(proto::kCableNames[0]);

// String descriptor indices. esp_tinyusb keeps at most 8 string slots (LANGID, manufacturer, product, serial
// and four more), so the three jack names and the audio function name use exactly the four user slots.
constexpr uint8_t kStrLangId = 0;
constexpr uint8_t kStrManufacturer = 1;
constexpr uint8_t kStrProduct = 2;
constexpr uint8_t kStrSerial = 3;
constexpr uint8_t kStrCable0 = 4;  // cable n uses kStrCable0 + n
constexpr uint8_t kStrAudio = 7;
constexpr uint8_t kStringCount = 8;
constexpr size_t kMaxStringChars = 31;  // esp_tinyusb serves at most this many characters of a string

// Interfaces and endpoints.
constexpr uint8_t kItfMidiControl = 0;
constexpr uint8_t kItfMidiStreaming = 1;
constexpr uint8_t kItfAudioControl = 2;
constexpr uint8_t kItfAudioStreaming = 3;
constexpr uint8_t kEpMidiOut = 0x02;
constexpr uint8_t kEpMidiIn = 0x82;
constexpr uint8_t kEpAudioOut = 0x01;
constexpr uint8_t kEpAudioFeedback = 0x81;
constexpr uint16_t kMidiPacketBytes = 64;  // full-speed bulk maximum

// Audio format: the only one offered (a single fixed rate, so the host has nothing to choose).
constexpr uint32_t kSampleRateHz = 48000;
constexpr uint8_t kChannels = 2;
constexpr uint8_t kSubslotBytes = 2;
constexpr uint8_t kBitResolution = 16;
// One USB frame is 1 ms; an async source may be one sample frame over nominal.
constexpr uint16_t kAudioPacketBytes =
    static_cast<uint16_t>((kSampleRateHz / 1000 + 1) * kChannels * kSubslotBytes);
constexpr uint16_t kFeedbackPacketBytes = 3;  // samples per frame in 10.14, little-endian (USB 2.0 section 5.12.4.2)

// UAC1 unit and terminal ids (what class-specific control requests address in the wIndex high byte).
constexpr uint8_t kEntityInputTerminal = 1;
constexpr uint8_t kEntityFeatureUnit = 2;
constexpr uint8_t kEntityOutputTerminal = 3;
constexpr uint16_t kTerminalLineConnector = 0x0603;  // external output: "Line Out"

constexpr uint16_t kMaxPowerMa = 250;  // bus powered: ESP32-S3 with radio on + DAC board
constexpr size_t kDeviceDescriptorBytes = 18;
constexpr size_t kMaxConfigDescriptorBytes = 320;  // upper bound for buffers; exact size from configDescriptorBytes()

// 18-byte device descriptor. bcdDevice differs with and without audio so a host that cached the old
// interface set for this VID/PID/serial sees a different device release.
size_t buildDeviceDescriptor(bool audio, uint8_t* out, size_t cap);

// Whole configuration descriptor (configuration header + all functions). Returns the byte count, or 0 if
// `cap` is too small.
size_t buildConfigDescriptor(bool audio, uint8_t* out, size_t cap);
size_t configDescriptorBytes(bool audio);

// "EP-DJ3000 Deck A" / "... Deck B" (deck > 1 is treated as B). NUL-terminated; returns the length, or 0 if
// `cap` is too small.
size_t formatProductName(uint8_t deck, char* out, size_t cap);

// 12 upper-case hex digits of a MAC, NUL-terminated: the USB serial number. Returns 12, or 0 if cap < 13.
size_t formatSerial(const uint8_t mac[6], char* out, size_t cap);

// The string descriptors for one unit, in the layout esp_tinyusb takes: `table` goes into
// tinyusb_config_t.descriptor.string with kStringCount entries. The entries point into this struct (and at static
// text), so it must outlive the USB stack and must not be copied.
struct Strings {
  const char* table[kStringCount] = {};
  char product[kMaxStringChars + 1] = {};
  char audio[kMaxStringChars + 1] = {};
  char serial[13] = {};

  Strings() = default;
  Strings(const Strings&) = delete;
  Strings& operator=(const Strings&) = delete;
};

// Fills `out` for deck 0 (A) / 1 (B) and a station MAC. Manufacturer is the contract's product name, the
// product string "<name> Deck A|B", the jack names are the contract's cable names. False if a string does not
// fit esp_tinyusb's limit (cannot happen with the contract's values; the host test pins it).
bool buildStrings(uint8_t deck, const uint8_t mac[6], Strings* out);

}  // namespace usb
}  // namespace epdj
