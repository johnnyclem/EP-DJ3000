#include "epdj_link/radio.h"

#include <atomic>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace epdj {
namespace radio {
namespace {

const char* TAG = "radio";
constexpr size_t kRxDepth = 16;
constexpr size_t kTxDepth = 16;
constexpr uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct TxItem {
  uint8_t len;
  uint8_t data[kMaxPacket];
};

QueueHandle_t g_rxQ = nullptr;
QueueHandle_t g_txQ = nullptr;
SemaphoreHandle_t g_txDone = nullptr;
bool g_running = false;

std::atomic<uint32_t> g_rxPackets{0}, g_rxDropped{0}, g_txPackets{0}, g_txQueueFull{0}, g_txFailed{0};

// ---- Wi-Fi task context: copy and return ----
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len <= 0 || len > static_cast<int>(kMaxPacket)) return;
  RxPacket p;
  p.rxUs = static_cast<uint64_t>(esp_timer_get_time());
  p.rssi = (info != nullptr && info->rx_ctrl != nullptr) ? static_cast<int8_t>(info->rx_ctrl->rssi) : 0;
  p.len = static_cast<uint8_t>(len);
  std::memcpy(p.data, data, static_cast<size_t>(len));
  g_rxPackets.fetch_add(1, std::memory_order_relaxed);
  if (xQueueSend(g_rxQ, &p, 0) != pdTRUE) g_rxDropped.fetch_add(1, std::memory_order_relaxed);
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void onSent(const esp_now_send_info_t*, esp_now_send_status_t) {
#else
void onSent(const uint8_t*, esp_now_send_status_t) {
#endif
  xSemaphoreGive(g_txDone);  // runs in the Wi-Fi task, not an interrupt
}

// One packet at a time: the next esp_now_send waits for the previous send-complete callback, so the driver's
// own queue never fills and the order on the air is the order we queued.
void linkTask(void*) {
  TxItem it;
  for (;;) {
    if (xQueueReceive(g_txQ, &it, portMAX_DELAY) != pdTRUE) continue;
    xSemaphoreTake(g_txDone, 0);
    if (esp_now_send(kBroadcast, it.data, it.len) != ESP_OK) {
      g_txFailed.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    g_txPackets.fetch_add(1, std::memory_order_relaxed);
    if (xSemaphoreTake(g_txDone, pdMS_TO_TICKS(10)) != pdTRUE) g_txFailed.fetch_add(1, std::memory_order_relaxed);
  }
}

}  // namespace

void readStationMac(uint8_t mac[6]) { esp_read_mac(mac, ESP_MAC_WIFI_STA); }

bool start(uint8_t channel) {
  if (g_running) return true;
  g_rxQ = xQueueCreate(kRxDepth, sizeof(RxPacket));
  g_txQ = xQueueCreate(kTxDepth, sizeof(TxItem));
  g_txDone = xSemaphoreCreateBinary();
  if (g_rxQ == nullptr || g_txQ == nullptr || g_txDone == nullptr) return false;

  esp_err_t err = esp_netif_init();
  if (err == ESP_OK) {
    err = esp_event_loop_create_default();
    if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;  // somebody else made it
  }
  wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
  if (err == ESP_OK) err = esp_wifi_init(&wc);
  if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err == ESP_OK) err = esp_wifi_start();
  if (err == ESP_OK) err = esp_wifi_set_ps(WIFI_PS_NONE);  // power save would add up to a beacon interval of latency
  if (err == ESP_OK) err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  if (err == ESP_OK) err = esp_now_init();
  if (err == ESP_OK) err = esp_now_register_recv_cb(onRecv);
  if (err == ESP_OK) err = esp_now_register_send_cb(onSent);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "radio bring-up failed: %s", esp_err_to_name(err));
    return false;
  }

  esp_now_peer_info_t peer = {};
  std::memcpy(peer.peer_addr, kBroadcast, 6);
  peer.channel = 0;  // current channel
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  err = esp_now_add_peer(&peer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_now_add_peer: %s", esp_err_to_name(err));
    return false;
  }
  // The 1 Mbps default would put a 60-byte frame on the air for ~0.7 ms; 24 Mbps OFDM takes ~60 us.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
  esp_now_rate_config_t rate = {};
  rate.phymode = WIFI_PHY_MODE_11G;
  rate.rate = WIFI_PHY_RATE_24M;
  err = esp_now_set_peer_rate_config(kBroadcast, &rate);
#else
  err = esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_24M);
#endif
  if (err != ESP_OK) ESP_LOGW(TAG, "could not set the ESP-NOW rate (%s): staying at the default", esp_err_to_name(err));

  if (xTaskCreatePinnedToCore(linkTask, "link", 3072, nullptr, 21, nullptr, 0) != pdPASS) return false;
  g_running = true;
  uint8_t mac[6];
  readStationMac(mac);
  ESP_LOGI(TAG, "ESP-NOW on channel %u, station %02X:%02X:%02X:%02X:%02X:%02X, broadcast peer", channel, mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
  return true;
}

bool running() { return g_running; }

bool pop(RxPacket* out) { return g_rxQ != nullptr && xQueueReceive(g_rxQ, out, 0) == pdTRUE; }

bool send(const uint8_t* data, size_t len) {
  if (!g_running || len == 0 || len > kMaxPacket) return false;
  TxItem it;
  it.len = static_cast<uint8_t>(len);
  std::memcpy(it.data, data, len);
  if (xQueueSend(g_txQ, &it, 0) != pdTRUE) {
    g_txQueueFull.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

Stats stats() {
  Stats s;
  s.rxPackets = g_rxPackets.load(std::memory_order_relaxed);
  s.rxDropped = g_rxDropped.load(std::memory_order_relaxed);
  s.txPackets = g_txPackets.load(std::memory_order_relaxed);
  s.txQueueFull = g_txQueueFull.load(std::memory_order_relaxed);
  s.txFailed = g_txFailed.load(std::memory_order_relaxed);
  return s;
}

}  // namespace radio
}  // namespace epdj
