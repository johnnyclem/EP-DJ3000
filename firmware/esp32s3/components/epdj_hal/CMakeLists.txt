# Board support: everything that touches a pin except USB, I2S audio and the radio.
# `driver` is the legacy component that still carries the touch-sensor v2 driver (and the legacy I2C one).
idf_component_register(
    SRCS "src/adc_inputs.cpp" "src/as5600.cpp" "src/buttons.cpp" "src/dac_detect.cpp" "src/i2c_bus.cpp"
         "src/nvs_store.cpp" "src/oled.cpp" "src/status_led.cpp" "src/touch_pads.cpp" "src/trs_midi.cpp"
    INCLUDE_DIRS "include"
    # Public: the headers expose these components' types (adc handle, I2C bus handle, RMT channel, queue, timer).
    REQUIRES epdj_core esp_adc esp_driver_i2c esp_driver_rmt esp_timer freertos
    PRIV_REQUIRES driver esp_driver_gpio esp_driver_uart nvs_flash esp_hw_support esp_rom log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wshadow)
