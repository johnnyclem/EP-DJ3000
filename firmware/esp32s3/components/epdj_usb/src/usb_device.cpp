#include "epdj_usb/usb_device.h"

#include <atomic>
#include <cstring>

#include "epdj/config_server.h"
#include "epdj/usb_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "tinyusb.h"
#include "tusb.h"
#include "usb_internal.h"

namespace epdj {
namespace usb_dev {

namespace {

const char* TAG = "usb";

// Never 0, which g_suspendedAtUs uses for "not suspended".
uint64_t nowUs() {
  const uint64_t t = static_cast<uint64_t>(esp_timer_get_time());
  return t != 0 ? t : 1;
}

// The descriptor builder and the TinyUSB audio configuration must agree on the isochronous packet size.
#if CONFIG_EPDJ_USB_AUDIO
static_assert(usb::kAudioPacketBytes == CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX,
              "usb_desc.h kAudioPacketBytes != CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX in epdj_tusb_config.h");
#endif
static_assert(usb::kStringCount == 8, "esp_tinyusb keeps exactly 8 string descriptors");

// A GET_CONFIG dump (PROTOCOL.md section 3: one CONFIG frame per key, then the ACK) must fit the MIDI TX FIFO in one
// go, so the engine task never has to hold an event back for it. Every frame is its own SysEx message, and a
// message of n bytes is ceil(n / 3) four-byte USB-MIDI packets: 33 x 5 + 4 = 169 packets = 676 bytes.
constexpr size_t kDumpPackets = proto::kConfigCount * ((kConfigFrameBytes + 2) / 3) + (kAckFrameBytes + 2) / 3;
static_assert(CFG_TUD_MIDI_TX_BUFSIZE >= kDumpPackets * 4,
              "CFG_TUD_MIDI_TX_BUFSIZE (epdj_tusb_config.h) cannot hold a full GET_CONFIG dump");

// Descriptors and strings must outlive the stack.
alignas(4) uint8_t g_device[usb::kDeviceDescriptorBytes];
alignas(4) uint8_t g_config[usb::kMaxConfigDescriptorBytes];
usb::Strings g_strings;

constexpr size_t kRxDepth = 128;
QueueHandle_t g_rxQ = nullptr;
bool g_audioEnabled = false;

std::atomic<bool> g_mounted{false};
std::atomic<uint64_t> g_suspendedAtUs{0};  // esp_timer time the bus went idle; 0 = not suspended
std::atomic<uint32_t> g_rxPackets{0}, g_rxDropped{0}, g_txPackets{0}, g_txRejected{0};

// TinyUSB task context.
void onEvent(tinyusb_event_t* ev, void*) {
  switch (ev->id) {
    case TINYUSB_EVENT_ATTACHED:
      g_mounted.store(true, std::memory_order_release);
      g_suspendedAtUs.store(0, std::memory_order_relaxed);
      // A fresh configuration puts every interface back on alternate setting 0, but a bus reset (a replug, a host
      // reboot, a port reset) reaches the audio driver without any callback, so a stream that was running before
      // would still be flagged as streaming.
      detail::audioOnDetach();
      ESP_LOGI(TAG, "mounted by host");
      break;
    case TINYUSB_EVENT_DETACHED:
      g_mounted.store(false, std::memory_order_release);
      detail::audioOnDetach();
      ESP_LOGI(TAG, "unmounted");
      break;
    case TINYUSB_EVENT_SUSPENDED:
      g_suspendedAtUs.store(nowUs(), std::memory_order_relaxed);
      ESP_LOGI(TAG, "bus idle (suspended, or the cable was pulled)");
      break;
    case TINYUSB_EVENT_RESUMED:
      g_suspendedAtUs.store(0, std::memory_order_relaxed);
      ESP_LOGI(TAG, "bus active again");
      break;
  }
}

}  // namespace

namespace detail {
std::atomic<uint32_t> g_audioFrames{0};
}

bool start(const Params& p) {
#if CONFIG_EPDJ_USB_AUDIO
  g_audioEnabled = p.audio;
#else
  g_audioEnabled = false;
#endif

  const size_t devLen = usb::buildDeviceDescriptor(g_audioEnabled, g_device, sizeof g_device);
  const size_t cfgLen = usb::buildConfigDescriptor(g_audioEnabled, g_config, sizeof g_config);
  if (devLen == 0 || cfgLen == 0) {
    ESP_LOGE(TAG, "descriptor build failed");
    return false;
  }
  if (!usb::buildStrings(p.deck, p.mac, &g_strings)) {
    ESP_LOGE(TAG, "string descriptors do not fit");
    return false;
  }

  g_rxQ = xQueueCreate(kRxDepth, sizeof(RxPacket));
  if (g_rxQ == nullptr) return false;

  tinyusb_config_t cfg = {};
  cfg.port = TINYUSB_PORT_FULL_SPEED_0;
  cfg.phy.skip_setup = false;
  // Bus powered, no VBUS sense pin: the PHY then reports VBUS as always present, so TinyUSB never gets a
  // DCD_EVENT_UNPLUGGED and a pulled cable shows up only as the bus going idle (hostActive()). A real unplug
  // signal needs a 5 V -> 3.3 V divider on a spare GPIO with self_powered = true and vbus_monitor_io set.
  cfg.phy.self_powered = false;
  cfg.phy.vbus_monitor_io = -1;
  cfg.task.size = 4096;
  cfg.task.priority = 22;
  cfg.task.xCoreID = 1;
  cfg.descriptor.device = reinterpret_cast<const tusb_desc_device_t*>(g_device);
  cfg.descriptor.qualifier = nullptr;  // full speed only
  cfg.descriptor.string = g_strings.table;
  cfg.descriptor.string_count = usb::kStringCount;
  cfg.descriptor.full_speed_config = g_config;
  cfg.descriptor.high_speed_config = nullptr;
  cfg.pm_lock_enable = false;
  cfg.event_cb = onEvent;
  cfg.event_arg = nullptr;

  const esp_err_t err = tinyusb_driver_install(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "tinyusb_driver_install: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(TAG, "\"%s\" serial %s, config %u bytes, VID:PID %04X:%04X, audio %s", g_strings.product, g_strings.serial,
           static_cast<unsigned>(cfgLen), proto::kUsbVendorId, proto::kUsbProductId,
           g_audioEnabled ? "UAC1 on" : "off");
  return true;
}

bool mounted() { return g_mounted.load(std::memory_order_acquire); }
bool suspended() { return g_suspendedAtUs.load(std::memory_order_relaxed) != 0; }
bool hostActive() {
  if (!mounted()) return false;
  const uint64_t since = g_suspendedAtUs.load(std::memory_order_relaxed);
  return since == 0 || nowUs() - since < kSuspendDebounceUs;
}
bool audioEnabled() { return g_audioEnabled; }
bool audioStreaming() { return detail::audioIsStreaming(); }

void releaseForRomDownload() {
  const esp_err_t err = tinyusb_driver_uninstall();
  if (err != ESP_OK) ESP_LOGW(TAG, "tinyusb_driver_uninstall: %s", esp_err_to_name(err));
  // Back to the reset values: the PHY belongs to USB-Serial-JTAG (the "hardware chooses" setting).
  CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL);
  g_mounted.store(false, std::memory_order_release);
}

bool popRx(RxPacket* out) { return g_rxQ != nullptr && xQueueReceive(g_rxQ, out, 0) == pdTRUE; }

bool writePacket(const uint8_t pkt[4]) {
  if (!hostActive() || !tud_midi_n_mounted(0) || !tud_midi_n_packet_write(0, pkt)) {
    g_txRejected.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  g_txPackets.fetch_add(1, std::memory_order_relaxed);
  return true;
}

Stats stats() {
  Stats s;
  s.rxPackets = g_rxPackets.load(std::memory_order_relaxed);
  s.rxDropped = g_rxDropped.load(std::memory_order_relaxed);
  s.txPackets = g_txPackets.load(std::memory_order_relaxed);
  s.txRejected = g_txRejected.load(std::memory_order_relaxed);
  s.audioFrames = detail::g_audioFrames.load(std::memory_order_relaxed);
  return s;
}

}  // namespace usb_dev
}  // namespace epdj

// TinyUSB task: bytes arrived on the MIDI OUT endpoint. Stamp them now; the engine task consumes them.
extern "C" void tud_midi_rx_cb(uint8_t itf) {
  using namespace epdj::usb_dev;
  const uint64_t t = static_cast<uint64_t>(esp_timer_get_time());
  RxPacket p;
  p.rxUs = t;
  while (tud_midi_n_packet_read(itf, p.bytes)) {
    g_rxPackets.fetch_add(1, std::memory_order_relaxed);
    if (xQueueSend(g_rxQ, &p, 0) != pdTRUE) g_rxDropped.fetch_add(1, std::memory_order_relaxed);
  }
}
