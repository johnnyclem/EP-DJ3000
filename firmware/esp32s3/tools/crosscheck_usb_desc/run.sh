#!/bin/sh
# Byte-compares firmware/core's usb_desc builder with TinyUSB's own descriptor macros.
# usage: run.sh [path to TinyUSB src dir]
#   default: the copy esp_tinyusb brings in (run `pio run` or `idf.py build` once so it is downloaded)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
tusb=${1:-$root/firmware/esp32s3/managed_components/espressif__esp_tinyusb/tinyusb/src}
out=${TMPDIR:-/tmp}/epdj_crosscheck_usb_desc
[ -f "$tusb/tusb.h" ] || { echo "TinyUSB sources not found at $tusb" >&2; exit 2; }
g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
  -DCFG_TUSB_CONFIG_FILE='"host_tusb_config.h"' -I"$here" -I"$tusb" -I"$root/firmware/core/include" \
  "$here/crosscheck.cpp" "$root/firmware/core/src/usb_desc.cpp" -o "$out"
"$out"
