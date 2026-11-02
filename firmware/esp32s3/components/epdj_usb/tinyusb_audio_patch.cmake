# One-line fix to TinyUSB's audio class driver, applied to a copy at configure time (the managed esp_tinyusb
# component is regenerated, so it is never edited in place).
#
#   audiod_rx_xfer_isr() writes every received isochronous OUT packet into its FIFO with
#       TU_VERIFY(0 < tu_fifo_write_n(...));
#   which returns from the function on a zero-length packet BEFORE the endpoint is armed for the next frame, so
#   one such packet (legal on an isochronous endpoint) ends the stream until the host re-selects the interface.
#   The patch skips the FIFO write for an empty packet and re-arms as usual.
#
# Included by CMakeLists.txt (defines the function) and run as a script by tools/crosscheck_usb_desc/run.sh:
#   cmake -DEPDJ_IN=<audio_device.c> -DEPDJ_OUT=<patched copy> -P tinyusb_audio_patch.cmake
#
# Fails loudly when the lines are not there: either TinyUSB fixed this (then delete this file and its use in
# CMakeLists.txt) or moved the code (then update the patterns).
function(epdj_patch_tinyusb_audio in out)
  file(READ "${in}" src)

  set(old_write "  TU_VERIFY(0 < tu_fifo_write_n(&audio->ep_out_ff, audio->lin_buf_out, n_bytes_received));")
  set(new_write "  // EPDJ PATCH: an empty isochronous packet is valid; it must not skip re-arming the endpoint\n  if (n_bytes_received > 0) {\n    TU_VERIFY(0 < tu_fifo_write_n(&audio->ep_out_ff, audio->lin_buf_out, n_bytes_received));\n  }")
  # The copy lives in the build directory, so the include that relied on the file's own directory needs a path.
  set(old_inc "#include \"audio_device.h\"")
  set(new_inc "#include \"class/audio/audio_device.h\"")

  string(FIND "${src}" "${old_write}" pos_write)
  string(FIND "${src}" "${old_inc}" pos_inc)
  if(pos_write EQUAL -1 OR pos_inc EQUAL -1)
    message(FATAL_ERROR "epdj_usb: ${in} no longer has the lines tinyusb_audio_patch.cmake patches "
                        "(zero-length OUT packet handling in audiod_rx_xfer_isr). If TinyUSB fixed it upstream, delete "
                        "the patch step; otherwise update the patterns.")
  endif()
  string(REPLACE "${old_write}" "${new_write}" patched "${src}")
  string(REPLACE "${old_inc}" "${new_inc}" patched "${patched}")

  # Do not touch the copy when nothing changed, so an unchanged build does not recompile it.
  if(EXISTS "${out}")
    file(READ "${out}" current)
    if(current STREQUAL patched)
      return()
    endif()
  endif()
  get_filename_component(out_dir "${out}" DIRECTORY)
  file(MAKE_DIRECTORY "${out_dir}")
  file(WRITE "${out}" "${patched}")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
  epdj_patch_tinyusb_audio("${EPDJ_IN}" "${EPDJ_OUT}")
endif()
