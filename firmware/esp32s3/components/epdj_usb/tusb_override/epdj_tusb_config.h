// TinyUSB configuration for the EP-DJ3000: esp_tinyusb's stock tusb_config.h (class counts from Kconfig) plus
// the USB Audio 1.0 speaker function and larger MIDI FIFOs. Selected through CFG_TUSB_CONFIG_FILE, see CMakeLists.txt.
#pragma once

#include "sdkconfig.h"
#include "tusb_config.h"  // esp_tinyusb's own

#ifdef __cplusplus
extern "C" {
#endif

// ---- MIDI: three cables share one bulk pair; sysex replies need room ----
// TX: a GET_CONFIG dump is 34 SysEx frames = 169 USB-MIDI packets = 676 bytes, which must fit in one go
// (static_assert in usb_device.cpp). RX: host requests are small and one at a time (the app waits for each answer).
#undef CFG_TUD_MIDI_RX_BUFSIZE
#undef CFG_TUD_MIDI_TX_BUFSIZE
#define CFG_TUD_MIDI_RX_BUFSIZE 256
#define CFG_TUD_MIDI_TX_BUFSIZE 1024

// ---- Audio: one USB Audio 1.0 function, host -> device only, stereo 48 kHz 16 bit, asynchronous with feedback ----
// The descriptors (core/src/usb_desc.cpp) make TinyUSB's driver take its UAC1 path, which sends the feedback value
// as three bytes 10.14, what full-speed hosts expect.
#if CONFIG_EPDJ_USB_AUDIO
#define CFG_TUD_AUDIO 1
#define CFG_TUD_AUDIO_ENABLE_EP_OUT 1
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP 1

#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX 2
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX 2
#define CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX 16

// (48000 / 1000 + 1 sample) * 2 channels * 2 bytes: must equal epdj::usb::kAudioPacketBytes (static_assert in
// usb_device.cpp). The driver's software FIFO holds a few packets; the interrupt drains it into our ring.
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX 196
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ (4 * 196)
#else
#define CFG_TUD_AUDIO 0
#endif

#ifdef __cplusplus
}
#endif
