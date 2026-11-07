idf_component_register(
    SRCS "src/sd_card.cpp"
    INCLUDE_DIRS "include"
    REQUIRES epdj_core epdj_hal epdj_audio fatfs esp_driver_sdmmc
    PRIV_REQUIRES sdmmc vfs esp_driver_gpio log freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wshadow)
