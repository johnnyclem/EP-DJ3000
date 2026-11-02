#include "audio_out.h"

#include "tusb.h"
#include "uart_dac.h"

// One short groove. A click up front, then a body, so a slow scratch
// still has an edge to hear. 256 samples, played at `rate`.
static const int16_t loop[256] = {
    0, 13795, 19551, 15774, 6363, -2157, -4582, 199, 8999, 16546, 18642, 14391, 6363, -1181, -4825, -3775,
    0, 3194, 3177, -570, -6363, -11446, -13617, -12373, -9000, -5653, -4069, -4643, -6363, -7541, -6843, -4029,
    0, 3790, 6166, 6821, 6363, 5853, 6135, 7368, 8999, 10131, 10060, 8650, 6363, 3955, 2046, 820,
    0, -919, -2324, -4251, -6363, -8153, -9211, -9426, -9000, -8290, -7597, -7003, -6363, -5429, -4018, -2140,
    0, 2099, 3904, 5307, 6363, 7207, 7946, 8580, 9000, 9047, 8609, 7680, 6363, 4823, 3207, 1597,
    0, -1614, -3254, -4873, -6363, -7596, -8466, -8928, -9000, -8736, -8193, -7402, -6363, -5072, -3541, -1820,
    0, 1813, 3521, 5052, 6363, 7436, 8252, 8785, 9000, 8864, 8364, 7516, 6363, 4970, 3404, 1729,
    0, -1731, -3412, -4978, -6363, -7502, -8340, -8844, -9000, -8811, -8294, -7469, -6363, -5012, -3460, -1766,
    0, 1765, 3457, 5008, 6363, 7475, 8304, 8820, 9000, 8833, 8323, 7488, 6363, 4995, 3437, 1751,
    0, -1751, -3438, -4996, -6363, -7486, -8319, -8829, -9000, -8824, -8311, -7480, -6363, -5002, -3446, -1757,
    0, 1757, 3446, 5001, 6363, 7481, 8313, 8825, 9000, 8828, 8316, 7484, 6363, 4999, 3443, 1755,
    0, -1755, -3443, -4999, -6363, -7483, -8315, -8827, -9000, -8826, -8314, -7482, -6363, -5000, -3444, -1756,
    0, 1756, 3444, 5000, 6363, 7483, 8314, 8826, 9000, 8827, 8315, 7483, 6363, 4999, 3443, 1755,
    0, -1755, -3443, -5000, -6363, -7483, -8315, -8827, -9000, -8826, -8314, -7483, -6363, -5000, -3444, -1755,
    0, 1755, 3444, 5000, 6363, 7483, 8314, 8827, 9000, 8827, 8314, 7483, 6363, 5000, 3444, 1755,
    0, -1755, -3444, -5000, -6363, -7483, -8314, -8827, -9000, -8827, -8314, -7483, -6363, -5000, -3444, -1755};

static int32_t rate_q16;
static uint16_t gain_q15 = 32767;
static bool usb_mute;
static int16_t usb_db;
static uint32_t phase;
static uint16_t cue_left;
static uint32_t last_ms;

static int16_t sample_at(uint32_t p) {
  uint32_t idx = (p >> 16) & 255;
  uint32_t frac = p & 0xFFFF;
  int32_t a = loop[idx];
  int32_t b = loop[(idx + 1) & 255];
  return (int16_t)(a + (((b - a) * (int32_t)frac) >> 16));
}

static int32_t usb_trim_q15(void) {
  if (usb_mute) return 0;
  if (usb_db <= -90) return 0;
  if (usb_db >= 0) return 32767;
  uint32_t shift = (uint32_t)(-usb_db) / 6;
  if (shift > 15) return 0;
  return 32767 >> shift;
}

void audio_out_init(void) {
  rate_q16 = 0;
  phase = 0;
  last_ms = 0;
}

void audio_out_set_rate(int32_t rate) { rate_q16 = rate; }
void audio_out_set_gain(uint16_t gain) { gain_q15 = gain; }
void audio_out_set_usb(bool mute, int16_t volume_db) {
  usb_mute = mute;
  usb_db = volume_db;
}
void audio_out_cue(void) { cue_left = 64; }

void audio_out_task(void) {
  uint32_t now = tusb_time_millis_api();
  if (now == last_ms) return;
  last_ms = now;

  int16_t frame[AUDIO_FRAME * 2];
  int32_t trim = usb_trim_q15();
  int32_t gain = ((int32_t)gain_q15 * trim) >> 15;

  for (int i = 0; i < AUDIO_FRAME; i++) {
    int32_t s = 0;
    if (rate_q16 != 0) {
      s = sample_at(phase);
      phase += (uint32_t)rate_q16;
    }
    if (cue_left) {
      uint32_t cphase = (uint32_t)(64 - cue_left) << 16;
      s += sample_at(cphase);
      cue_left--;
    }
    s = (s * gain) >> 15;
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    frame[i * 2] = (int16_t)s;
    frame[i * 2 + 1] = (int16_t)s;
  }

  if (tud_audio_mounted()) {
    tud_audio_write(frame, sizeof(frame));
  }
  uart_dac_write(frame, AUDIO_FRAME);
}
