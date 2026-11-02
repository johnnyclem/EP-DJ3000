// Runs TinyUSB's real audio and MIDI class drivers (audio_device.c, midi_device.c, compiled for the host) on the
// builder's descriptors, together with this firmware's own audio callbacks (components/epdj_usb/src/usb_audio.cpp,
// unmodified). Only the layer below the class drivers (usbd_edpt_*, control transfers) is replaced by recorders.
//
// What it shows, with no USB hardware:
//   * the driver claims the audio function and leaves the MIDI function to the MIDI driver;
//   * selecting alternate setting 1 opens 0x01 (196 B) and 0x81 (3 B) and queues the first feedback packet;
//   * the feedback bytes on the wire are the 3 bytes 10.14 FeedbackController::encodeUsb() gives, for any value;
//   * host audio packets reach audio_out::pushFromUsb() sample for sample;
//   * the class requests a host sends (mute, volume, sampling frequency) reach the firmware's handlers through the
//     driver's own entity routing, and unsupported ones stall;
//   * the MIDI driver takes the MIDI function, and a full GET_CONFIG dump (what ConfigServer really produces) fits
//     its TX FIFO in one go and comes out of the endpoint byte for byte.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

#include "device/usbd_pvt.h"
#include "epdj/audio_rate.h"
#include "epdj/config_server.h"
#include "epdj/midi.h"
#include "epdj/sysex.h"
#include "epdj/usb_desc.h"
#include "epdj_audio/audio_out.h"
#include "epdj_usb/usb_device.h"
#include "tusb.h"
#include "usb_internal.h"

using namespace epdj;
using Bytes = std::vector<uint8_t>;

// ---- recorders for what the firmware's callbacks do to the rest of the system ----

namespace epdj {
namespace usb_dev {
namespace detail {
std::atomic<uint32_t> g_audioFrames{0};
}
}  // namespace usb_dev

namespace audio_out {
struct Recorded {
  bool streaming = false;
  int16_t volume = 0;
  bool mute = false;
  int volumeCalls = 0;
  std::vector<int16_t> pcm;
} g_ao;
void setStreaming(bool s) { g_ao.streaming = s; }
void setVolume(int16_t v, bool m) {
  g_ao.volume = v;
  g_ao.mute = m;
  ++g_ao.volumeCalls;
}
size_t pushFromUsb(const int16_t* p, size_t frames) {
  g_ao.pcm.assign(p, p + frames * 2);
  return frames;
}
}  // namespace audio_out
}  // namespace epdj

// ---- the layer below the class driver ----

namespace {
struct Xfer {
  uint8_t ep;
  uint8_t* buf;
  uint16_t len;
  Bytes data;  // IN transfers: the bytes at the moment the transfer was queued (the driver reuses its buffer)
};
struct Iso {
  uint8_t ep;
  uint16_t size;
};
std::vector<Xfer> g_xfers;
bool g_epBusy[256];  // endpoint claimed or transfer in flight: usbd_edpt_claim() refuses until it completes
std::vector<Iso> g_isoActivated;
std::vector<uint8_t> g_isoAllocated;
int g_xferFifoCalls = 0;
bool g_sofEnabled = false;
int g_controlStatus = 0;
struct ControlData {
  uint8_t* buf = nullptr;
  uint16_t len = 0;
} g_control;
alignas(4) uint8_t g_ep0Buffer[64];
}  // namespace

