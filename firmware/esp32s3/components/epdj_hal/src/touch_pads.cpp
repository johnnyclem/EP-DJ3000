#include "epdj_hal/touch_pads.h"

#include "driver/touch_pad.h"
#include "epdj_hal/board_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace epdj {
namespace hal {
namespace {
const char* TAG = "touch";

touch_pad_t padFor(unsigned sector) { return static_cast<touch_pad_t>(pins::kTouchFirst + static_cast<int>(sector)); }

uint32_t readHw(unsigned sector) {
  uint32_t v = 0;
  if (touch_pad_read_raw_data(padFor(sector), &v) != ESP_OK) return 0;
  return v;
}
}  // namespace

bool TouchPads::begin(unsigned count) {
  if (count == 0 || count > kMax) return false;
  if (touch_pad_init() != ESP_OK) {
    ESP_LOGE(TAG, "touch_pad_init failed");
    return false;
  }
  for (unsigned i = 0; i < count; ++i) {
    if (touch_pad_config(padFor(i)) != ESP_OK) {
      ESP_LOGE(TAG, "touch_pad_config(GPIO%d) failed", pins::kTouchFirst + static_cast<int>(i));
      return false;
    }
  }
  // Raw count = RTC_FAST clock cycles taken by this many charge/discharge cycles: it grows with the pad's
  // capacitance, so a finger raises it. Fewer cycles = faster scan, coarser counts.
  touch_pad_set_charge_discharge_times(CONFIG_EPDJ_TOUCH_CHARGE_DISCHARGE_TIMES);
  touch_pad_set_measurement_interval(CONFIG_EPDJ_TOUCH_INTERVAL_CYCLES);
  touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
  if (touch_pad_fsm_start() != ESP_OK) {
    ESP_LOGE(TAG, "touch_pad_fsm_start failed");
    return false;
  }
  count_ = count;

  // Let the scan run, then choose the shift from the largest idle count. The count is a property of the
  // pad and wiring, not of the firmware, so it is measured rather than assumed.
  vTaskDelay(pdMS_TO_TICKS(100));
  uint32_t peak = 0;
  for (unsigned i = 0; i < count; ++i) {
    const uint32_t v = readHw(i);
    if (v > peak) peak = v;
  }
  shift_ = 0;
  while ((peak >> shift_) > 0x8000u) ++shift_;
  ESP_LOGI(TAG, "%u pad(s) on GPIO%d..%d, idle peak count %lu, shift %u", count, pins::kTouchFirst,
           pins::kTouchFirst + static_cast<int>(count) - 1, static_cast<unsigned long>(peak), shift_);
  if (peak < 200) ESP_LOGW(TAG, "idle counts are tiny: pads not connected, or too few charge/discharge cycles");
  return true;
}

void TouchPads::read(uint16_t raw[kMax]) {
  bool any = false;
  for (unsigned i = 0; i < kMax; ++i) {
    if (i >= count_) {
      raw[i] = 0;
      continue;
    }
    const uint32_t v = readHw(i) >> shift_;
    raw[i] = v > 0xFFFFu ? static_cast<uint16_t>(0xFFFFu) : static_cast<uint16_t>(v);
    if (raw[i] != last_[i]) any = true;
    last_[i] = raw[i];
  }
  // 1-second window for the freshness estimate.
  ++reads_;
  if (any) ++changed_;
  if (reads_ >= 1000) {
    fresh_ = changed_ * 100u / reads_;
    reads_ = changed_ = 0;
  }
}

}  // namespace hal
}  // namespace epdj
