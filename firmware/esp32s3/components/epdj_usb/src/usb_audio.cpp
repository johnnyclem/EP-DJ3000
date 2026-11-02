// USB Audio Class 1.0 speaker: control requests (mute, volume, sampling frequency), stream start/stop, the feedback
// value, and the interrupt that moves each 1 ms packet into the audio ring. The descriptor is epdj/usb_desc.cpp's;
// TinyUSB's audio driver does the transfers and sends the feedback value as three bytes 10.14.
#include <atomic>
#include <cstring>

#include "epdj/audio_rate.h"
#include "epdj/usb_desc.h"
#include "epdj_usb/usb_device.h"
#include "sdkconfig.h"
#include "tusb.h"
#include "usb_internal.h"

#if CONFIG_EPDJ_USB_AUDIO

#include "epdj_audio/audio_out.h"

namespace epdj {
namespace usb_dev {
namespace {

std::atomic<bool> g_streaming{false};

// Feature unit state: index 0 = master, 1 = left, 2 = right.
struct FeatureUnit {
  bool mute[3] = {false, false, false};
  int16_t volume[3] = {0, 0, 0};  // 1/256 dB, 0 = unity
};
FeatureUnit g_fu;

// What the DAC applies. The DAC path has one stereo gain, so the two channel trims are averaged and added to the
// master (hosts that only use the master, which is nearly all of them, are exact).
void applyFeatureUnit() {
  int32_t v = g_fu.volume[0] + (g_fu.volume[1] + g_fu.volume[2]) / 2;
  if (v > 0) v = 0;  // advertised range tops out at unity: no boost
  if (v < INT16_MIN + 1) v = INT16_MIN + 1;
  const bool mute = g_fu.mute[0] || (g_fu.mute[1] && g_fu.mute[2]);
  audio_out::setVolume(static_cast<int16_t>(v), mute);
}

constexpr int16_t kVolMin = -80 * 256;  // -80 dB
constexpr int16_t kVolMax = 0;
constexpr int16_t kVolRes = 256;        // 1 dB

constexpr uint32_t kNominalQ16 = (usb::kSampleRateHz / 1000u) << 16;  // 48.0 samples per frame

// Once per USB frame while streaming (audio task), and once at stream start (TinyUSB task). TinyUSB's UAC1 path
// sends (value >> 2) as three bytes, i.e. 10.14; handing it a value that is already a multiple of 4 makes that
// shift exact, so the bytes on the wire are audio::FeedbackController::encodeUsb(q16) (rounded, saturating).
void setFeedback(uint32_t samplesPerFrameQ16) {
  tud_audio_n_fb_set(0, audio::FeedbackController::toUsb10_14(samplesPerFrameQ16) << 2);
}

}  // namespace

namespace detail {
void audioOnDetach() {
  g_streaming.store(false, std::memory_order_release);
  audio_out::setStreaming(false);
}
bool audioIsStreaming() { return g_streaming.load(std::memory_order_acquire); }
}  // namespace detail

FeedbackFn audioFeedbackSink() { return setFeedback; }

}  // namespace usb_dev
}  // namespace epdj

using namespace epdj;
using namespace epdj::usb_dev;

// ---- stream control (TinyUSB task) ----

extern "C" bool tud_audio_set_itf_cb(uint8_t, tusb_control_request_t const* req) {
  if (TU_U16_LOW(req->wIndex) == usb::kItfAudioStreaming) {
    const bool streaming = TU_U16_LOW(req->wValue) != 0;
    // TinyUSB has already queued the first feedback packet, with value 0; every packet after it carries this
    // until the audio task has a measurement.
    if (streaming) setFeedback(kNominalQ16);
    g_streaming.store(streaming, std::memory_order_release);
    audio_out::setStreaming(streaming);
  }
  return true;
}

extern "C" bool tud_audio_set_itf_close_ep_cb(uint8_t, tusb_control_request_t const* req) {
  if (TU_U16_LOW(req->wIndex) == usb::kItfAudioStreaming) {
    g_streaming.store(false, std::memory_order_release);
    audio_out::setStreaming(false);
  }
  return true;
}

// The feedback value is pushed by the audio task through tud_audio_n_fb_set (manual method), so the driver
// needs no SOF interrupt.
extern "C" void tud_audio_feedback_params_cb(uint8_t, uint8_t, audio_feedback_params_t* p) {
  p->method = AUDIO_FEEDBACK_METHOD_DISABLED;
  p->sample_freq = usb::kSampleRateHz;
}

// ---- audio data (USB interrupt) ----

