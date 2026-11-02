#include <stdbool.h>
#include <stdint.h>

#include "bsp/board_api.h"
#include "tusb.h"

#include "audio_out.h"
#include "controls.h"
#include "uart_dac.h"

enum {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;
static uint32_t last_ms;
static uint32_t blink_ms;
static bool led;

// Master plus left plus right. Channel 0 is the master.
static bool mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
static int16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
static uint32_t samp_freq = 48000;

static void push_usb_gain(void) {
  audio_out_set_usb(mute[0] || mute[1] || mute[2], volume[0]);
}

int main(void) {
  board_init();

  tusb_rhport_init_t dev_init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_FULL,
  };
  tusb_init(BOARD_TUD_RHPORT, &dev_init);
  board_init_after_tusb();

  controls_init();
  uart_dac_init();
  audio_out_init();
  for (int i = 0; i < 3; i++) volume[i] = 0;

  while (1) {
    tud_task();

    uint32_t now = tusb_time_millis_api();
    if (now != last_ms) {
      last_ms = now;
      deck_state_t deck;
      controls_poll(&deck);
      audio_out_set_rate(deck.rate_q16);
      audio_out_set_gain(deck.gain_q15);
      if (deck.cue_edge) audio_out_cue();
      audio_out_task();
    }

    if (now - blink_ms >= blink_interval_ms) {
      blink_ms += blink_interval_ms;
      board_led_write(led);
      led = !led;
    }
  }
}

void tud_mount_cb(void) { blink_interval_ms = BLINK_MOUNTED; }
void tud_umount_cb(void) { blink_interval_ms = BLINK_NOT_MOUNTED; }
void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}
void tud_resume_cb(void) {
  blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

static bool audio10_set_req_ep(tusb_control_request_t const *req, uint8_t *buf) {
  if (TU_U16_HIGH(req->wValue) != AUDIO10_EP_CTRL_SAMPLING_FREQ) return false;
  if (req->bRequest != AUDIO10_CS_REQ_SET_CUR) return false;
  TU_VERIFY(req->wLength == 3);
  samp_freq = tu_unaligned_read32(buf) & 0x00FFFFFF;
  return samp_freq == 48000;
}

static bool audio10_get_req_ep(uint8_t rhport, tusb_control_request_t const *req) {
  if (TU_U16_HIGH(req->wValue) != AUDIO10_EP_CTRL_SAMPLING_FREQ) return false;
  if (req->bRequest != AUDIO10_CS_REQ_GET_CUR) return false;
  uint8_t freq[3] = {
      (uint8_t)(samp_freq & 0xFF),
      (uint8_t)((samp_freq >> 8) & 0xFF),
      (uint8_t)((samp_freq >> 16) & 0xFF),
  };
  return tud_audio_buffer_and_schedule_control_xfer(rhport, req, freq, sizeof(freq));
}

static bool audio10_set_req_entity(tusb_control_request_t const *req, uint8_t *buf) {
  uint8_t channel = TU_U16_LOW(req->wValue);
  uint8_t ctrl = TU_U16_HIGH(req->wValue);
  if (TU_U16_HIGH(req->wIndex) != 0x02) return false;
  TU_VERIFY(channel <= CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX);

  if (ctrl == AUDIO10_FU_CTRL_MUTE && req->bRequest == AUDIO10_CS_REQ_SET_CUR) {
    TU_VERIFY(req->wLength == 1);
    mute[channel] = buf[0];
    push_usb_gain();
    return true;
  }
  if (ctrl == AUDIO10_FU_CTRL_VOLUME && req->bRequest == AUDIO10_CS_REQ_SET_CUR) {
    TU_VERIFY(req->wLength == 2);
    volume[channel] = (int16_t)(tu_unaligned_read16(buf) / 256);
    push_usb_gain();
    return true;
  }
  return false;
}

static bool audio10_get_req_entity(uint8_t rhport, tusb_control_request_t const *req) {
  uint8_t channel = TU_U16_LOW(req->wValue);
  uint8_t ctrl = TU_U16_HIGH(req->wValue);
  if (TU_U16_HIGH(req->wIndex) != 0x02) return false;
  TU_VERIFY(channel <= CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX);

  if (ctrl == AUDIO10_FU_CTRL_MUTE) {
    return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &mute[channel], 1);
  }
  if (ctrl == AUDIO10_FU_CTRL_VOLUME) {
    int16_t value;
    if (req->bRequest == AUDIO10_CS_REQ_GET_CUR) value = (int16_t)(volume[channel] * 256);
    else if (req->bRequest == AUDIO10_CS_REQ_GET_MIN) value = (int16_t)(-90 * 256);
    else if (req->bRequest == AUDIO10_CS_REQ_GET_MAX) value = (int16_t)(30 * 256);
    else if (req->bRequest == AUDIO10_CS_REQ_GET_RES) value = (int16_t)(1 * 256);
    else return false;
    return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &value, sizeof(value));
  }
  return false;
}

bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *req, uint8_t *buf) {
  (void)rhport;
  if (tud_audio_version() != 1) return false;
  return audio10_set_req_ep(req, buf);
}

bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *req) {
  if (tud_audio_version() != 1) return false;
  return audio10_get_req_ep(rhport, req);
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *req, uint8_t *buf) {
  (void)rhport;
  if (tud_audio_version() != 1) return false;
  return audio10_set_req_entity(req, buf);
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *req) {
  if (tud_audio_version() != 1) return false;
  return audio10_get_req_entity(rhport, req);
}
