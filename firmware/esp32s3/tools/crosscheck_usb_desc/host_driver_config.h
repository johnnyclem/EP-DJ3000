// TinyUSB configuration for the host build of TinyUSB's audio class driver (driver_check.cpp): the firmware's own
// (components/epdj_usb/tusb_override/epdj_tusb_config.h, which includes esp_tinyusb's tusb_config.h), with the RTOS
// swapped out because there is none on the host.
#pragma once
#define CFG_TUSB_MCU OPT_MCU_ESP32S3
#define DRAM_DMA_ALIGNED_ATTR
#include "epdj_tusb_config.h"
#undef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE
