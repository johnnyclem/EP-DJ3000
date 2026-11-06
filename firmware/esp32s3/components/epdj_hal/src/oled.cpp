#include "epdj_hal/oled.h"

#include <cstdio>
#include <cstring>

#include "epdj_hal/board_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace epdj {
namespace hal {
namespace {
const char* TAG = "oled";

constexpr int kWidth = 128;
constexpr int kHeight = 32;
constexpr int kPages = kHeight / 8;
constexpr int kFrame = kWidth * kPages;  // 512
constexpr uint8_t kAddrPrimary = 0x3C;
constexpr uint8_t kAddrAlt = 0x3D;

// 5x7, column-major, bit 0 at the top. Advance is 6 so glyphs do not touch. Missing characters are skipped.
struct Glyph {
  char ch;
  uint8_t col[5];
};
constexpr Glyph kFont[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}}, {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}}, {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x62, 0x51, 0x49, 0x49, 0x46}}, {'3', {0x22, 0x41, 0x49, 0x49, 0x36}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}}, {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3E, 0x49, 0x49, 0x49, 0x30}}, {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}}, {'9', {0x26, 0x49, 0x49, 0x49, 0x3E}},
    {'A', {0x7E, 0x09, 0x09, 0x09, 0x7E}}, {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}}, {'D', {0x7F, 0x41, 0x41, 0x41, 0x3E}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}}, {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x3A}}, {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x30, 0x40, 0x41, 0x3F, 0x01}}, {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}}, {'M', {0x7F, 0x02, 0x04, 0x02, 0x7F}},
    {'N', {0x7F, 0x02, 0x04, 0x08, 0x7F}}, {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}}, {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x26, 0x49, 0x49, 0x49, 0x32}}, {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}}, {'W', {0x7F, 0x20, 0x18, 0x20, 0x7F}},
    {'Y', {0x03, 0x04, 0x78, 0x04, 0x03}},
};

const uint8_t* glyph(char ch) {
  if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
  for (const Glyph& g : kFont)
    if (g.ch == ch) return g.col;
  return nullptr;
}

void drawText(uint8_t* fb, int page, const char* text) {
  int x = 0;
  for (const char* p = text; *p != '\0' && x + 5 < kWidth; ++p) {
    const uint8_t* col = glyph(*p);
    if (col == nullptr) continue;
    for (int i = 0; i < 5; ++i) fb[page * kWidth + x + i] = col[i];
    x += 6;
  }
}

bool tx(i2c_master_dev_handle_t dev, const uint8_t* data, size_t n) {
  return i2c_master_transmit(dev, data, n, 100) == ESP_OK;
}

// One transaction, control byte 0x00, then the command bytes. A 0xD5/0x8D/0xDA and its argument have to
// travel together. The 0.91" yellow/blue modules ignore a parameter that shows up in the next start, so the
// charge pump never enables and the glass stays black while every ACK still succeeds.
bool cmds(i2c_master_dev_handle_t dev, const uint8_t* c, size_t n) {
  uint8_t buf[48];
  if (n + 1 > sizeof buf) return false;
  buf[0] = 0x00;
  std::memcpy(buf + 1, c, n);
  return tx(dev, buf, n + 1);
}

// 128x32 SSD1306, the 0.91" yellow-over-blue glass. Charge pump on, COM pins sequential (0x02, not the
// 128x64 value 0x12).
bool initPanel(i2c_master_dev_handle_t dev) {
  const uint8_t seq[] = {
      0xAE,        // display off
      0xD5, 0x80,  // clock
      0xA8, 0x1F,  // multiplex 1/32
      0xD3, 0x00,  // display offset
      0x40,        // start line 0
      0x8D, 0x14,  // charge pump on, from the panel's 3V3
      0x20, 0x00,  // horizontal addressing
      0xA1,        // segment remap
      0xC8,        // COM scan from COM[N-1] to COM0
      0xDA, 0x02,  // COM pins, 128x32
      0x81, 0x8F,  // contrast
      0xD9, 0xF1,  // precharge, internal pump
      0xDB, 0x40,  // VCOMH
      0x2E,        // deactivate scroll
      0xA4,        // follow RAM
      0xA6,        // not inverted
  };
  if (!cmds(dev, seq, sizeof seq)) return false;
  vTaskDelay(pdMS_TO_TICKS(100));  // the pump needs this before the panel is switched on
  const uint8_t on[] = {0xAF};
  return cmds(dev, on, sizeof on);
}

bool push(i2c_master_dev_handle_t dev, const uint8_t* fb) {
  const uint8_t window[] = {0x21, 0x00, static_cast<uint8_t>(kWidth - 1), 0x22, 0x00, static_cast<uint8_t>(kPages - 1)};
  if (!cmds(dev, window, sizeof window)) return false;
  uint8_t pkt[1 + kWidth];
  pkt[0] = 0x40;
  for (int page = 0; page < kPages; ++page) {
    std::memcpy(pkt + 1, fb + page * kWidth, kWidth);
    if (!tx(dev, pkt, sizeof pkt)) return false;
  }
  return true;
}

}  // namespace

OledBringup oledBringup(I2cBus& bus, char deck, const char* line2) {
  OledBringup out;
  if (!bus.ok()) return out;
  uint8_t addr = 0;
  if (bus.probe(kAddrPrimary)) addr = kAddrPrimary;
  else if (bus.probe(kAddrAlt)) addr = kAddrAlt;
  if (addr == 0) {
    ESP_LOGW(TAG, "nothing answered at 0x%02X or 0x%02X (SDA GPIO%d, SCL GPIO%d, 3V3, GND). An SPI module will not ACK",
             kAddrPrimary, kAddrAlt, pins::kI2cSda, pins::kI2cScl);
    return out;
  }

  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = addr;
  dc.scl_speed_hz = 100000;  // the 0.91" modules drop bytes at 400 kHz on a long lead
  i2c_master_dev_handle_t dev = nullptr;
  if (i2c_master_bus_add_device(bus.handle(), &dc, &dev) != ESP_OK) return out;

  uint8_t fb[kFrame];
  std::memset(fb, 0, sizeof fb);
  std::memset(fb, 0xFF, kWidth);  // page 0 is the yellow band: a solid row, so a dark glass is not a blank font
  char deckLine[12];
  std::snprintf(deckLine, sizeof deckLine, "DECK %c", deck);
  drawText(fb, 0, "EP-DJ3000");
  drawText(fb, 1, deckLine);
  drawText(fb, 2, line2 != nullptr ? line2 : "");
  drawText(fb, 3, "SDA21 SCL47");

  const bool ok = initPanel(dev) && push(dev, fb);
  if (!ok) {
    ESP_LOGE(TAG, "SSD1306 at 0x%02X answered but the init write failed", addr);
    return out;
  }
  ESP_LOGI(TAG, "SSD1306 at 0x%02X, 128x32, on SDA GPIO%d SCL GPIO%d", addr, pins::kI2cSda, pins::kI2cScl);
  out.present = true;
  out.address = addr;
  return out;
}

}  // namespace hal
}  // namespace epdj
