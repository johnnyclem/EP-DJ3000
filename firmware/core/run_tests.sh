#!/bin/sh
# Configure, build and run all host tests. Usage: ./run_tests.sh [--sanitize]
set -e
cd "$(dirname "$0")"
SAN=OFF; DIR=build
[ "$1" = "--sanitize" ] && { SAN=ON; DIR=build-san; }
cmake -S . -B "$DIR" -DEPDJ_SANITIZE=$SAN >/dev/null
cmake --build "$DIR" -j"$(nproc)"
ctest --test-dir "$DIR" --output-on-failure
