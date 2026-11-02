# ESP-NOW transport for the core's radio link: broadcast peer, fixed channel, callbacks only copy into queues.
idf_component_register(
    SRCS "src/radio.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_event esp_netif esp_hw_support esp_timer freertos log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wshadow)
