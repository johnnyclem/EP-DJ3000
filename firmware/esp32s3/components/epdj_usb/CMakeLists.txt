# USB device: composite MIDI (+ USB Audio 1.0 when a DAC is present), on esp_tinyusb with our own descriptors.
idf_component_register(
    SRCS "src/usb_device.cpp" "src/usb_audio.cpp"
    INCLUDE_DIRS "include"
    REQUIRES epdj_core
    PRIV_REQUIRES epdj_audio espressif__esp_tinyusb esp_timer freertos log soc)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wshadow)

# esp_tinyusb's own tusb_config.h has no audio class, and the TinyUSB sources are compiled from that header.
# TinyUSB reads CFG_TUSB_CONFIG_FILE instead of tusb_config.h when it is defined, so point every translation
# unit that includes tusb.h (the esp_tinyusb library and everything linking it) at tusb_override/epdj_tusb_config.h,
# which includes the original and adds the audio class and bigger MIDI FIFOs.
idf_component_get_property(tusb_lib espressif__esp_tinyusb COMPONENT_LIB)
target_compile_definitions(${tusb_lib} PUBLIC "CFG_TUSB_CONFIG_FILE=\"epdj_tusb_config.h\"")
target_include_directories(${tusb_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/tusb_override")

# TinyUSB's audio class driver stops the stream on a zero-length OUT packet (BRINGUP.md risk 3). Compile a patched
# copy of audio_device.c instead of the original; the copy lives in the build directory.
include("${CMAKE_CURRENT_LIST_DIR}/tinyusb_audio_patch.cmake")
idf_component_get_property(tusb_dir espressif__esp_tinyusb COMPONENT_DIR)
set(tusb_audio_orig "${tusb_dir}/tinyusb/src/class/audio/audio_device.c")
set(tusb_audio_patched "${CMAKE_BINARY_DIR}/epdj_patched/audio_device.c")
epdj_patch_tinyusb_audio("${tusb_audio_orig}" "${tusb_audio_patched}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${tusb_audio_orig}"
             "${CMAKE_CURRENT_LIST_DIR}/tinyusb_audio_patch.cmake")
get_target_property(tusb_srcs ${tusb_lib} SOURCES)
set(tusb_srcs_patched)
set(tusb_audio_swapped FALSE)
foreach(src ${tusb_srcs})
    get_filename_component(src_abs "${src}" ABSOLUTE BASE_DIR "${tusb_dir}")
    if(src_abs STREQUAL tusb_audio_orig)
        list(APPEND tusb_srcs_patched "${tusb_audio_patched}")
        set(tusb_audio_swapped TRUE)
    else()
        list(APPEND tusb_srcs_patched "${src}")
    endif()
endforeach()
if(NOT tusb_audio_swapped)
    message(FATAL_ERROR "epdj_usb: esp_tinyusb does not compile ${tusb_audio_orig}; the audio patch has nothing to replace")
endif()
set_property(TARGET ${tusb_lib} PROPERTY SOURCES ${tusb_srcs_patched})
