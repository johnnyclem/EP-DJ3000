cmake_minimum_required(VERSION 3.13)

# Pico SDK 2.1 or newer. Its TinyUSB must provide TUD_AUDIO10_DESC_* 
# (the UAC1 templates). Set PICO_SDK_PATH, or pass -DPICO_SDK_PATH=.
if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif ()
if (NOT PICO_SDK_PATH)
    message(FATAL_ERROR "Set PICO_SDK_PATH to a Pico SDK 2.1+ checkout")
endif ()
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(field_jog_audio C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

pico_sdk_init()

add_executable(field_jog_audio
    src/main.c
    src/usb_descriptors.c
    src/audio_out.c
    src/controls.c
    src/uart_dac.c
)

target_include_directories(field_jog_audio PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

target_link_libraries(field_jog_audio PRIVATE
    pico_stdlib
    tinyusb_device
    tinyusb_board
    hardware_i2c
    hardware_adc
    hardware_uart
    hardware_gpio
)

# UART0 is the optional DAC link. Don't put stdio on it.
pico_enable_stdio_uart(field_jog_audio 0)
pico_enable_stdio_usb(field_jog_audio 0)

pico_add_extra_outputs(field_jog_audio)
