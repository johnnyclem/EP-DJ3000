// Compares the pure descriptor builder (firmware/core/src/usb_desc.cpp) with the same functions written out
// with TinyUSB's own TUD_* descriptor macros, byte for byte. Run by run.sh against the TinyUSB that
// esp_tinyusb actually ships.
#include <cstdio>
#include <cstring>

#include "epdj/usb_desc.h"
#include "tusb.h"

using namespace epdj::usb;

#define MIDI_FUNCTION                                                                                          \
  /* IAD for the AudioControl + MIDIStreaming pair; MIDI has no audio-class protocol version */               \
  8, TUSB_DESC_INTERFACE_ASSOCIATION, kItfMidiControl, 2, TUSB_CLASS_AUDIO, AUDIO_FUNCTION_SUBCLASS_UNDEFINED, \
      AUDIO_FUNC_PROTOCOL_CODE_UNDEF, 0,                                                                      \
      TUD_MIDI_DESC_HEAD(kItfMidiControl, 0, 3),                                                              \
      TUD_MIDI_DESC_JACK_DESC(1, kStrCable0 + 0), TUD_MIDI_DESC_JACK_DESC(2, kStrCable0 + 1),                 \
      TUD_MIDI_DESC_JACK_DESC(3, kStrCable0 + 2),                                                             \
      TUD_MIDI_DESC_EP(kEpMidiOut, kMidiPacketBytes, 3),                                                      \
      TUD_MIDI_JACKID_IN_EMB(1), TUD_MIDI_JACKID_IN_EMB(2), TUD_MIDI_JACKID_IN_EMB(3),                        \
      TUD_MIDI_DESC_EP(kEpMidiIn, kMidiPacketBytes, 3),                                                       \
      TUD_MIDI_JACKID_OUT_EMB(1), TUD_MIDI_JACKID_OUT_EMB(2), TUD_MIDI_JACKID_OUT_EMB(3)

#define AUDIO_LEN                                                                                              \
  (TUD_AUDIO20_DESC_IAD_LEN + TUD_AUDIO20_DESC_STD_AC_LEN + TUD_AUDIO20_DESC_CS_AC_LEN +                       \
   TUD_AUDIO20_DESC_CLK_SRC_LEN + TUD_AUDIO20_DESC_INPUT_TERM_LEN + TUD_AUDIO20_DESC_OUTPUT_TERM_LEN +        \
   TUD_AUDIO20_DESC_FEATURE_UNIT_LEN(2) + TUD_AUDIO20_DESC_STD_AS_LEN + TUD_AUDIO20_DESC_STD_AS_LEN +         \
   TUD_AUDIO20_DESC_CS_AS_INT_LEN + TUD_AUDIO20_DESC_TYPE_I_FORMAT_LEN + TUD_AUDIO20_DESC_STD_AS_ISO_EP_LEN + \
   TUD_AUDIO20_DESC_CS_AS_ISO_EP_LEN + TUD_AUDIO20_DESC_STD_AS_ISO_FB_EP_LEN)