extern "C" {
bool usbd_edpt_xfer(uint8_t, uint8_t ep, uint8_t* buf, uint16_t len, bool) {
  Xfer x{ep, buf, len, {}};
  if ((ep & 0x80) != 0) x.data.assign(buf, buf + len);
  g_xfers.push_back(x);
  return true;
}
bool usbd_edpt_open(uint8_t, tusb_desc_endpoint_t const*) { return true; }
bool usbd_edpt_claim(uint8_t, uint8_t ep) {
  if (g_epBusy[ep]) return false;
  g_epBusy[ep] = true;
  return true;
}
bool usbd_edpt_release(uint8_t, uint8_t ep) {
  g_epBusy[ep] = false;
  return true;
}
// Pulled in by tusb.c, which is only compiled for its endpoint-stream helpers.
void dcd_int_handler(uint8_t) {}
bool tud_deinit(uint8_t) { return true; }
bool tud_inited(void) { return false; }
bool tud_rhport_init(uint8_t, tusb_rhport_init_t const*) { return true; }
uint32_t tusb_time_millis_api(void) { return 0; }
bool usbd_edpt_xfer_fifo(uint8_t, uint8_t, tu_fifo_t*, uint16_t, bool) {
  ++g_xferFifoCalls;
  return true;
}
void usbd_edpt_clear_stall(uint8_t, uint8_t) {}
bool usbd_edpt_iso_alloc(uint8_t, uint8_t ep, uint16_t) {
  g_isoAllocated.push_back(ep);
  return true;
}
bool usbd_edpt_iso_activate(uint8_t, tusb_desc_endpoint_t const* d) {
  g_isoActivated.push_back({d->bEndpointAddress, tu_edpt_packet_size(d)});
  return true;
}
uint8_t* usbd_get_ctrl_buf(void) { return g_ep0Buffer; }
void usbd_sof_enable(uint8_t, sof_consumer_t, bool en) { g_sofEnabled = en; }
bool tud_control_xfer(uint8_t, tusb_control_request_t const*, void* buf, uint16_t len) {
  g_control.buf = static_cast<uint8_t*>(buf);
  g_control.len = len;
  return true;
}
bool tud_control_status(uint8_t, tusb_control_request_t const*) {
  ++g_controlStatus;
  return true;
}
tusb_speed_t tud_speed_get(void) { return TUSB_SPEED_FULL; }
}

// ---- tiny test harness ----

static int g_failed = 0;
static int g_checks = 0;
#define CHECK(cond, ...)                                           \
  do {                                                             \
    ++g_checks;                                                    \
    if (!(cond)) {                                                 \
      ++g_failed;                                                  \
      std::printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);  \
      std::printf(__VA_ARGS__);                                    \
      std::printf("\n");                                           \
    }                                                              \
  } while (0)

static tusb_control_request_t makeReq(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t length) {
  tusb_control_request_t r;
  std::memset(&r, 0, sizeof r);
  r.bmRequestType = type;
  r.bRequest = request;
  r.wValue = value;
  r.wIndex = index;
  r.wLength = length;
  return r;
}

// Class request, device -> host. False = the device stalled; otherwise `out` holds the data stage.
static bool hostGet(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t length, Bytes* out) {
  g_control = {};
  tusb_control_request_t r = makeReq(type, request, value, index, length);
  if (!audiod_control_xfer_cb(0, CONTROL_STAGE_SETUP, &r) || g_control.buf == nullptr) return false;
  out->assign(g_control.buf, g_control.buf + g_control.len);
  return true;
}

// Class request, host -> device: SETUP stage, then the data stage with `data`. False = stalled.
static bool hostSet(uint8_t type, uint8_t request, uint16_t value, uint16_t index, const Bytes& data) {
  g_control = {};
  tusb_control_request_t r = makeReq(type, request, value, index, static_cast<uint16_t>(data.size()));
  if (!audiod_control_xfer_cb(0, CONTROL_STAGE_SETUP, &r) || g_control.buf == nullptr) return false;
  std::memcpy(g_control.buf, data.data(), data.size());
  return audiod_control_xfer_cb(0, CONTROL_STAGE_DATA, &r);
}

static bool setInterface(uint8_t itf, uint8_t alt) {
  g_xfers.clear();
  g_isoActivated.clear();
  tusb_control_request_t r = makeReq(0x01, TUSB_REQ_SET_INTERFACE, alt, itf, 0);
  return audiod_control_xfer_cb(0, CONTROL_STAGE_SETUP, &r);
}

