#!/bin/sh
# Two host-side checks of firmware/core's USB descriptor builder against TinyUSB itself:
#   1. crosscheck.cpp: the builder's bytes against the same descriptors written with TinyUSB's own TUD_* macros;
#   2. driver_check.cpp: TinyUSB's real audio class driver (audio_device.c, compiled for the host) opening those
#      descriptors, with this firmware's own audio callbacks (epdj_usb/src/usb_audio.cpp) answering it.
# Nothing here runs on a chip. Exit status is non-zero if either check fails.
#
# usage: run.sh [path to TinyUSB src dir]
#   default: the copy esp_tinyusb brings in (run `pio run` or `idf.py build` once so it is downloaded)
# EPDJ_USB_DESC_SRC=<file> checks another copy of usb_desc.cpp instead of the real one (used to prove the checks
# fail on a mutated builder).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
esp_tusb=$root/firmware/esp32s3/managed_components/espressif__esp_tinyusb
tusb=${1:-$esp_tusb/tinyusb/src}
desc_src=${EPDJ_USB_DESC_SRC:-$root/firmware/core/src/usb_desc.cpp}
comp=$root/firmware/esp32s3/components
[ -f "$tusb/tusb.h" ] || { echo "TinyUSB sources not found at $tusb" >&2; exit 2; }
out=$(mktemp -d "${TMPDIR:-/tmp}/epdj_crosscheck.XXXXXX")
trap 'rm -rf "$out"' EXIT
rc=0

echo "== descriptor bytes against TinyUSB's TUD_* macros"
g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
  -DCFG_TUSB_CONFIG_FILE='"host_tusb_config.h"' -I"$here" -I"$tusb" -I"$root/firmware/core/include" \
  "$here/crosscheck.cpp" "$desc_src" -o "$out/crosscheck"
"$out/crosscheck" || rc=1

echo "== TinyUSB's audio class driver on those descriptors and this firmware's callbacks"
inc="-I$here/host -I$here -I$tusb -I$esp_tusb/include -I$comp/epdj_usb/tusb_override -I$root/firmware/core/include \
 -I$comp/epdj_usb/include -I$comp/epdj_usb/src -I$comp/epdj_audio/include"
defs='-DCFG_TUSB_CONFIG_FILE="host_driver_config.h"'
# shellcheck disable=SC2086
gcc -std=gnu11 -Wall -Wno-unused-parameter $defs $inc -c "$tusb/class/audio/audio_device.c" -o "$out/audio_device.o"
# shellcheck disable=SC2086
gcc -std=gnu11 -Wall -Wno-unused-parameter $defs $inc -c "$tusb/common/tusb_fifo.c" -o "$out/tusb_fifo.o"
# shellcheck disable=SC2086
gcc -std=gnu11 -Wall -Wno-unused-parameter $defs $inc -c "$tusb/class/midi/midi_device.c" -o "$out/midi_device.o"
# shellcheck disable=SC2086
gcc -std=gnu11 -Wall -Wno-unused-parameter $defs $inc -c "$tusb/tusb.c" -o "$out/tusb.o"
# shellcheck disable=SC2086
g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers $defs $inc \
  "$here/driver_check.cpp" "$comp/epdj_usb/src/usb_audio.cpp" "$desc_src" "$root/firmware/core/src/audio_rate.cpp" \
  "$root/firmware/core/src/config_server.cpp" "$root/firmware/core/src/config.cpp" "$root/firmware/core/src/midi.cpp" \
  "$root/firmware/core/src/sysex.cpp" \
  "$out/audio_device.o" "$out/tusb_fifo.o" "$out/midi_device.o" "$out/tusb.o" -o "$out/driver_check"
"$out/driver_check" || rc=1
exit $rc