// The structure of TinyUSB's uac2_speaker_fb example, with this firmware's choices: fixed read-only clock,
// line-out terminal, bInterval 1 feedback endpoint.
#define AUDIO_FUNCTION                                                                                         \
  TUD_AUDIO20_DESC_IAD(kItfAudioControl, 2, 0),                                                                \
  TUD_AUDIO20_DESC_STD_AC(kItfAudioControl, 0, kStrAudio),                                                     \
  TUD_AUDIO20_DESC_CS_AC(0x0200, AUDIO20_FUNC_DESKTOP_SPEAKER,                                                 \
                         TUD_AUDIO20_DESC_CLK_SRC_LEN + TUD_AUDIO20_DESC_INPUT_TERM_LEN +                      \
                             TUD_AUDIO20_DESC_OUTPUT_TERM_LEN + TUD_AUDIO20_DESC_FEATURE_UNIT_LEN(2),          \
                         AUDIO20_CS_AS_INTERFACE_CTRL_LATENCY_POS),                                            \
  TUD_AUDIO20_DESC_CLK_SRC(kEntityClockSource, AUDIO20_CLOCK_SOURCE_ATT_INT_FIX_CLK,                           \
                           (AUDIO20_CTRL_R << AUDIO20_CLOCK_SOURCE_CTRL_CLK_FRQ_POS), kEntityInputTerminal, 0), \
  TUD_AUDIO20_DESC_INPUT_TERM(kEntityInputTerminal, AUDIO_TERM_TYPE_USB_STREAMING, 0, kEntityClockSource,      \
                              kChannels, AUDIO20_CHANNEL_CONFIG_NON_PREDEFINED, 0, 0, 0),                      \
  TUD_AUDIO20_DESC_OUTPUT_TERM(kEntityOutputTerminal, kTerminalLineConnector, kEntityInputTerminal,            \
                               kEntityFeatureUnit, kEntityClockSource, 0x0000, 0),                             \
  TUD_AUDIO20_DESC_FEATURE_UNIT(                                                                               \
      kEntityFeatureUnit, kEntityInputTerminal, 0,                                                             \
      AUDIO20_CTRL_RW << AUDIO20_FEATURE_UNIT_CTRL_MUTE_POS | AUDIO20_CTRL_RW << AUDIO20_FEATURE_UNIT_CTRL_VOLUME_POS, \
      AUDIO20_CTRL_RW << AUDIO20_FEATURE_UNIT_CTRL_MUTE_POS | AUDIO20_CTRL_RW << AUDIO20_FEATURE_UNIT_CTRL_VOLUME_POS, \
      AUDIO20_CTRL_RW << AUDIO20_FEATURE_UNIT_CTRL_MUTE_POS | AUDIO20_CTRL_RW << AUDIO20_FEATURE_UNIT_CTRL_VOLUME_POS), \
  TUD_AUDIO20_DESC_STD_AS_INT(kItfAudioStreaming, 0, 0, 0),                                                    \
  TUD_AUDIO20_DESC_STD_AS_INT(kItfAudioStreaming, 1, 2, 0),                                                    \
  TUD_AUDIO20_DESC_CS_AS_INT(kEntityInputTerminal, AUDIO20_CTRL_NONE, AUDIO20_FORMAT_TYPE_I,                   \
                             AUDIO20_DATA_FORMAT_TYPE_I_PCM, kChannels, AUDIO20_CHANNEL_CONFIG_NON_PREDEFINED, 0), \
  TUD_AUDIO20_DESC_TYPE_I_FORMAT(kSubslotBytes, kBitResolution),                                               \
  TUD_AUDIO20_DESC_STD_AS_ISO_EP(kEpAudioOut,                                                                  \
                                 (uint8_t)((uint8_t)TUSB_XFER_ISOCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS | \
                                           (uint8_t)TUSB_ISO_EP_ATT_DATA),                                     \
                                 kAudioPacketBytes, 0x01),                                                     \
  TUD_AUDIO20_DESC_CS_AS_ISO_EP(AUDIO20_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, AUDIO20_CTRL_NONE,           \
                                AUDIO20_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC, 0x0001),                   \
  TUD_AUDIO20_DESC_STD_AS_ISO_FB_EP(kEpAudioFeedback, kFeedbackPacketBytes, 1)

static const uint8_t kRefMidiOnly[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_HEAD_LEN + 3 * TUD_MIDI_DESC_JACK_LEN +
                                       2 * TUD_MIDI_DESC_EP_LEN(3) + 8,
                          0, kMaxPowerMa),
    MIDI_FUNCTION};
static const uint8_t kRefWithAudio[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0, TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_HEAD_LEN + 3 * TUD_MIDI_DESC_JACK_LEN +
                                       2 * TUD_MIDI_DESC_EP_LEN(3) + 8 + AUDIO_LEN,
                          0, kMaxPowerMa),
    MIDI_FUNCTION, AUDIO_FUNCTION};

static int compare(const char* what, const uint8_t* want, size_t wantLen, bool audio) {
  uint8_t got[kMaxConfigDescriptorBytes];
  const size_t n = buildConfigDescriptor(audio, got, sizeof got);
  int bad = 0;
  if (n != wantLen) {
    std::printf("FAIL %s: builder %zu bytes, TinyUSB macros %zu bytes\n", what, n, wantLen);
    bad = 1;
  }
  for (size_t i = 0; i < n && i < wantLen; ++i) {
    if (got[i] != want[i]) {
      std::printf("FAIL %s: first difference at byte %zu: builder 0x%02X, TinyUSB 0x%02X\n", what, i, got[i], want[i]);
      bad = 1;
      break;
    }
  }
  if (!bad) std::printf("ok   %s: %zu bytes identical\n", what, n);
  return bad;
}

int main() {
  int bad = 0;
  bad |= compare("config, MIDI only", kRefMidiOnly, sizeof kRefMidiOnly, false);
  bad |= compare("config, MIDI + UAC2 speaker", kRefWithAudio, sizeof kRefWithAudio, true);

  // Device descriptor against TinyUSB's struct layout.
  uint8_t dev[kDeviceDescriptorBytes];
  buildDeviceDescriptor(true, dev, sizeof dev);
  tusb_desc_device_t d;
  static_assert(sizeof d == kDeviceDescriptorBytes, "tusb_desc_device_t size");
  std::memcpy(&d, dev, sizeof d);
  const bool devOk = d.bDescriptorType == TUSB_DESC_DEVICE && d.bDeviceClass == TUSB_CLASS_MISC &&
                     d.bDeviceSubClass == MISC_SUBCLASS_COMMON && d.bDeviceProtocol == MISC_PROTOCOL_IAD &&
                     d.bMaxPacketSize0 == CFG_TUD_ENDPOINT0_SIZE && d.idVendor == kVendorId &&
                     d.idProduct == kProductId && d.bNumConfigurations == 1 && d.bcdUSB == 0x0200;
  std::printf("%s device descriptor fields match tusb_desc_device_t\n", devOk ? "ok  " : "FAIL");
  bad |= devOk ? 0 : 1;
  return bad;
}
