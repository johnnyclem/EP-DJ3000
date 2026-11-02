#!/bin/sh
# End-to-end conformance: builds the engine simulator and runs the Swift and Node suites against it (docs/E2E.md).
#
#   tools/e2e.sh                run both suites
#   tools/e2e.sh node|swift     run one
#   tools/e2e.sh build          only build the simulator
#
# Environment
#   EPDJ_E2E_BUILD       build directory for the simulator    (default firmware/core/build-e2e, git-ignored)
#   EPDJ_E2E_SANITIZE    ON|OFF AddressSanitizer + UBSan       (default ON: a sanitizer report fails the test that hit it)
#   EPDJ_SWIFT_SCRATCH   --scratch-path for swift build/test   (default macos/.build)
# Needs cmake and a C++17 compiler; node 18+ for the Node suite; a Swift 5.9+ toolchain on PATH for the Swift suite.
# A missing node or swift is reported and skipped, never counted as a pass.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${EPDJ_E2E_BUILD:-$ROOT/firmware/core/build-e2e}
SAN=${EPDJ_E2E_SANITIZE:-ON}
WHAT=${1:-all}

case "$WHAT" in all | node | swift | build) ;; *)
  echo "usage: tools/e2e.sh [all|node|swift|build]" >&2
  exit 2
  ;;
esac

jobs() { nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2; }

echo "== building epdj_engine_sim into $BUILD (sanitizers $SAN)"
cmake -S "$ROOT/firmware/core" -B "$BUILD" -DEPDJ_BUILD_TOOLS=ON -DEPDJ_SANITIZE="$SAN" >/dev/null || exit 1
cmake --build "$BUILD" --target epdj_engine_sim -j"$(jobs)" || exit 1
EPDJ_ENGINE_SIM="$BUILD/epdj_engine_sim"
export EPDJ_ENGINE_SIM
[ "$WHAT" = build ] && exit 0

failed=0
skipped=0

if [ "$WHAT" = all ] || [ "$WHAT" = node ]; then
  echo
  echo "== Node: real firmware -> real Mixxx mapping (drivers/test/e2e)"
  if command -v node >/dev/null 2>&1; then
    node "$ROOT/drivers/test/e2e/run.mjs" || failed=1
  else
    echo "SKIP  node is not installed"
    skipped=1
  fi
fi

if [ "$WHAT" = all ] || [ "$WHAT" = swift ]; then
  echo
  echo "== Swift: real firmware -> real DeviceClient / ControllerState / DeckModel (macos/Tests/EPDJ3000E2ETests)"
  if command -v swift >/dev/null 2>&1; then
    scratch=""
    [ -n "${EPDJ_SWIFT_SCRATCH:-}" ] && scratch="--scratch-path $EPDJ_SWIFT_SCRATCH"
    # shellcheck disable=SC2086
    (cd "$ROOT/macos" && swift test $scratch --filter EPDJ3000E2ETests) || failed=1
  else
    echo "SKIP  swift is not installed"
    skipped=1
  fi
fi

echo
if [ "$failed" -ne 0 ]; then
  echo "E2E: FAILED"
  exit 1
fi
[ "$skipped" -ne 0 ] && echo "E2E: passed, but a suite was skipped (see above)" || echo "E2E: all suites passed"
exit 0
