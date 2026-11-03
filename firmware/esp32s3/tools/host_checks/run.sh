#!/bin/sh
# Host tests of the ESP32-S3 layer's pure logic: the engine task's output policy (main/output_pump.h), its
# catch-up rule (main/tick_pacing.h), the touch measurement-length fit (epdj_hal/touch_range.h) and the AS5600 magnet
# decode and sensor time budget (epdj_hal/sensor_budget.h), the AS5600 CONF word and register choice
# (epdj_hal/as5600_regs.h) and the encoder re-probe timing (epdj_hal/encoder_retry.h). They are the
# headers the firmware compiles, run against fakes; nothing here needs ESP-IDF or a chip. Uses firmware/core's test
# harness and compiles firmware/core/src with the same flags as the core's own build.
#
# usage: run.sh [work dir]      (default: a temporary directory, removed afterwards)
# EPDJ_PUMP_HEADER=<file> runs the tests against another copy of output_pump.h (to prove they fail on a mutant).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
core=$root/firmware/core
comp=$root/firmware/esp32s3
if [ -n "$1" ]; then
  out=$1
  mkdir -p "$out"
else
  out=$(mktemp -d "${TMPDIR:-/tmp}/epdj_host_checks.XXXXXX")
  trap 'rm -rf "$out"' EXIT
fi
pump_dir=$comp/main
if [ -n "$EPDJ_PUMP_HEADER" ]; then
  pump_dir=$out/pump_override
  mkdir -p "$pump_dir"
  cp "$EPDJ_PUMP_HEADER" "$pump_dir/output_pump.h"
  cp "$comp/main/tick_pacing.h" "$pump_dir/"
fi
flags="-std=c++17 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-sanitize-recover=undefined -g -O1"
warn="-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion"
inc="-I$core/include -I$core/test -I$pump_dir -I$comp/components/epdj_hal/include"

# The core library, once per run (the objects are reusable in a given work dir).
for f in "$core"/src/*.cpp; do
  o=$out/core_$(basename "$f" .cpp).o
  [ -f "$o" ] && [ "$o" -nt "$f" ] && continue
  echo "g++ $f" >&2
  g++ $flags $warn -Werror -I"$core/include" -c "$f" -o "$o" &
  while [ "$(jobs -p | wc -l)" -ge 4 ]; do wait -n 2>/dev/null || wait; done
done
wait
[ -f "$out/harness.o" ] || g++ $flags -I"$core/test" -c "$core/test/harness_main.cpp" -o "$out/harness.o"

rc=0
for t in test_output_pump test_pacing_and_range test_sensor_budget test_as5600_conf test_encoder_retry; do
  echo "== $t"
  # shellcheck disable=SC2086
  g++ $flags $warn $inc "$here/$t.cpp" "$out"/core_*.o "$out/harness.o" -o "$out/$t"
  "$out/$t" || rc=1
done
exit $rc
