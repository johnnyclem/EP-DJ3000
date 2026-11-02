#include "uart_dac.h"

#include "audio_out.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"

#define DAC_UART uart0
#define DAC_TX 0
#define DAC_BAUD 3125000
#define DAC_STRAP 22

static bool enabled;
static uint16_t sequence;

static uint8_t crc8(const uint8_t *data, uint32_t n) {
  uint8_t crc = 0;
  for (uint32_t i = 0; i < n; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

void uart_dac_init(void) {
  gpio_init(DAC_STRAP);
  gpio_set_dir(DAC_STRAP, GPIO_IN);
  gpio_pull_up(DAC_STRAP);
  enabled = gpio_get(DAC_STRAP);
  if (!enabled) return;

  uart_init(DAC_UART, DAC_BAUD);
  gpio_set_function(DAC_TX, GPIO_FUNC_UART);
  uart_set_format(DAC_UART, 8, 1, UART_PARITY_NONE);
  uart_set_fifo_enabled(DAC_UART, true);
}

bool uart_dac_enabled(void) {
  return enabled;
}

void uart_dac_write(const int16_t *stereo, uint32_t frames) {
  if (!enabled) return;

  uint8_t payload[2 + 2 + AUDIO_FRAME * 4];
  uint32_t nbytes = frames * 4;
  payload[0] = (uint8_t)(nbytes & 0xFF);
  payload[1] = (uint8_t)((nbytes >> 8) & 0xFF);
  payload[2] = (uint8_t)(sequence & 0xFF);
  payload[3] = (uint8_t)((sequence >> 8) & 0xFF);
  sequence++;

  for (uint32_t i = 0; i < frames * 2; i++) {
    uint16_t s = (uint16_t)stereo[i];
    payload[4 + i * 2] = (uint8_t)(s & 0xFF);
    payload[5 + i * 2] = (uint8_t)((s >> 8) & 0xFF);
  }

  uint8_t crc = crc8(payload, 4 + nbytes);
  uart_putc_raw(DAC_UART, 0xA5);
  uart_putc_raw(DAC_UART, 0x5A);
  uart_write_blocking(DAC_UART, payload, 4 + nbytes);
  uart_putc_raw(DAC_UART, crc);
}
