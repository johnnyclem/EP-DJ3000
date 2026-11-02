# DAC output: SPSC ring fed by the USB audio callbacks, drained by an I2S task (PCM5102A class, 48 kHz / 16 bit
# stereo, no MCLK). Knows nothing about TinyUSB: the USB layer pushes frames in and takes the feedback value out.
idf_component_register(
    SRCS "src/audio_out.cpp"
    INCLUDE_DIRS "include"
    REQUIRES epdj_core
    PRIV_REQUIRES epdj_hal esp_driver_i2s esp_driver_gpio freertos log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wshadow)
