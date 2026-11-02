// USB Audio Class 2.0 speaker: control requests (clock, mute, volume), stream start/stop, and the interrupt that
// moves each 1 ms packet into the audio ring. The descriptor is epdj/usb_desc.cpp's; TinyUSB's audio driver does
// the transfers.
#include <atomic>
#include <cstring>

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

// Interrupt context, once per USB frame while streaming.
void setFeedback(uint32_t samplesPerFrameQ16) { tud_audio_n_fb_set(0, samplesPerFrameQ16); }

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
  const uint8_t itf = TU_U16_LOW(req->wIndex);
  const uint8_t alt = TU_U16_LOW(req->wValue);
  if (itf == usb::kItfAudioStreaming) {
    g_streaming.store(alt != 0, std::memory_order_release);
    audio_out::setStreaming(alt != 0);
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

extern "C" bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const* req) {
  const uint8_t entity = TU_U16_HIGH(req->wIndex);
  const uint8_t selector = TU_U16_HIGH(req->wValue);
  const uint8_t channel = TU_U16_LOW(req->wValue);

  if (entity == usb::kEntityClockSource) {
    if (selector == AUDIO20_CS_CTRL_SAM_FREQ) {
      if (req->bRequest == AUDIO20_CS_REQ_CUR) {
        audio20_control_cur_4_t cur = {static_cast<int32_t>(tu_htole32(usb::kSampleRateHz))};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &cur, sizeof cur);
      }
      if (req->bRequest == AUDIO20_CS_REQ_RANGE) {
        audio20_control_range_4_n_t(1) range = {};
        range.wNumSubRanges = tu_htole16(1);
        range.subrange[0].bMin = static_cast<int32_t>(usb::kSampleRateHz);
        range.subrange[0].bMax = static_cast<int32_t>(usb::kSampleRateHz);
        range.subrange[0].bRes = 0;
        return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &range, sizeof range);
      }
    } else if (selector == AUDIO20_CS_CTRL_CLK_VALID && req->bRequest == AUDIO20_CS_REQ_CUR) {
      audio20_control_cur_1_t valid = {1};
      return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &valid, sizeof valid);
    }
    return false;
  }

  if (entity == usb::kEntityFeatureUnit && channel <= 2) {
    if (selector == AUDIO20_FU_CTRL_MUTE && req->bRequest == AUDIO20_CS_REQ_CUR) {
      audio20_control_cur_1_t mute = {static_cast<int8_t>(g_fu.mute[channel] ? 1 : 0)};
      return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &mute, sizeof mute);
    }
    if (selector == AUDIO20_FU_CTRL_VOLUME) {
      if (req->bRequest == AUDIO20_CS_REQ_CUR) {
        audio20_control_cur_2_t cur = {static_cast<int16_t>(tu_htole16(static_cast<uint16_t>(g_fu.volume[channel])))};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &cur, sizeof cur);
      }
      if (req->bRequest == AUDIO20_CS_REQ_RANGE) {
        audio20_control_range_2_n_t(1) range = {};
        range.wNumSubRanges = tu_htole16(1);
        range.subrange[0].bMin = static_cast<int16_t>(tu_htole16(static_cast<uint16_t>(kVolMin)));
        range.subrange[0].bMax = static_cast<int16_t>(tu_htole16(static_cast<uint16_t>(kVolMax)));
        range.subrange[0].bRes = static_cast<uint16_t>(tu_htole16(static_cast<uint16_t>(kVolRes)));
        return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &range, sizeof range);
      }
    }
  }
  return false;
}

extern "C" bool tud_audio_set_req_entity_cb(uint8_t, tusb_control_request_t const* req, uint8_t* buf) {
  const uint8_t entity = TU_U16_HIGH(req->wIndex);
  const uint8_t selector = TU_U16_HIGH(req->wValue);
  const uint8_t channel = TU_U16_LOW(req->wValue);
  if (entity != usb::kEntityFeatureUnit || channel > 2 || req->bRequest != AUDIO20_CS_REQ_CUR) return false;  // clock is read-only

  if (selector == AUDIO20_FU_CTRL_MUTE && req->wLength == sizeof(audio20_control_cur_1_t)) {
    g_fu.mute[channel] = buf[0] != 0;
    applyFeatureUnit();
    return true;
  }
  if (selector == AUDIO20_FU_CTRL_VOLUME && req->wLength == sizeof(audio20_control_cur_2_t)) {
    int16_t v;
    std::memcpy(&v, buf, sizeof v);
    g_fu.volume[channel] = v;
    applyFeatureUnit();
    return true;
  }
  return false;
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
