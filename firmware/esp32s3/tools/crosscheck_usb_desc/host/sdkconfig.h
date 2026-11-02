// Stand-in for the generated sdkconfig.h when the firmware's TinyUSB configuration is read on the host
// (driver_check.cpp). Only what epdj_tusb_config.h, esp_tinyusb's tusb_config.h and usb_audio.cpp look at.
#pragma once
#define CONFIG_EPDJ_USB_AUDIO 1
#define CONFIG_TINYUSB_MIDI_COUNT 1  // sdkconfig.defaults
