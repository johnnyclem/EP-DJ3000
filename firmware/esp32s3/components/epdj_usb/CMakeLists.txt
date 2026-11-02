# USB device: composite MIDI (+ UAC2 audio when a DAC is present), on esp_tinyusb with our own descriptors.
idf_component_register(
    SRCS "src/usb_device.cpp" "src/usb_audio.cpp"
    INCLUDE_DIRS "include"
    REQUIRES epdj_core
    PRIV_REQUIRES epdj_audio espressif__esp_tinyusb esp_timer freertos log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wshadow)

# esp_tinyusb's own tusb_config.h has no UAC support, and the TinyUSB sources are compiled from that header.
# TinyUSB reads CFG_TUSB_CONFIG_FILE instead of tusb_config.h when it is defined, so point every translation
# unit that includes tusb.h (the esp_tinyusb library and everything linking it) at tusb_override/epdj_tusb_config.h,
# which includes the original and adds the audio class and bigger MIDI FIFOs.
idf_component_get_property(tusb_lib espressif__esp_tinyusb COMPONENT_LIB)
target_compile_definitions(${tusb_lib} PUBLIC "CFG_TUSB_CONFIG_FILE=\"epdj_tusb_config.h\"")
target_include_directories(${tusb_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/tusb_override")
