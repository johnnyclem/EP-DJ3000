#include "epdj_hal/nvs_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace epdj {
namespace hal {
namespace {
const char* TAG = "nvs";
const char* kNamespace = "epdj";
const char* kKey = "cfg";
}  // namespace

bool NvsConfigStore::begin() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "partition needs erasing (%s)", esp_err_to_name(err));
    if (nvs_flash_erase() != ESP_OK) return false;
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

size_t NvsConfigStore::load(uint8_t* buf, size_t cap) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return 0;  // namespace not created yet: first boot
  size_t len = 0;
  size_t result = 0;
  if (nvs_get_blob(h, kKey, nullptr, &len) == ESP_OK && len != 0) {
    // A blob larger than the caller's buffer is reported by its size and not copied (the core rejects it).
    if (len > cap) result = len;
    else if (nvs_get_blob(h, kKey, buf, &len) == ESP_OK) result = len;
  }
  nvs_close(h);
  return result;
}

bool NvsConfigStore::save(const uint8_t* blob, size_t len) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_set_blob(h, kKey, blob, len);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

}  // namespace hal
}  // namespace epdj
