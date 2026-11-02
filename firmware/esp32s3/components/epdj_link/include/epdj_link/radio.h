// ESP-NOW broadcast transport (docs/LINK.md section 8). The Wi-Fi driver's callbacks run in its own task, so
// they only copy into queues; the engine task drains rx and fills tx, a small link task paces the sends.
#pragma once
#include <cstddef>
#include <cstdint>

namespace epdj {
namespace radio {

constexpr size_t kMaxPacket = 250;  // ESP-NOW v1 payload limit == link::kMaxPacket

struct RxPacket {
  uint64_t rxUs;  // esp_timer time inside the receive callback
  int8_t rssi;    // dBm
  uint8_t len;
  uint8_t data[kMaxPacket];
};

struct Stats {
  uint32_t rxPackets = 0;
  uint32_t rxDropped = 0;    // rx queue full: the engine task is not draining
  uint32_t txPackets = 0;    // handed to the radio
  uint32_t txQueueFull = 0;  // engine produced faster than the radio could send
  uint32_t txFailed = 0;     // esp_now_send refused, or no send-complete callback
};

// Reads the factory station MAC (efuse); no radio needed.
void readStationMac(uint8_t mac[6]);

// Brings up Wi-Fi in station mode on a fixed channel and ESP-NOW with a broadcast peer, starts the link task.
bool start(uint8_t channel);
bool running();

bool pop(RxPacket* out);                  // engine task: oldest received packet
bool send(const uint8_t* data, size_t len);  // engine task: queue one packet (<= kMaxPacket); never blocks
Stats stats();

}  // namespace radio
}  // namespace epdj
