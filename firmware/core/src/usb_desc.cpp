#include "epdj/usb_desc.h"

#include <cstring>

namespace epdj {
namespace usb {
namespace {

// USB / audio class constants (USB 2.0 table 9-x, Audio 1.0, USB-MIDI 1.0). Named after TinyUSB's.
constexpr uint8_t kDescDevice = 0x01;
constexpr uint8_t kDescConfiguration = 0x02;
constexpr uint8_t kDescInterface = 0x04;
constexpr uint8_t kDescEndpoint = 0x05;
constexpr uint8_t kDescIad = 0x0B;
constexpr uint8_t kDescCsInterface = 0x24;
constexpr uint8_t kDescCsEndpoint = 0x25;

constexpr uint8_t kClassAudio = 0x01;
constexpr uint8_t kSubclassControl = 0x01;
constexpr uint8_t kSubclassStreaming = 0x02;
constexpr uint8_t kSubclassMidiStreaming = 0x03;
// Audio 1.0 has no protocol code (0 = "version 1.0"); the MIDI function uses 0 as well.

constexpr uint8_t kXferBulk = 0x02;
constexpr uint8_t kXferIsochronous = 0x01;
constexpr uint8_t kIsoAsync = 0x04;  // synchronisation type: asynchronous

// Audio 1.0 class-specific descriptor subtypes.
constexpr uint8_t kAcHeader = 0x01;
constexpr uint8_t kAcInputTerminal = 0x02;
constexpr uint8_t kAcOutputTerminal = 0x03;
constexpr uint8_t kAcFeatureUnit = 0x06;
constexpr uint8_t kAsGeneral = 0x01;
constexpr uint8_t kAsFormatType = 0x02;
constexpr uint8_t kFormatTypeI = 0x01;
constexpr uint16_t kFormatTagPcm = 0x0001;
constexpr uint8_t kCsEpGeneral = 0x01;
constexpr uint8_t kCsEpSamplingFreqControl = 0x01;  // the endpoint accepts a sampling frequency request
constexpr uint16_t kTerminalUsbStreaming = 0x0101;
constexpr uint16_t kChannelsLeftRightFront = 0x0003;
constexpr uint16_t kFuMuteAndVolume = 0x0003;  // bmaControls bit 0 mute, bit 1 volume

// MIDI streaming subtypes.
constexpr uint8_t kMsHeader = 0x01;
constexpr uint8_t kMsInJack = 0x02;
constexpr uint8_t kMsOutJack = 0x03;
constexpr uint8_t kMsEndpointGeneral = 0x01;
constexpr uint8_t kJackEmbedded = 0x01;
constexpr uint8_t kJackExternal = 0x02;

// Feature unit with a 2-byte control bitmap for the master and each channel.
constexpr uint8_t kFuControlBytes = 2;
constexpr uint8_t kAcHeaderBytes = 8 + 1;  // one streaming interface
constexpr uint8_t kInputTermBytes = 12;
constexpr uint8_t kOutputTermBytes = 9;
constexpr uint8_t kFeatureUnitBytes = 7 + (kChannels + 1) * kFuControlBytes;
constexpr uint16_t kUac1AcTotal =
    static_cast<uint16_t>(kAcHeaderBytes + kInputTermBytes + kOutputTermBytes + kFeatureUnitBytes);

constexpr uint8_t kJackSetBytes = 6 + 6 + 9 + 9;  // per cable: IN emb, IN ext, OUT emb, OUT ext
constexpr uint8_t kMidiEpBytes = 9 + 4 + kMidiCables;
constexpr uint16_t kMsTotal = static_cast<uint16_t>(7 + kMidiCables * kJackSetBytes + 2 * kMidiEpBytes);

// The jack names and the audio function name use exactly the four user string slots.
static_assert(kStrCable0 + kMidiCables == kStrAudio && kStrAudio + 1 == kStringCount,
              "the contract's cable count no longer fits esp_tinyusb's eight string slots");

// Jack ids as TinyUSB numbers them: cable n (1-based) owns 4n-3 .. 4n.
constexpr uint8_t jackInEmb(unsigned n) { return static_cast<uint8_t>((n - 1) * 4 + 1); }
constexpr uint8_t jackInExt(unsigned n) { return static_cast<uint8_t>((n - 1) * 4 + 2); }
constexpr uint8_t jackOutEmb(unsigned n) { return static_cast<uint8_t>((n - 1) * 4 + 3); }
constexpr uint8_t jackOutExt(unsigned n) { return static_cast<uint8_t>((n - 1) * 4 + 4); }

// Appends bytes and keeps counting past the end of the buffer, so one code path serves both "how big" and
// "write it".
class Writer {
 public:
  Writer(uint8_t* out, size_t cap) : out_(out), cap_(cap) {}
  void u8(unsigned v) {
    if (n_ < cap_) out_[n_] = static_cast<uint8_t>(v);
    ++n_;
  }
  void u16(unsigned v) {
    u8(v & 0xFFu);
    u8((v >> 8) & 0xFFu);
  }
  void u24(uint32_t v) {
    u16(v & 0xFFFFu);
    u8((v >> 16) & 0xFFu);
  }
  size_t size() const { return n_; }
  bool fits() const { return n_ <= cap_; }

 private:
  uint8_t* out_;
  size_t cap_;
  size_t n_ = 0;
};

void iad(Writer& w, uint8_t firstItf, uint8_t count) {
  w.u8(8);
  w.u8(kDescIad);
  w.u8(firstItf);
  w.u8(count);
  w.u8(kClassAudio);
  w.u8(0);  // function subclass: undefined
  w.u8(0);  // function protocol: audio 1.0 / MIDI (no version code)
  w.u8(0);
}

void interfaceDesc(Writer& w, uint8_t itf, uint8_t alt, uint8_t numEps, uint8_t subclass, uint8_t str) {
  w.u8(9);
  w.u8(kDescInterface);
  w.u8(itf);
  w.u8(alt);
  w.u8(numEps);
  w.u8(kClassAudio);
  w.u8(subclass);
  w.u8(0);  // protocol
  w.u8(str);
}

void midiFunction(Writer& w) {
  iad(w, kItfMidiControl, 2);
  interfaceDesc(w, kItfMidiControl, 0, 0, kSubclassControl, 0);
  // AudioControl header: bcdADC 1.00, wTotalLength 9, one streaming interface.
  w.u8(9);
  w.u8(kDescCsInterface);
  w.u8(kAcHeader);
  w.u16(0x0100);
  w.u16(9);
  w.u8(1);
  w.u8(kItfMidiStreaming);

  interfaceDesc(w, kItfMidiStreaming, 0, 2, kSubclassMidiStreaming, 0);
  w.u8(7);
  w.u8(kDescCsInterface);
  w.u8(kMsHeader);
  w.u16(0x0100);
  w.u16(kMsTotal);

  for (unsigned c = 0; c < kMidiCables; ++c) {
    const unsigned n = c + 1;
    const uint8_t str = static_cast<uint8_t>(kStrCable0 + c);
    w.u8(6); w.u8(kDescCsInterface); w.u8(kMsInJack); w.u8(kJackEmbedded); w.u8(jackInEmb(n)); w.u8(str);
    w.u8(6); w.u8(kDescCsInterface); w.u8(kMsInJack); w.u8(kJackExternal); w.u8(jackInExt(n)); w.u8(str);
    // Embedded OUT jack is fed by the external IN jack (device -> host), and vice versa.
    w.u8(9); w.u8(kDescCsInterface); w.u8(kMsOutJack); w.u8(kJackEmbedded); w.u8(jackOutEmb(n)); w.u8(1);
    w.u8(jackInExt(n)); w.u8(1); w.u8(str);
    w.u8(9); w.u8(kDescCsInterface); w.u8(kMsOutJack); w.u8(kJackExternal); w.u8(jackOutExt(n)); w.u8(1);
    w.u8(jackInEmb(n)); w.u8(1); w.u8(str);
  }

  // Bulk OUT (host -> device) is fed by the embedded IN jacks; bulk IN drains the embedded OUT jacks. The
  // position in the jack list is the USB-MIDI cable number.
  for (int dir = 0; dir < 2; ++dir) {
    const bool out = dir == 0;
    w.u8(9); w.u8(kDescEndpoint); w.u8(out ? kEpMidiOut : kEpMidiIn); w.u8(kXferBulk);
    w.u16(kMidiPacketBytes); w.u8(0); w.u8(0); w.u8(0);
    w.u8(4 + kMidiCables); w.u8(kDescCsEndpoint); w.u8(kMsEndpointGeneral); w.u8(kMidiCables);
    for (unsigned n = 1; n <= kMidiCables; ++n) w.u8(out ? jackInEmb(n) : jackOutEmb(n));
  }
}

// The structure of TinyUSB's full-speed speaker-with-feedback example (examples/device/uac2_speaker_fb, UAC1
// branch), with this firmware's choices: one fixed rate, line-out terminal.
void audioFunction(Writer& w) {
  iad(w, kItfAudioControl, 2);
  interfaceDesc(w, kItfAudioControl, 0, 0, kSubclassControl, kStrAudio);

  // Class-specific AC header: bcdADC 1.00, wTotalLength spans header..last unit, one streaming interface.
  w.u8(kAcHeaderBytes); w.u8(kDescCsInterface); w.u8(kAcHeader);
  w.u16(0x0100);
  w.u16(kUac1AcTotal);
  w.u8(1);
  w.u8(kItfAudioStreaming);

  // Input terminal: USB streaming, stereo front left + right
  w.u8(kInputTermBytes); w.u8(kDescCsInterface); w.u8(kAcInputTerminal); w.u8(kEntityInputTerminal);
  w.u16(kTerminalUsbStreaming); w.u8(0); w.u8(kChannels); w.u16(kChannelsLeftRightFront); w.u8(0); w.u8(0);

  // Output terminal: line out, fed by the feature unit
  w.u8(kOutputTermBytes); w.u8(kDescCsInterface); w.u8(kAcOutputTerminal); w.u8(kEntityOutputTerminal);
  w.u16(kTerminalLineConnector); w.u8(0); w.u8(kEntityFeatureUnit); w.u8(0);

  // Feature unit: mute + volume on the master and on both channels
  w.u8(kFeatureUnitBytes); w.u8(kDescCsInterface); w.u8(kAcFeatureUnit); w.u8(kEntityFeatureUnit);
  w.u8(kEntityInputTerminal); w.u8(kFuControlBytes);
  for (unsigned ch = 0; ch <= kChannels; ++ch) w.u16(kFuMuteAndVolume);
  w.u8(0);

  // Streaming interface: alt 0 has no endpoints (bandwidth 0), alt 1 streams
  interfaceDesc(w, kItfAudioStreaming, 0, 0, kSubclassStreaming, 0);
  interfaceDesc(w, kItfAudioStreaming, 1, 2, kSubclassStreaming, 0);
  w.u8(7); w.u8(kDescCsInterface); w.u8(kAsGeneral); w.u8(kEntityInputTerminal);
  w.u8(0);  // delay
  w.u16(kFormatTagPcm);
  w.u8(8 + 3); w.u8(kDescCsInterface); w.u8(kAsFormatType); w.u8(kFormatTypeI);
  w.u8(kChannels); w.u8(kSubslotBytes); w.u8(kBitResolution);
  w.u8(1);  // one discrete sampling frequency
  w.u24(kSampleRateHz);

  // Isochronous data endpoint: asynchronous, bSynchAddress names the feedback endpoint.
  w.u8(9); w.u8(kDescEndpoint); w.u8(kEpAudioOut); w.u8(kXferIsochronous | kIsoAsync);
  w.u16(kAudioPacketBytes);
  w.u8(1);  // bInterval: every frame
  w.u8(0);  // bRefresh
  w.u8(kEpAudioFeedback);
  w.u8(7); w.u8(kDescCsEndpoint); w.u8(kCsEpGeneral);
  w.u8(kCsEpSamplingFreqControl);
  w.u8(0);  // lock delay units: undefined
  w.u16(0);

  // Synchronisation (feedback) endpoint. bRefresh is 0 although audio 1.0 asks for 1..9: TinyUSB's example sets 0
  // because Windows and macOS schedule the endpoint from bRefresh, Linux from bInterval, and the DWC2 controller
  // can only serve one interval (1 ms).
  w.u8(9); w.u8(kDescEndpoint); w.u8(kEpAudioFeedback); w.u8(kXferIsochronous);
  w.u16(kFeedbackPacketBytes);
  w.u8(1);
  w.u8(0);
  w.u8(0);
}

void emitConfig(Writer& w, bool audio) {
  const unsigned interfaces = audio ? 4 : 2;
  w.u8(9);
  w.u8(kDescConfiguration);
  w.u16(0);  // wTotalLength, patched by the caller once the size is known
  w.u8(interfaces);
  w.u8(1);  // bConfigurationValue
  w.u8(0);
  w.u8(0x80);  // bus powered
  w.u8(kMaxPowerMa / 2);
  midiFunction(w);
  if (audio) audioFunction(w);
}

// out = a + ' ' + b, NUL-terminated. Returns the length, 0 if it does not fit.
size_t joinWithSpace(const char* a, const char* b, char* out, size_t cap) {
  const size_t la = std::strlen(a);
  const size_t lb = std::strlen(b);
  if (out == nullptr || cap < la + 1 + lb + 1) return 0;
  std::memcpy(out, a, la);
  out[la] = ' ';
  std::memcpy(out + la + 1, b, lb + 1);
  return la + 1 + lb;
}

const char kLangId[2] = {0x09, 0x04};  // English (US); esp_tinyusb reads these two bytes as string 0

}  // namespace

size_t buildDeviceDescriptor(bool audio, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u8(kDeviceDescriptorBytes);
  w.u8(kDescDevice);
  w.u16(0x0200);
  w.u8(0xEF);  // Miscellaneous
  w.u8(0x02);  // Common class
  w.u8(0x01);  // Interface Association Descriptor
  w.u8(64);    // EP0 max packet
  w.u16(proto::kUsbVendorId);
  w.u16(proto::kUsbProductId);
  w.u16(audio ? 0x0101 : 0x0100);
  w.u8(kStrManufacturer);
  w.u8(kStrProduct);
  w.u8(kStrSerial);
  w.u8(1);
  return w.fits() ? w.size() : 0;
}

size_t configDescriptorBytes(bool audio) {
  Writer w(nullptr, 0);
  emitConfig(w, audio);
  return w.size();
}

size_t buildConfigDescriptor(bool audio, uint8_t* out, size_t cap) {
  const size_t total = configDescriptorBytes(audio);
  if (out == nullptr || cap < total) return 0;
  Writer w(out, cap);
  emitConfig(w, audio);
  out[2] = static_cast<uint8_t>(total & 0xFF);
  out[3] = static_cast<uint8_t>((total >> 8) & 0xFF);
  return total;
}

size_t formatProductName(uint8_t deck, char* out, size_t cap) {
  return joinWithSpace(proto::kProductName, proto::kDeckNames[deck > 1 ? 1 : deck], out, cap);
}

size_t formatSerial(const uint8_t mac[6], char* out, size_t cap) {
  if (out == nullptr || cap < 13) return 0;
  static const char kHex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < 6; ++i) {
    out[2 * i] = kHex[mac[i] >> 4];
    out[2 * i + 1] = kHex[mac[i] & 0x0F];
  }
  out[12] = '\0';
  return 12;
}

bool buildStrings(uint8_t deck, const uint8_t mac[6], Strings* out) {
  if (out == nullptr || mac == nullptr) return false;
  if (std::strlen(proto::kProductName) > kMaxStringChars) return false;
  for (const char* cable : proto::kCableNames) {
    if (std::strlen(cable) > kMaxStringChars) return false;
  }
  if (formatProductName(deck, out->product, sizeof out->product) == 0) return false;
  if (joinWithSpace(proto::kProductName, "Audio", out->audio, sizeof out->audio) == 0) return false;
  if (formatSerial(mac, out->serial, sizeof out->serial) == 0) return false;

  out->table[kStrLangId] = kLangId;
  out->table[kStrManufacturer] = proto::kProductName;
  out->table[kStrProduct] = out->product;
  out->table[kStrSerial] = out->serial;
  for (unsigned c = 0; c < kMidiCables; ++c) out->table[kStrCable0 + c] = proto::kCableNames[c];
  out->table[kStrAudio] = out->audio;
  return true;
}

}  // namespace usb
}  // namespace epdj
