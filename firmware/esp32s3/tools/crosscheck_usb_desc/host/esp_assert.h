// Stand-in for ESP-IDF's esp_assert.h (used by esp_tinyusb's tusb_config.h).
#pragma once
#define ESP_STATIC_ASSERT(c, ...) _Static_assert(c, __VA_ARGS__)
