#ifndef UART_DAC_H_
#define UART_DAC_H_

#include <stdbool.h>
#include <stdint.h>

// Optional. Ground GP22 at boot to leave the UART idle.
// Otherwise UART0 TX (GP0) runs at 3.125 Mbaud, 8N1.
//
// One frame per millisecond, 48 stereo frames, little-endian:
//   0xA5 0x5A
//   uint16 length   payload bytes, always 192
//   uint16 sequence wraps
//   int16 L, int16 R, repeated 48 times
//   uint8  crc8     poly 0x07, over length..last sample, not the sync bytes

void uart_dac_init(void);
bool uart_dac_enabled(void);
void uart_dac_write(const int16_t *stereo, uint32_t frames);

#endif
