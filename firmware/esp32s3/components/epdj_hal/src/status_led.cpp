#include "epdj_hal/status_led.h"

#include "epdj_hal/board_pins.h"
#include "esp_log.h"

namespace epdj {
namespace hal {
namespace {
const char* TAG = "led";
constexpr uint32_t kResolutionHz = 10000000;  // 0.1 us per RMT tick

StatusLed::Rgb scale(StatusLed::Rgb c, unsigned num, unsigned den) {
  return {static_cast<uint8_t>(c.r * num / den), static_cast<uint8_t>(c.g * num / den),
          static_cast<uint8_t>(c.b * num / den)};
}
}  // namespace

bool StatusLed::begin() {
  rmt_tx_channel_config_t cc = {};
  cc.gpio_num = static_cast<gpio_num_t>(pins::kStatusLed);
  cc.clk_src = RMT_CLK_SRC_DEFAULT;
  cc.resolution_hz = kResolutionHz;
  cc.mem_block_symbols = 64;
  cc.trans_queue_depth = 2;
  if (rmt_new_tx_channel(&cc, &chan_) != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_tx_channel failed");
    chan_ = nullptr;
    return false;
  }
  // WS2812 bit timing at 0.1 us/tick: '0' = 0.3 us high, 0.9 us low; '1' = 0.9 us high, 0.3 us low.
  rmt_bytes_encoder_config_t bc = {};
  bc.bit0.level0 = 1; bc.bit0.duration0 = 3; bc.bit0.level1 = 0; bc.bit0.duration1 = 9;
  bc.bit1.level0 = 1; bc.bit1.duration0 = 9; bc.bit1.level1 = 0; bc.bit1.duration1 = 3;
  bc.flags.msb_first = 1;
  if (rmt_new_bytes_encoder(&bc, &encoder_) != ESP_OK || rmt_enable(chan_) != ESP_OK) {
    ESP_LOGE(TAG, "rmt encoder/enable failed");
    return false;
  }
  set({0, 0, 0});
  return true;
}

void StatusLed::set(Rgb c) {
  if (chan_ == nullptr) return;
  const uint8_t grb[3] = {c.g, c.r, c.b};
  rmt_transmit_config_t tc = {};
  tc.loop_count = 0;
  // The previous frame is a few tens of microseconds long and we are called every few milliseconds, so this
  // wait is a formality. An idle-low line for >50 us latches the frame (no explicit reset symbol).
  rmt_tx_wait_all_done(chan_, 2);
  rmt_transmit(chan_, encoder_, grb, sizeof grb, &tc);
  last_ = c;
  haveLast_ = true;
}

StatusLed::Rgb StatusLed::colorFor(const LedStatus& s, uint64_t nowUs) {
  if (s.identify) return {255, 255, 255};
  if (s.chordStage == 2) return {255, 0, 0};    // releasing now swaps decks
  if (s.chordStage == 1) return {255, 160, 0};  // releasing now cycles the mode
  if (s.beatFlash) return {255, 255, 255};

  Rgb c = s.deck == proto::Deck::A ? Rgb{0, 110, 255} : Rgb{255, 70, 0};
  const uint32_t ms = static_cast<uint32_t>(nowUs / 1000);
  switch (s.link) {
    case LedStatus::LinkLed::Searching:  // a short bright blink every 2 s, dim in between
      c = (ms % 2000) < 150 ? c : scale(c, 1, 5);
      break;
    case LedStatus::LinkLed::Conflict:   // two units claim the same deck: alternate with red
      c = (ms % 500) < 250 ? c : Rgb{255, 0, 0};
      break;
    case LedStatus::LinkLed::Bridging:   // a bit of white means "carrying the other deck's MIDI"
      c.r = static_cast<uint8_t>(c.r + (255 - c.r) / 4);
      c.g = static_cast<uint8_t>(c.g + (255 - c.g) / 4);
      c.b = static_cast<uint8_t>(c.b + (255 - c.b) / 4);
      break;
    default:
      break;
  }
  if (!s.touched) c = scale(c, 1, 2);
  if (s.cueLit) c = {255, 255, 255};
  return scale(c, s.brightness, 255);
}

void StatusLed::show(const LedStatus& s, uint64_t nowUs) {
  const Rgb c = colorFor(s, nowUs);
  if (!haveLast_ || !(c == last_)) set(c);
}

}  // namespace hal
}  // namespace epdj