static const Xfer* lastXfer(uint8_t ep) {
  for (size_t i = g_xfers.size(); i-- > 0;)
    if (g_xfers[i].ep == ep) return &g_xfers[i];
  return nullptr;
}

constexpr uint8_t kClassIfaceIn = 0xA1, kClassIfaceOut = 0x21, kClassEpIn = 0xA2, kClassEpOut = 0x22;
constexpr uint8_t kGetCur = 0x81, kGetMin = 0x82, kGetMax = 0x83, kGetRes = 0x84, kSetCur = 0x01;
constexpr uint8_t kMute = 1, kVolume = 2, kSamplingFreq = 1;
constexpr uint16_t kUnit = epdj::usb::kEntityFeatureUnit << 8 | epdj::usb::kItfAudioControl;

static uint16_t le16(const Bytes& b) { return static_cast<uint16_t>(b[0] | (b[1] << 8)); }

// The pieces ConfigServer needs to answer a request: nothing here is looked at except the frames it produces.
namespace {
struct FrameSink final : ReplySink {
  std::vector<Bytes> frames;
  bool send(const uint8_t* f, size_t n) override {
    frames.emplace_back(f, f + n);
    return true;
  }
};
struct NullActions final : DeviceActions {
  void reboot(proto::RebootMode) override {}
  void setDeck(proto::Deck) override {}
  bool testTone(bool, uint16_t, uint8_t) override { return false; }
  void identify(uint8_t) override {}
  bool saveConfig(const Config&) override { return true; }
  void getInfo(proto::Info*) override {}
  void getTelemetry(proto::Telemetry*) override {}
  size_t getPeers(proto::Peer*, size_t) override { return 0; }
};
}  // namespace

