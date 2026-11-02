#include "epdj_usb/usb_device.h"

#include <atomic>
#include <cstring>

#include "epdj/usb_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "tusb.h"
#include "usb_internal.h"

namespace epdj {
namespace usb_dev {

namespace {

const char* TAG = "usb";

// The descriptor builder and the TinyUSB audio configuration must agree on the isochronous packet size.
#if CONFIG_EPDJ_USB_AUDIO
static_assert(usb::kAudioPacketBytes == CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX,
              "usb_desc.h kAudioPacketBytes != CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX in epdj_tusb_config.h");
#endif
static_assert(usb::kStringCount == 8, "esp_tinyusb keeps exactly 8 string descriptors");

// Descriptors and strings must outlive the stack.
alignas(4) uint8_t g_device[usb::kDeviceDescriptorBytes];
alignas(4) uint8_t g_config[usb::kMaxConfigDescriptorBytes];
char g_product[32];
char g_serial[13];
const char kLangId[2] = {0x09, 0x04};  // English (US); esp_tinyusb reads these two bytes as string 0
const char* g_strings[usb::kStringCount];

constexpr size_t kRxDepth = 128;
QueueHandle_t g_rxQ = nullptr;
bool g_audioEnabled = false;

std::atomic<bool> g_mounted{false};
std::atomic<bool> g_suspended{false};
std::atomic<uint32_t> g_rxPackets{0}, g_rxDropped{0}, g_txPackets{0}, g_txRejected{0};

// TinyUSB task context.
void onEvent(tinyusb_event_t* ev, void*) {
  switch (ev->id) {
    case TINYUSB_EVENT_ATTACHED:
      g_mounted.store(true, std::memory_order_release);
      g_suspended.store(false, std::memory_order_relaxed);
      ESP_LOGI(TAG, "mounted by host");
      break;
    case TINYUSB_EVENT_DETACHED:
      g_mounted.store(false, std::memory_order_release);
      detail::audioOnDetach();
      ESP_LOGI(TAG, "unmounted");
      break;
    case TINYUSB_EVENT_SUSPENDED:
      g_suspended.store(true, std::memory_order_relaxed);
      break;
    case TINYUSB_EVENT_RESUMED:
      g_suspended.store(false, std::memory_order_relaxed);
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
  usb::formatProductName(p.deck, g_product, sizeof g_product);
  usb::formatSerial(p.mac, g_serial, sizeof g_serial);

  g_strings[usb::kStrLangId] = kLangId;
  g_strings[usb::kStrManufacturer] = usb::kManufacturer;
  g_strings[usb::kStrProduct] = g_product;
  g_strings[usb::kStrSerial] = g_serial;
  for (unsigned c = 0; c < usb::kMidiCables; ++c) g_strings[usb::kStrCable0 + c] = usb::kCableNames[c];
  g_strings[usb::kStrAudio] = usb::kAudioName;

  g_rxQ = xQueueCreate(kRxDepth, sizeof(RxPacket));
  if (g_rxQ == nullptr) return false;

  tinyusb_config_t cfg = {};
  cfg.port = TINYUSB_PORT_FULL_SPEED_0;
  cfg.phy.skip_setup = false;
  cfg.phy.self_powered = false;
  cfg.phy.vbus_monitor_io = -1;
  cfg.task.size = 4096;
  cfg.task.priority = 22;
  cfg.task.xCoreID = 1;
  cfg.descriptor.device = reinterpret_cast<const tusb_desc_device_t*>(g_device);
  cfg.descriptor.qualifier = nullptr;  // full speed only
  cfg.descriptor.string = g_strings;
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
  ESP_LOGI(TAG, "\"%s\" serial %s, config %u bytes, VID:PID %04X:%04X, audio %s", g_product, g_serial,
           static_cast<unsigned>(cfgLen), usb::kVendorId, usb::kProductId, g_audioEnabled ? "UAC2 on" : "off");
  return true;
}

bool mounted() { return g_mounted.load(std::memory_order_acquire); }
bool suspended() { return g_suspended.load(std::memory_order_relaxed); }
bool audioEnabled() { return g_audioEnabled; }
bool audioStreaming() { return detail::audioIsStreaming(); }

bool popRx(RxPacket* out) { return g_rxQ != nullptr && xQueueReceive(g_rxQ, out, 0) == pdTRUE; }

bool writePacket(const uint8_t pkt[4]) {
  if (!mounted() || !tud_midi_n_mounted(0) || !tud_midi_n_packet_write(0, pkt)) {
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
