#include "epdj_hal/trs_midi.h"

#include "driver/uart.h"
#include "epdj_hal/board_pins.h"
#include "epdj_hal/time.h"
#include "esp_log.h"

namespace epdj {
namespace hal {
namespace {
const char* TAG = "trs";
constexpr uart_port_t kUart = UART_NUM_1;
constexpr int kBaud = 31250;
constexpr int kRxBufBytes = 512;
constexpr int kTxBufBytes = 512;
constexpr size_t kChunkQueueDepth = 64;
}  // namespace

bool TrsMidi::begin() {
  uart_config_t cfg = {};
  cfg.baud_rate = kBaud;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  // Driver events let the rx task wake up on the first byte of a burst instead of polling.
  if (uart_driver_install(kUart, kRxBufBytes, kTxBufBytes, 32, &uartEvents_, 0) != ESP_OK ||
      uart_param_config(kUart, &cfg) != ESP_OK ||
      uart_set_pin(kUart, pins::kTrsTx, pins::kTrsRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    ESP_LOGE(TAG, "uart setup failed");
    return false;
  }
  uart_set_rx_full_threshold(kUart, 1);  // interrupt per byte: 31250 baud is only ~3 kB/s
  chunks_ = xQueueCreate(kChunkQueueDepth, sizeof(Chunk));
  if (chunks_ == nullptr) return false;
  if (xTaskCreatePinnedToCore(&TrsMidi::taskEntry, "trs_rx", 3072, this, 20, nullptr, 0) != pdPASS) return false;
  ESP_LOGI(TAG, "UART1 %d baud, TX GPIO%d RX GPIO%d", kBaud, pins::kTrsTx, pins::kTrsRx);
  return true;
}

void TrsMidi::taskEntry(void* self) {
  static_cast<TrsMidi*>(self)->rxLoop();
  vTaskDelete(nullptr);
}

void TrsMidi::rxLoop() {
  uart_event_t ev;
  for (;;) {
    if (xQueueReceive(uartEvents_, &ev, portMAX_DELAY) != pdTRUE) continue;
    const uint64_t t = nowUs();
    if (ev.type == UART_DATA) {
      size_t avail = 0;
      uart_get_buffered_data_len(kUart, &avail);
      while (avail > 0) {
        Chunk c;
        c.us = t;
        const size_t want = avail < kChunkBytes ? avail : kChunkBytes;
        const int n = uart_read_bytes(kUart, c.data, static_cast<uint32_t>(want), 0);
        if (n <= 0) break;
        c.len = static_cast<uint8_t>(n);
        if (xQueueSend(chunks_, &c, 0) != pdTRUE) rxOverflows_.fetch_add(1, std::memory_order_relaxed);
        avail -= static_cast<size_t>(n);
      }
    } else if (ev.type == UART_FIFO_OVF || ev.type == UART_BUFFER_FULL) {
      rxOverflows_.fetch_add(1, std::memory_order_relaxed);
      uart_flush_input(kUart);
      xQueueReset(uartEvents_);
    }
  }
}

bool TrsMidi::pop(Chunk* out) { return chunks_ != nullptr && xQueueReceive(chunks_, out, 0) == pdTRUE; }

void TrsMidi::write(const uint8_t* bytes, size_t n) {
  if (uartEvents_ == nullptr || n == 0) return;
  size_t room = 0;
  uart_get_tx_buffer_free_size(kUart, &room);
  if (room < n) {  // never block the engine task on a slow wire
    txDropped_.fetch_add(static_cast<uint32_t>(n), std::memory_order_relaxed);
    return;
  }
  uart_write_bytes(kUart, bytes, n);
}

}  // namespace hal
}  // namespace epdj
