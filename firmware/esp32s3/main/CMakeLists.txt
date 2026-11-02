idf_component_register(
    SRCS "main.cpp" "engine_task.cpp" "device_platform.cpp" "diag_task.cpp"
    INCLUDE_DIRS "."
    REQUIRES epdj_core epdj_hal epdj_usb epdj_audio epdj_link
    PRIV_REQUIRES esp_timer esp_system esp_hw_support soc freertos log nvs_flash)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wshadow)
