// USB descriptor builder: device + configuration descriptor of the EP-DJ3000 composite device, as plain bytes.
// Pure (no TinyUSB, no heap, no globals) so the descriptor layout is unit-tested on the host. The ESP-IDF layer
// hands the result to TinyUSB; firmware/esp32s3/tools/crosscheck_usb_desc compares these bytes against
// TinyUSB's own TUD_* macros.
//
// Layout (interface numbers and endpoint addresses are fixed, so the MIDI function looks the same with and
// without the DAC):
//
//   IAD  if 0-1  USB-MIDI 1.0: AudioControl (0) + MIDIStreaming (1), kMidiCables embedded jack pairs,
//                bulk OUT 0x02 / bulk IN 0x82, 64 B
//   IAD  if 2-3  (audio only) UAC2 speaker, stereo 48 kHz 16-bit: AudioControl (2) + AudioStreaming (3),
//                alt 0 = idle, alt 1 = isochronous OUT 0x01 (async) + explicit feedback IN 0x81
//
// Because both functions sit behind an IAD the device class is Miscellaneous/Common/IAD.
#pragma once
#include <cstddef>
#include <cstdint>

namespace epdj {
namespace usb {

// Identity: protocol/epdj3000.json "device" (test_usb_desc.cpp fails if they drift apart).
constexpr uint16_t kVendorId = 0x1209;   // pid.codes (open hardware)
constexpr uint16_t kProductId = 0x0001;  // DEV PLACEHOLDER (pid.codes test PID): request a real one before sharing units
constexpr const char* kManufacturer = "EP-DJ3000";
constexpr const char* kProductPrefix = "EP-DJ3000 ";  // + "Deck A" / "Deck B"
constexpr const char* kDeckNames[2] = {"Deck A", "Deck B"};

// Jack strings = cable names (PROTOCOL.md section 1). Cable n <-> USB-MIDI cable number n.
constexpr uint8_t kMidiCables = 3;
constexpr const char* kCableNames[kMidiCables] = {"Deck", "TRS MIDI", "Linked deck"};
constexpr const char* kAudioName = "EP-DJ3000 Audio";

// String descriptor indices. esp_tinyusb keeps at most 8 string slots (LANGID, manufacturer, product, serial
// and four more), so the three jack names and the audio name use exactly the four user slots.
constexpr uint8_t kStrLangId = 0;
constexpr uint8_t kStrManufacturer = 1;
constexpr uint8_t kStrProduct = 2;
constexpr uint8_t kStrSerial = 3;
constexpr uint8_t kStrCable0 = 4;  // cable n uses kStrCable0 + n
constexpr uint8_t kStrAudio = 7;
constexpr uint8_t kStringCount = 8;

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

// Audio format: the only one offered (the clock source is fixed).
constexpr uint32_t kSampleRateHz = 48000;
constexpr uint8_t kChannels = 2;
constexpr uint8_t kSubslotBytes = 2;
constexpr uint8_t kBitResolution = 16;
// One USB frame is 1 ms; an async source may be one sample frame over nominal.
constexpr uint16_t kAudioPacketBytes =
    static_cast<uint16_t>((kSampleRateHz / 1000 + 1) * kChannels * kSubslotBytes);
constexpr uint16_t kFeedbackPacketBytes = 4;  // TinyUSB sends 16.16 in four bytes for UAC2

// UAC2 entity ids (what the class-specific control requests address in wIndex high byte).
constexpr uint8_t kEntityInputTerminal = 1;
constexpr uint8_t kEntityFeatureUnit = 2;
constexpr uint8_t kEntityOutputTerminal = 3;
constexpr uint8_t kEntityClockSource = 4;
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

}  // namespace usb
}  // namespace epdj
