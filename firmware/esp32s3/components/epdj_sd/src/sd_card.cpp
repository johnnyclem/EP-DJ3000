#include "epdj_sd/sd_card.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include <dirent.h>
#include "driver/gpio.h"
#include "epdj/library.h"
#include "epdj_audio/audio_out.h"
#include "epdj_hal/board_pins.h"
#include "epdj_hal/oled.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

namespace epdj {
namespace sd {
namespace {

const char* TAG = "sd";

enum class Cmd : uint8_t { None = 0, Play = 1, Pause = 2, Stop = 3 };

library::Catalog g_cat;
library::Player g_player;
FILE* g_file = nullptr;
int g_open = -1;

std::atomic<bool> g_capture{false};
std::atomic<uint8_t> g_cmd{0};
std::atomic<int> g_index{0};
std::atomic<uint8_t> g_state{static_cast<uint8_t>(library::PlayState::Empty)};

bool pressed(int gpio) { return gpio_get_level(static_cast<gpio_num_t>(gpio)) == 0; }

size_t fileRead(void* ctx, uint8_t* dst, size_t n) {
  return std::fread(dst, 1, n, static_cast<FILE*>(ctx));
}
bool fileSeek(void* ctx, uint32_t offset) {
  return std::fseek(static_cast<FILE*>(ctx), static_cast<long>(offset), SEEK_SET) == 0;
}

void openIndex(int index) {
  if (g_file != nullptr) {
    std::fclose(g_file);
    g_file = nullptr;
  }
  g_open = -1;
  g_player.reset();
  if (index < 0 || index >= g_cat.count()) return;
  char path[8 + library::kNameLen];
  std::snprintf(path, sizeof path, "/sd/%s", g_cat.name(index));
  g_file = std::fopen(path, "rb");
  if (g_file == nullptr) {
    ESP_LOGW(TAG, "open failed: %s", path);
    return;
  }
  const library::WavError err = g_player.load(&fileRead, &fileSeek, g_file);
  if (err != library::WavError::Ok) {
    ESP_LOGW(TAG, "%s is not 16-bit PCM WAV (%u)", path, static_cast<unsigned>(err));
    std::fclose(g_file);
    g_file = nullptr;
    return;
  }
  g_open = index;
}

// Runs on the audio task. The browser task only stores a command.
size_t pull(int16_t* dst, size_t frames, void*) {
  const Cmd cmd = static_cast<Cmd>(g_cmd.exchange(0, std::memory_order_acq_rel));
  const int index = g_index.load(std::memory_order_relaxed);
  if (cmd == Cmd::Stop) {
    g_player.stop();
  } else if (cmd == Cmd::Pause) {
    g_player.pause();
  } else if (cmd == Cmd::Play) {
    if (g_open != index) openIndex(index);
    g_player.play();
  }
  g_state.store(static_cast<uint8_t>(g_player.state()), std::memory_order_relaxed);
  return g_player.pull(dst, frames);
}

void draw(int index) {
  library::Screen screen;
  library::formatScreen(screen, g_cat, index, static_cast<library::PlayState>(g_state.load(std::memory_order_relaxed)));
  hal::oledLines(screen.top, screen.bottom);
}

void browserTask(void*) {
  bool cueWas = false;
  bool revWas = false;
  int heldMs = 0;
  bool holdUsed = false;
  int shown = -1;
  auto shownState = library::PlayState::Empty;

  for (;;) {
    const bool cue = pressed(pins::kCue);
    const bool rev = pressed(pins::kRev);
    const bool captured = g_capture.load(std::memory_order_relaxed);

    if (captured) {
      if (cue && !cueWas) {
        const auto st = static_cast<library::PlayState>(g_state.load(std::memory_order_relaxed));
        if (st == library::PlayState::Playing) {
          g_cmd.store(static_cast<uint8_t>(Cmd::Pause), std::memory_order_release);
        } else {
          g_cmd.store(static_cast<uint8_t>(Cmd::Play), std::memory_order_release);
        }
      }
      if (rev) {
        heldMs += 20;
        if (!holdUsed && heldMs >= 800) {
          // Give the buttons back, but only once the finger is up, so the engine does not see the tail.
          holdUsed = true;
          g_cmd.store(static_cast<uint8_t>(Cmd::Stop), std::memory_order_release);
        }
      } else if (revWas) {
        if (holdUsed) {
          g_capture.store(false, std::memory_order_relaxed);
          hal::oledLines("DECK", "REV HOLD = CARD");
          shown = -2;
        } else if (heldMs > 0) {
          const auto st = static_cast<library::PlayState>(g_state.load(std::memory_order_relaxed));
          if (st == library::PlayState::Playing || st == library::PlayState::Paused) {
            g_cmd.store(static_cast<uint8_t>(Cmd::Stop), std::memory_order_release);
          } else if (g_cat.count() > 0) {
            const int cur = g_index.load(std::memory_order_relaxed);
            g_index.store((cur + 1) % g_cat.count(), std::memory_order_relaxed);
          }
        }
        heldMs = 0;
        holdUsed = false;
      }
    } else if (rev) {
      heldMs += 20;
      if (!holdUsed && heldMs >= 800 && g_cat.count() > 0) {
        g_capture.store(true, std::memory_order_relaxed);
        holdUsed = true;
        shown = -1;
      }
    } else {
      heldMs = 0;
      holdUsed = false;
    }

    cueWas = cue;
    revWas = rev;

    if (g_capture.load(std::memory_order_relaxed)) {
      const int index = g_index.load(std::memory_order_relaxed);
      const auto st = static_cast<library::PlayState>(g_state.load(std::memory_order_relaxed));
      if (index != shown || st != shownState) {
        draw(index);
        shown = index;
        shownState = st;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

bool mountAndScan() {
  esp_vfs_fat_sdmmc_mount_config_t mnt = {};
  mnt.format_if_mount_failed = false;
  mnt.max_files = 3;
  mnt.allocation_unit_size = 16 * 1024;

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 20 MHz. The wires inside the shell are short, but not a controlled pair.

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk = static_cast<gpio_num_t>(pins::kSdClk);
  slot.cmd = static_cast<gpio_num_t>(pins::kSdCmd);
  slot.d0 = static_cast<gpio_num_t>(pins::kSdD0);
  slot.d1 = GPIO_NUM_NC;
  slot.d2 = GPIO_NUM_NC;
  slot.d3 = GPIO_NUM_NC;  // not driven. The socket pulls DAT3 up so the card stays in SD mode.
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  sdmmc_card_t* card = nullptr;
  const esp_err_t err = esp_vfs_fat_sdmmc_mount("/sd", &host, &slot, &mnt, &card);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "no card (%s). CLK GPIO%d CMD GPIO%d D0 GPIO%d, 3V3 and GND, DAT3 pulled up",
             esp_err_to_name(err), pins::kSdClk, pins::kSdCmd, pins::kSdD0);
    return false;
  }
  ESP_LOGI(TAG, "card %.16s, %llu MB", card->cid.name, static_cast<unsigned long long>(card->csd.capacity) *
                                                           card->csd.sector_size / (1024ull * 1024ull));

  DIR* dir = opendir("/sd");
  if (dir == nullptr) return false;
  int skipped = 0;
  while (dirent* de = readdir(dir)) {
    const char* name = de->d_name;
    const size_t n = std::strlen(name);
    if (n < 5) continue;
    const char* ext = name + n - 4;
    const bool wav = (ext[0] == '.') && (ext[1] == 'w' || ext[1] == 'W') && (ext[2] == 'a' || ext[2] == 'A') &&
                     (ext[3] == 'v' || ext[3] == 'V');
    if (!wav) continue;
    if (!g_cat.add(name)) ++skipped;
  }
  closedir(dir);
  ESP_LOGI(TAG, "%d wav file%s in /sd%s", g_cat.count(), g_cat.count() == 1 ? "" : "s",
           skipped > 0 ? " (some names were too long or the list is full at 48)" : "");
  return g_cat.count() > 0;
}

}  // namespace

bool buttonsCaptured() { return g_capture.load(std::memory_order_relaxed); }

void start(hal::I2cBus&) {
  if (!mountAndScan()) return;
  audio_out::setLocalSource(&pull, nullptr);
  g_index.store(0, std::memory_order_relaxed);
  g_capture.store(true, std::memory_order_relaxed);
  draw(0);
  if (xTaskCreate(browserTask, "sd", 4096, nullptr, 4, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "browser task failed");
    g_capture.store(false, std::memory_order_relaxed);
  }
}

}  // namespace sd
}  // namespace epdj