int main() {
  using namespace epdj::usb;
  Bytes cfg(kMaxConfigDescriptorBytes);
  cfg.resize(buildConfigDescriptor(true, cfg.data(), cfg.size()));
  std::printf("config descriptor: %zu bytes\n", cfg.size());

  // Interface descriptors by position: IAD (8) follows the 9-byte configuration header; find the audio function's.
  size_t midiAc = 9 + 8, audioAc = 0;
  for (size_t off = 9; off < cfg.size(); off += cfg[off]) {
    if (cfg[off + 1] == TUSB_DESC_INTERFACE_ASSOCIATION && cfg[off + 2] == kItfAudioControl) audioAc = off + 8;
  }
  CHECK(audioAc > midiAc, "audio function not found");

  // ---- the driver takes the audio function and leaves MIDI alone ----
  audiod_init();
  audiod_reset(0);
  const uint16_t midiClaimed = audiod_open(0, reinterpret_cast<const tusb_desc_interface_t*>(&cfg[midiAc]),
                                           static_cast<uint16_t>(cfg.size() - midiAc));
  CHECK(midiClaimed == 0, "audio driver claimed the MIDI AudioControl interface (%u bytes)", midiClaimed);
  const uint16_t audioClaimed = audiod_open(0, reinterpret_cast<const tusb_desc_interface_t*>(&cfg[audioAc]),
                                            static_cast<uint16_t>(cfg.size() - audioAc));
  CHECK(audioClaimed == cfg.size() - audioAc, "claimed %u bytes, the audio function is %zu", audioClaimed, cfg.size() - audioAc);
  CHECK(tud_audio_n_version(0) == 1, "driver sees audio class version %u", tud_audio_n_version(0));
  CHECK(g_isoAllocated.size() == 2, "iso endpoints allocated: %zu", g_isoAllocated.size());
  std::printf("ok   driver claims the audio function (%u bytes, UAC%u), leaves MIDI to the MIDI driver\n", audioClaimed,
              tud_audio_n_version(0));

  // ---- alternate setting 0 -> 1: endpoints, first feedback packet, firmware callbacks ----
  CHECK(setInterface(kItfAudioStreaming, 1), "SET_INTERFACE alt 1 refused");
  CHECK(g_xferFifoCalls == 0, "driver used the hardware FIFO path");
  bool out = false, fb = false;
  for (const Iso& i : g_isoActivated) {
    if (i.ep == kEpAudioOut) { out = true; CHECK(i.size == kAudioPacketBytes, "OUT size %u", i.size); }
    if (i.ep == kEpAudioFeedback) { fb = true; CHECK(i.size == kFeedbackPacketBytes, "feedback size %u", i.size); }
  }
  CHECK(out && fb && g_isoActivated.size() == 2, "opened %zu endpoints", g_isoActivated.size());
  const Xfer* firstFb = lastXfer(kEpAudioFeedback);
  CHECK(firstFb != nullptr && firstFb->len == 3, "first feedback transfer: %s, %u bytes", firstFb ? "queued" : "missing",
        firstFb ? firstFb->len : 0u);
  if (firstFb != nullptr) {
    CHECK(firstFb->buf[0] == 0 && firstFb->buf[1] == 0 && firstFb->buf[2] == 0,
          "first feedback packet is %02X %02X %02X (documented: the driver sends 0 before the firmware can set a value)",
          firstFb->buf[0], firstFb->buf[1], firstFb->buf[2]);
  }
  const Xfer* firstOut = lastXfer(kEpAudioOut);
  CHECK(firstOut != nullptr && firstOut->len == kAudioPacketBytes, "first OUT transfer");
  CHECK(!g_sofEnabled, "SOF interrupt enabled although the feedback value is set by the firmware");
  CHECK(audio_out::g_ao.streaming, "firmware was not told the stream started");
  CHECK(g_controlStatus > 0, "SET_INTERFACE was not completed with a status stage");
  std::printf("ok   alt 1: 0x01 (%u B) + 0x81 (%u B) open, first feedback queued, firmware told streaming\n",
              kAudioPacketBytes, kFeedbackPacketBytes);

  // ---- feedback bytes on the wire ----
  usb_dev::FeedbackFn sink = usb_dev::audioFeedbackSink();
  CHECK(sink != nullptr, "no feedback sink");
  // set_itf_cb primed the nominal value: the next packet after the first one carries 48.0 samples/frame
  g_xfers.clear();
  CHECK(audiod_xfer_isr(0, kEpAudioFeedback, XFER_RESULT_SUCCESS, 3), "feedback completion refused");
  const Xfer* second = lastXfer(kEpAudioFeedback);
  CHECK(second != nullptr, "feedback not re-queued");
  if (second != nullptr) {
    // 48.0 in 10.14 = 48 << 14 = 0x0C0000 -> bytes 00 00 0C
    CHECK(second->buf[0] == 0x00 && second->buf[1] == 0x00 && second->buf[2] == 0x0C,
          "second feedback packet is %02X %02X %02X, expected nominal 00 00 0C", second->buf[0], second->buf[1], second->buf[2]);
  }
  uint32_t values[] = {0x2F0000u, 0x2FFFFFu, 0x300000u, 0x300001u, 0x300002u, 0x300003u, 0x300083u, 0x3000FFu, 0x30028Fu,
                       0x2FFF7Du, 0x2F5C29u, 0x30A3D7u, 0x30D000u, 0x2F3000u, 0x7FFFFFFFu, 0xFFFFFFFFu, 0u, 1u, 2u, 3u};
  unsigned checked = 0, mismatches = 0;
  auto sendAndRead = [&](uint32_t q16) {
    sink(q16);
    g_xfers.clear();
    audiod_xfer_isr(0, kEpAudioFeedback, XFER_RESULT_SUCCESS, 3);
    const Xfer* x = lastXfer(kEpAudioFeedback);
    uint8_t want[3];
    audio::FeedbackController::encodeUsb(q16, want);
    ++checked;
    if (x == nullptr || x->len != 3 || std::memcmp(x->buf, want, 3) != 0) {
      ++mismatches;
      if (mismatches < 5)
        std::printf("     q16 0x%08X: wire %02X %02X %02X, encodeUsb %02X %02X %02X\n", q16, x ? x->buf[0] : 0, x ? x->buf[1] : 0,
                    x ? x->buf[2] : 0, want[0], want[1], want[2]);
    }
  };
  for (uint32_t v : values) sendAndRead(v);
  uint32_t seed = 12345;
  for (int i = 0; i < 20000; ++i) {  // +-1 % of nominal, the controller's whole output range
    seed = seed * 1664525u + 1013904223u;
    sendAndRead(0x2F5C29u + (seed >> 8) % (0x30A3D7u - 0x2F5C29u + 1u));
  }
  CHECK(mismatches == 0, "%u of %u feedback values differ from FeedbackController::encodeUsb", mismatches, checked);
  std::printf("%s   feedback on the wire == encodeUsb (3 bytes 10.14) for %u values, incl. rounding and saturation\n",
              mismatches == 0 ? "ok  " : "FAIL", checked);

  // ---- audio data: OUT packet -> driver FIFO -> firmware -> ring ----
  CHECK(setInterface(kItfAudioStreaming, 1), "SET_INTERFACE alt 1 (again) refused");  // a fresh stream
  firstOut = lastXfer(kEpAudioOut);
  CHECK(firstOut != nullptr, "OUT endpoint not armed");
  if (firstOut != nullptr) {
    int16_t src[kAudioPacketBytes / 2];
    for (size_t i = 0; i < sizeof src / sizeof src[0]; ++i) src[i] = static_cast<int16_t>(1000 + 3 * i);
    std::memcpy(firstOut->buf, src, sizeof src);
    g_xfers.clear();
    audio_out::g_ao.pcm.clear();
    usb_dev::detail::g_audioFrames = 0;
    CHECK(audiod_xfer_isr(0, kEpAudioOut, XFER_RESULT_SUCCESS, kAudioPacketBytes), "OUT completion refused");
    CHECK(lastXfer(kEpAudioOut) != nullptr, "OUT endpoint not re-armed after a full packet");
    const size_t frames = kAudioPacketBytes / 4;
    CHECK(audio_out::g_ao.pcm.size() == frames * 2, "pushed %zu samples", audio_out::g_ao.pcm.size());
    if (audio_out::g_ao.pcm.size() == frames * 2) CHECK(std::memcmp(audio_out::g_ao.pcm.data(), src, sizeof src) == 0, "samples differ");
    CHECK(usb_dev::detail::g_audioFrames == frames, "frame counter %u", usb_dev::detail::g_audioFrames.load());
    // A nominal 48-frame packet (192 bytes), the usual one
    audio_out::g_ao.pcm.clear();
    CHECK(audiod_xfer_isr(0, kEpAudioOut, XFER_RESULT_SUCCESS, 192), "192-byte completion refused");
    CHECK(audio_out::g_ao.pcm.size() == 96, "192-byte packet pushed %zu samples", audio_out::g_ao.pcm.size());
    std::printf("ok   OUT packets (196 B and 192 B) arrive in pushFromUsb() unchanged, endpoint re-armed\n");
  }

  // ---- class requests through the driver's entity routing to the firmware's handlers ----
  Bytes d;
  CHECK(hostGet(kClassIfaceIn, kGetCur, kMute << 8, kUnit, 1, &d) && d.size() == 1 && d[0] == 0, "GET_CUR mute");
  CHECK(hostSet(kClassIfaceOut, kSetCur, kMute << 8, kUnit, {1}), "SET_CUR mute master");
  CHECK(audio_out::g_ao.mute, "mute not applied");
  CHECK(hostGet(kClassIfaceIn, kGetCur, kMute << 8, kUnit, 1, &d) && d.size() == 1 && d[0] == 1, "GET_CUR mute after set");
  CHECK(hostSet(kClassIfaceOut, kSetCur, kMute << 8, kUnit, {0}) && !audio_out::g_ao.mute, "unmute");
  CHECK(hostGet(kClassIfaceIn, kGetMin, kVolume << 8, kUnit, 2, &d) && d.size() == 2 && static_cast<int16_t>(le16(d)) == -80 * 256, "GET_MIN volume");
  CHECK(hostGet(kClassIfaceIn, kGetMax, kVolume << 8, kUnit, 2, &d) && d.size() == 2 && le16(d) == 0, "GET_MAX volume");
  CHECK(hostGet(kClassIfaceIn, kGetRes, kVolume << 8, kUnit, 2, &d) && d.size() == 2 && le16(d) == 256, "GET_RES volume");
  const int16_t minus6 = -6 * 256;
  CHECK(hostSet(kClassIfaceOut, kSetCur, kVolume << 8, kUnit, {static_cast<uint8_t>(minus6 & 0xFF), static_cast<uint8_t>((minus6 >> 8) & 0xFF)}),
        "SET_CUR volume");
  CHECK(audio_out::g_ao.volume == minus6, "volume applied: %d", audio_out::g_ao.volume);
  CHECK(hostGet(kClassIfaceIn, kGetCur, kVolume << 8, kUnit, 2, &d) && d.size() == 2 && static_cast<int16_t>(le16(d)) == minus6, "GET_CUR volume");
  CHECK(hostSet(kClassIfaceOut, kSetCur, kVolume << 8 | 1, kUnit, {0x00, 0xFD}) && hostSet(kClassIfaceOut, kSetCur, kVolume << 8 | 2, kUnit, {0x00, 0xFD}),
        "SET_CUR volume left/right");
  CHECK(audio_out::g_ao.volume == minus6 + (-3 * 256), "master + averaged channel trims: %d", audio_out::g_ao.volume);
  CHECK(hostSet(kClassIfaceOut, kSetCur, kVolume << 8 | 3, kUnit, {0, 0}) == false, "channel 3 does not exist: must stall");
  CHECK(hostGet(kClassIfaceIn, kGetCur, 0x03 << 8, kUnit, 2, &d) == false, "bass control is not offered: must stall");
  CHECK(hostGet(kClassIfaceIn, kGetCur, kMute << 8, static_cast<uint16_t>(kEntityOutputTerminal << 8 | kItfAudioControl), 1, &d) == false,
        "mute on the output terminal: must stall");
  CHECK(hostSet(kClassIfaceOut, kSetCur, kVolume << 8, kUnit, {0}) == false, "volume with a 1-byte payload: must stall");
  // endpoint sampling frequency (advertised by the descriptor)
  CHECK(hostGet(kClassEpIn, kGetCur, kSamplingFreq << 8, kEpAudioOut, 3, &d) && d.size() == 3 && d[0] == 0x80 && d[1] == 0xBB && d[2] == 0x00,
        "GET_CUR sampling frequency");
  CHECK(hostSet(kClassEpOut, kSetCur, kSamplingFreq << 8, kEpAudioOut, {0x80, 0xBB, 0x00}), "SET_CUR 48000");
  CHECK(hostSet(kClassEpOut, kSetCur, kSamplingFreq << 8, kEpAudioOut, {0x44, 0xAC, 0x00}) == false, "SET_CUR 44100: must stall");
  std::printf("ok   mute / volume (cur, min, max, res) / sampling frequency answered; bad channel, control, unit, length stall\n");

  // ---- a zero-length OUT packet (what BRINGUP.md risk 3 is about) ----
  g_xfers.clear();
  const bool zlp = audiod_xfer_isr(0, kEpAudioOut, XFER_RESULT_SUCCESS, 0);
  const bool rearmed = lastXfer(kEpAudioOut) != nullptr;
  std::printf("info zero-length OUT packet: driver returns %s and %s the endpoint%s\n", zlp ? "true" : "false",
              rearmed ? "re-arms" : "does NOT re-arm",
              rearmed ? "" : " (stream stays dead until the host re-selects the interface)");


  // ---- MIDI: the MIDI driver takes the MIDI function; a whole GET_CONFIG dump fits its TX FIFO ----
  midid_init();
  midid_reset(0);
  const size_t midiBytes = (audioAc - 8) - midiAc;  // the MIDI AudioControl interface up to the audio function's IAD
  const uint16_t midiTaken = midid_open(0, reinterpret_cast<const tusb_desc_interface_t*>(&cfg[midiAc]),
                                        static_cast<uint16_t>(cfg.size() - midiAc));
  CHECK(midiTaken == midiBytes, "MIDI driver consumed %u bytes, the MIDI function is %zu", midiTaken, midiBytes);
  CHECK(tud_midi_n_mounted(0), "MIDI driver did not open both bulk endpoints");

  Config config;
  NullActions actions;
  FrameSink replies;
  ConfigServer server(config, actions, replies);
  const uint8_t dumpAll = 0xFF;
  uint8_t request[32];
  const size_t requestLen = syx::encodeFrame(static_cast<uint8_t>(config.deck()), static_cast<uint8_t>(proto::sysex::Cmd::GetConfig),
                                             &dumpAll, 1, request, sizeof request);
  server.handle(request, requestLen, 0);
  CHECK(replies.frames.size() == proto::kConfigCount + 1, "dump is %zu frames", replies.frames.size());
  if (!replies.frames.empty()) {
    CHECK(replies.frames.back()[6] == proto::sysex::kAck, "dump does not end with the ACK");
  }

  Bytes written;  // every byte handed to the driver, in order
  unsigned packets = 0, refused = 0;
  for (const Bytes& frame : replies.frames) {
    midi::UsbPacket pk[16];
    const size_t n = midi::encodeSysex(0, frame.data(), frame.size(), pk, 16);
    for (size_t i = 0; i < n; ++i) {
      uint8_t raw[4];
      pk[i].toBytes(raw);
      if (tud_midi_n_packet_write(0, raw)) {
        written.insert(written.end(), raw, raw + 4);
        ++packets;
      } else {
        ++refused;
      }
    }
  }
  CHECK(refused == 0 && packets == 169, "%u packets accepted, %u refused (expected all 169 of the dump)", packets, refused);

  // The host collects the endpoint 64 bytes at a time; everything must come out in order.
  Bytes onTheWire;
  int transfers = 0;
  for (;;) {
    const Xfer* x = lastXfer(kEpMidiIn);
    if (x == nullptr || x->len == 0 || transfers > 64) break;
    onTheWire.insert(onTheWire.end(), x->data.begin(), x->data.end());
    const uint16_t len = x->len;
    g_xfers.clear();
    g_epBusy[kEpMidiIn] = false;
    midid_xfer_cb(0, kEpMidiIn, XFER_RESULT_SUCCESS, len);
    ++transfers;
  }
  CHECK(onTheWire == written, "bulk IN carried %zu bytes, %zu were written", onTheWire.size(), written.size());
  std::printf("%s   GET_CONFIG dump: %zu frames -> %u USB-MIDI packets (%zu B) accepted by the %d-byte TX FIFO in one go, %d bulk transfers, byte-exact\n",
              onTheWire == written && refused == 0 ? "ok  " : "FAIL", replies.frames.size(), packets, written.size(),
              CFG_TUD_MIDI_TX_BUFSIZE, transfers);

  // How much the FIFO holds when the host reads nothing at all.
  unsigned room = 0;
  for (;;) {
    const uint8_t raw[4] = {0x09, 0x90, 60, 100};
    if (!tud_midi_n_packet_write(0, raw)) break;
    if (++room > 1000) break;
  }
  CHECK(room * 4u >= 676u, "TX FIFO took only %u packets with an idle host", room);
  std::printf("info idle host: the TX FIFO takes %u more packets (%u B) before refusing\n", room, room * 4);

  // ---- stop ----
  CHECK(setInterface(kItfAudioStreaming, 0), "SET_INTERFACE alt 0 refused");
  CHECK(!audio_out::g_ao.streaming, "firmware was not told the stream stopped");
  std::printf("ok   alt 0: firmware told the stream stopped\n");

  std::printf("%s: %d checks, %d failed\n", g_failed == 0 ? "driver check passed" : "driver check FAILED", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
