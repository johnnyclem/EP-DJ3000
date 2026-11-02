// Minimal TinyUSB configuration so tusb.h can be included on a desktop compiler: only the descriptor macros
// are used, no stack code is built.
#pragma once
#define CFG_TUSB_MCU OPT_MCU_ESP32S3
#define CFG_TUSB_OS OPT_OS_NONE
#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_AUDIO 1
#define CFG_TUD_MIDI 1
#define CFG_TUD_AUDIO_ENABLE_EP_OUT 1
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP 1
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX 196
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ 784
