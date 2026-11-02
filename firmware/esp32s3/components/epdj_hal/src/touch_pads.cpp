#include "epdj_hal/touch_pads.h"

#include "driver/touch_pad.h"
#include "epdj_hal/board_pins.h"
#include "epdj_hal/touch_range.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace epdj {
namespace hal {
namespace {
const char* TAG = "touch";

// Longest scan that still gives every pad a new value on each 1 ms engine read, with some headroom.
constexpr unsigned kSlowScanWarnUs = 1200;
// Used when the scan cannot be timed. About 1 ms for eight ordinary pads; auto-ranging replaces it.
constexpr uint32_t kFallbackCycles = 48;
constexpr uint32_t kMinCycles = 8;

touch_pad_t padFor(unsigned sector) { return static_cast<touch_pad_t>(pins::kTouchFirst + static_cast<int>(sector)); }

uint32_t readHw(unsigned sector) {
  uint32_t v = 0;
  if (touch_pad_read_raw_data(padFor(sector), &v) != ESP_OK) return 0;
  return v;
}

// Changes the measurement length and lets the new setting reach the pads' registers.
void setCycles(uint32_t cycles) {
  touch_pad_fsm_stop();
  touch_pad_set_charge_discharge_times(static_cast<uint16_t>(cycles));
  touch_pad_fsm_start();
  vTaskDelay(pdMS_TO_TICKS(30));
}

// Duration of one full scan, from the sensor's own "channel being measured" register: the time between
// successive entries into the channel it was on when we started, averaged over up to 12 revolutions. 0 = failed
// (the register never changed or came back fewer than 3 times in 150 ms).
uint32_t measureScanUs() {
  constexpr int64_t kWindowUs = 150000;
  constexpr unsigned kRevolutions = 12;
  const int64_t start = esp_timer_get_time();
  const touch_pad_t anchor = touch_pad_get_current_meas_channel();
  bool away = false;
  unsigned entries = 0;
  int64_t first = 0, last = 0;
  while (esp_timer_get_time() - start < kWindowUs && entries <= kRevolutions) {
    const touch_pad_t ch = touch_pad_get_current_meas_channel();
    if (ch != anchor) {
      away = true;
    } else if (away) {
      away = false;
      last = esp_timer_get_time();
      if (entries++ == 0) first = last;
    }
    esp_rom_delay_us(5);
  }
  if (entries < 3) return 0;
  return static_cast<uint32_t>((last - first) / static_cast<int64_t>(entries - 1));
}

// Shortens the measurement until one scan of all pads takes about CONFIG_EPDJ_TOUCH_SCAN_TARGET_US. Returns the
// scan duration at the final setting (0 if it could not be measured) and sets *cycles.
uint32_t autoRange(uint32_t cap, uint32_t* cycles) {
  using touch_range::Probe;
  const Probe hi{cap, measureScanUs()};
  const uint32_t loCycles = cap / 4 > kMinCycles ? cap / 4 : kMinCycles;
  setCycles(loCycles);
  const Probe lo{loCycles, measureScanUs()};
  bool clamped = false;
  uint32_t chosen = touch_range::cyclesForScan(lo, hi, CONFIG_EPDJ_TOUCH_SCAN_TARGET_US, kMinCycles, cap, &clamped);
  if (chosen == 0) {
    chosen = cap < kFallbackCycles ? cap : kFallbackCycles;
    ESP_LOGW(TAG, "could not time the scan (%lu us at %lu cycles, %lu us at %lu): using %lu cycles",
             static_cast<unsigned long>(hi.scanUs), static_cast<unsigned long>(hi.cycles),
             static_cast<unsigned long>(lo.scanUs), static_cast<unsigned long>(lo.cycles), static_cast<unsigned long>(chosen));
  } else {
    ESP_LOGI(TAG, "scan %lu us at %lu cycles, %lu us at %lu: target %d us -> %lu cycles%s", static_cast<unsigned long>(hi.scanUs),
             static_cast<unsigned long>(hi.cycles), static_cast<unsigned long>(lo.scanUs), static_cast<unsigned long>(lo.cycles),
             CONFIG_EPDJ_TOUCH_SCAN_TARGET_US, static_cast<unsigned long>(chosen),
             clamped ? " (limit reached: target not met)" : "");
  }
  *cycles = chosen;
  setCycles(chosen);
  return measureScanUs();
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
  // capacitance, so a finger raises it. The pads are measured one after another, so the cycles also set how
  // stale a pad's value is: see touch_range.h and the auto-range below.
  cycles_ = CONFIG_EPDJ_TOUCH_CHARGE_DISCHARGE_TIMES;
  touch_pad_set_charge_discharge_times(static_cast<uint16_t>(cycles_));
  touch_pad_set_measurement_interval(CONFIG_EPDJ_TOUCH_INTERVAL_CYCLES);
  touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
  if (touch_pad_fsm_start() != ESP_OK) {
    ESP_LOGE(TAG, "touch_pad_fsm_start failed");
    return false;
  }
  count_ = count;

  // Let the scan run (the first measurements after init are not valid), then fit the measurement length.
  vTaskDelay(pdMS_TO_TICKS(100));
#if CONFIG_EPDJ_TOUCH_SCAN_TARGET_US > 0
  if (count >= 2) {
    uint32_t chosen = cycles_;
    scanUs_ = autoRange(cycles_, &chosen);
    cycles_ = chosen;
  } else {
    scanUs_ = 0;
  }
#else
  scanUs_ = count >= 2 ? measureScanUs() : 0;
#endif

  // The count is a property of the pad and wiring, not of the firmware, so the shift is measured rather than assumed.
  uint32_t peak = 0;
  for (unsigned i = 0; i < count; ++i) {
    const uint32_t v = readHw(i);
    if (v > peak) peak = v;
  }
  shift_ = 0;
  while ((peak >> shift_) > 0x8000u) ++shift_;
  ESP_LOGI(TAG, "%u pad(s) on GPIO%d..%d, %u cycles per pad, idle peak count %lu, shift %u", count, pins::kTouchFirst,
           pins::kTouchFirst + static_cast<int>(count) - 1, cycles_, static_cast<unsigned long>(peak), shift_);
  if (scanUs_ != 0) {
    ESP_LOGI(TAG, "full scan of %u pads takes %u us (idle)", count, scanUs_);
    if (scanUs_ > kSlowScanWarnUs) {
      ESP_LOGW(TAG, "scan is slower than the 1 ms engine tick: each pad's value is stale for several reads and the pads are "
                    "not sampled at the same instant (lower EPDJ_TOUCH_CHARGE_DISCHARGE_TIMES, or shorten the pad wiring)");
    }
  } else if (count >= 2) {
    ESP_LOGW(TAG, "scan period not measurable: the pads' refresh rate is unknown");
  }
  if (peak < 200) ESP_LOGW(TAG, "idle counts are tiny: pads not connected, or too few charge/discharge cycles");
  return true;
}

void TouchPads::read(uint16_t raw[kMax]) {
  for (unsigned i = 0; i < kMax; ++i) {
    if (i >= count_) {
      raw[i] = 0;
      continue;
    }
    const uint32_t v = readHw(i) >> shift_;
    raw[i] = v > 0xFFFFu ? static_cast<uint16_t>(0xFFFFu) : static_cast<uint16_t>(v);
  }
}

}  // namespace hal
}  // namespace epdj