extern "C" bool tud_audio_rx_done_isr(uint8_t, uint16_t, uint8_t, uint8_t, uint8_t) {
  alignas(4) int16_t pcm[usb::kAudioPacketBytes / sizeof(int16_t)];
  // Whole frames only: a stray odd sample would swap left and right for the rest of the stream.
  uint16_t avail = tud_audio_available();
  if (avail > sizeof pcm) avail = static_cast<uint16_t>(sizeof pcm);
  avail = static_cast<uint16_t>(avail & ~3u);
  if (avail == 0) return true;
  const uint16_t got = tud_audio_read(pcm, avail);
  const size_t frames = got / 4u;
  audio_out::pushFromUsb(pcm, frames);
  detail::g_audioFrames.fetch_add(static_cast<uint32_t>(frames), std::memory_order_relaxed);
  return true;
}

// ---- control requests (TinyUSB task) ----
// Audio 1.0: wValue = (control selector << 8) | channel (0 = master), wIndex = (unit id << 8) | interface.

extern "C" bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const* req) {
  const uint8_t entity = TU_U16_HIGH(req->wIndex);
  const uint8_t selector = TU_U16_HIGH(req->wValue);
  const uint8_t channel = TU_U16_LOW(req->wValue);
  if (entity != usb::kEntityFeatureUnit || channel > 2) return false;

  if (selector == AUDIO10_FU_CTRL_MUTE && req->bRequest == AUDIO10_CS_REQ_GET_CUR) {
    uint8_t mute = g_fu.mute[channel] ? 1 : 0;
    return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &mute, sizeof mute);
  }
  if (selector == AUDIO10_FU_CTRL_VOLUME) {
    int16_t v;
    switch (req->bRequest) {
      case AUDIO10_CS_REQ_GET_CUR: v = g_fu.volume[channel]; break;
      case AUDIO10_CS_REQ_GET_MIN: v = kVolMin; break;
      case AUDIO10_CS_REQ_GET_MAX: v = kVolMax; break;
      case AUDIO10_CS_REQ_GET_RES: v = kVolRes; break;
      default: return false;
    }
    int16_t le = static_cast<int16_t>(tu_htole16(static_cast<uint16_t>(v)));
    return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &le, sizeof le);
  }
  return false;
}

extern "C" bool tud_audio_set_req_entity_cb(uint8_t, tusb_control_request_t const* req, uint8_t* buf) {
  const uint8_t entity = TU_U16_HIGH(req->wIndex);
  const uint8_t selector = TU_U16_HIGH(req->wValue);
  const uint8_t channel = TU_U16_LOW(req->wValue);
  if (entity != usb::kEntityFeatureUnit || channel > 2 || req->bRequest != AUDIO10_CS_REQ_SET_CUR) return false;

  if (selector == AUDIO10_FU_CTRL_MUTE && req->wLength == 1) {
    g_fu.mute[channel] = buf[0] != 0;
    applyFeatureUnit();
    return true;
  }
  if (selector == AUDIO10_FU_CTRL_VOLUME && req->wLength == 2) {
    uint16_t raw;
    std::memcpy(&raw, buf, sizeof raw);
    g_fu.volume[channel] = static_cast<int16_t>(tu_le16toh(raw));
    applyFeatureUnit();
    return true;
  }
  return false;
}

// The data endpoint advertises a sampling frequency control (as TinyUSB's example does, and some hosts send the
// request whenever it is there). There is one rate, so the only valid value is that one.
extern "C" bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const* req) {
  if (TU_U16_HIGH(req->wValue) != AUDIO10_EP_CTRL_SAMPLING_FREQ || req->bRequest != AUDIO10_CS_REQ_GET_CUR) return false;
  uint8_t freq[3] = {static_cast<uint8_t>(usb::kSampleRateHz & 0xFFu), static_cast<uint8_t>((usb::kSampleRateHz >> 8) & 0xFFu),
                     static_cast<uint8_t>((usb::kSampleRateHz >> 16) & 0xFFu)};
  return tud_audio_buffer_and_schedule_control_xfer(rhport, req, freq, sizeof freq);
}

extern "C" bool tud_audio_set_req_ep_cb(uint8_t, tusb_control_request_t const* req, uint8_t* buf) {
  if (TU_U16_HIGH(req->wValue) != AUDIO10_EP_CTRL_SAMPLING_FREQ || req->bRequest != AUDIO10_CS_REQ_SET_CUR || req->wLength != 3) {
    return false;
  }
  const uint32_t rate = static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) | (static_cast<uint32_t>(buf[2]) << 16);
  return rate == usb::kSampleRateHz;
}

#else  // !CONFIG_EPDJ_USB_AUDIO

namespace epdj {
namespace usb_dev {
namespace detail {
void audioOnDetach() {}
bool audioIsStreaming() { return false; }
}  // namespace detail
FeedbackFn audioFeedbackSink() { return nullptr; }
}  // namespace usb_dev
}  // namespace epdj

#endif
