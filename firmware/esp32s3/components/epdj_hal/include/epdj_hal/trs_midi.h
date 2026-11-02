// 3.5 mm TRS MIDI jack on UART1 (31250 baud, 8N1). Received bytes are time-stamped when they arrive by a
// small task, so the engine can estimate an incoming MIDI clock from them (a 1 kHz poll alone would add up to
// 1 ms of jitter to every tick).
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace epdj {
namespace hal {

class TrsMidi {
 public:
  struct Chunk {
    uint64_t us;      // esp_timer time when the bytes were noticed
    uint8_t len;      // 1..kChunkBytes
    uint8_t data[15];
  };
  static constexpr size_t kChunkBytes = 15;

  bool begin();
  // Engine task: oldest received chunk, or false.
  bool pop(Chunk* out);
  // Queues bytes for the wire without ever blocking; bytes that do not fit are dropped and counted.
  void write(const uint8_t* bytes, size_t n);

  uint32_t rxOverflows() const { return rxOverflows_.load(std::memory_order_relaxed); }
  uint32_t txDropped() const { return txDropped_.load(std::memory_order_relaxed); }

 private:
  static void taskEntry(void* self);
  void rxLoop();

  QueueHandle_t uartEvents_ = nullptr;
  QueueHandle_t chunks_ = nullptr;
  std::atomic<uint32_t> rxOverflows_{0};
  std::atomic<uint32_t> txDropped_{0};
};

}  // namespace hal
}  // namespace epdj
