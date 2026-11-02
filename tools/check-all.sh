#!/bin/sh
# Every portable check in this repo, in order, then one PASS / FAIL / SKIP table.
#
#   tools/check-all.sh                      run everything
#   tools/check-all.sh drivers swift        run only these steps (names below, in the order given)
#   tools/check-all.sh --list               print the step names
#
# Steps, in the default order:
#   contract    python3 tools/gen_protocol.py --check, tools/gen_vectors.py --check
#   core        firmware/core: cmake build with ASan + UBSan, then ctest
#   drivers     node drivers/test/run-all.mjs
#   swift       macos/: swift build, then swift test without the end-to-end suite (that is the next two rows)
#   e2e-node    tools/e2e.sh node   (real firmware Engine -> real Mixxx script)
#   e2e-swift   tools/e2e.sh swift  (real firmware Engine -> real Swift libraries)
#
# Options
#   -b, --build-dir DIR   where cmake builds and the per-step logs go
#                         (default firmware/core/build-check-all, git-ignored). Unless --swift-scratch is given,
#                         SwiftPM then builds in DIR/swift too.
#       --swift-scratch DIR   SwiftPM --scratch-path (default macos/.build, git-ignored)
#       --no-sanitize     build the firmware core and the simulator without ASan/UBSan
#   -v, --verbose         print each step's whole log after it finishes (a failing step always prints its tail)
#       --strict          exit 1 when any step was skipped, too (default: only a FAIL does)
#   -l, --list, -h, --help
#
# A step whose tool is missing (python3, node, cmake and a C++ compiler, swift) is SKIPPED with the reason. A skip is
# never a pass: it has its own column in the table, and nothing is claimed about what it would have found. Also
# reported: a passing step that still skipped something inside (an xmllint-less drivers run, a skipped XCTest).
# Exit status: 1 if any step FAILED (or, with --strict, was skipped), 2 on a usage error, else 0.
# Nothing here touches the network or writes outside DIR, macos/.build and the scratch paths you pass.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ALL_STEPS="contract core drivers swift e2e-node e2e-swift"

BUILD=$ROOT/firmware/core/build-check-all
SWIFT_SCRATCH=
SAN=ON
VERBOSE=0
STRICT=0
WANT=

usage() { sed -n '2,/^set -u/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; }
die() { printf 'check-all.sh: %s\n' "$*" >&2; exit 2; }
absolute() { case $1 in /*) printf '%s\n' "$1" ;; *) printf '%s/%s\n' "$PWD" "$1" ;; esac; }

while [ $# -gt 0 ]; do
  case $1 in
    -h | --help) usage; exit 0 ;;
    -l | --list) printf '%s\n' $ALL_STEPS; exit 0 ;;
    -v | --verbose) VERBOSE=1 ;;
    --strict) STRICT=1 ;;
    --no-sanitize) SAN=OFF ;;
    -b | --build-dir) [ $# -ge 2 ] || die "$1 needs a directory"; BUILD=$(absolute "$2"); shift ;;
    --swift-scratch) [ $# -ge 2 ] || die "$1 needs a directory"; SWIFT_SCRATCH=$(absolute "$2"); shift ;;
    -*) die "unknown option $1 (try --help)" ;;
    *)
      case " $ALL_STEPS " in *" $1 "*) WANT="$WANT $1" ;; *) die "unknown step '$1' (steps: $ALL_STEPS)" ;; esac
      ;;
  esac
  shift
done
[ -n "$WANT" ] || WANT=$ALL_STEPS
if [ -z "$SWIFT_SCRATCH" ]; then
  case $BUILD in "$ROOT/firmware/core/build-check-all") SWIFT_SCRATCH=$ROOT/macos/.build ;; *) SWIFT_SCRATCH=$BUILD/swift ;; esac
fi

LOGS=$BUILD/logs
mkdir -p "$LOGS" || die "cannot create $LOGS"
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

REASON=
need() {
  for tool in "$@"; do
    command -v "$tool" >/dev/null 2>&1 || { REASON="$tool is not installed"; return 1; }
  done
}
need_cxx() {
  for cxx in "${CXX:-}" c++ g++ clang++; do
    [ -n "$cxx" ] && command -v "$cxx" >/dev/null 2>&1 && return 0
  done
  REASON="no C++ compiler (c++, g++, clang++) found"
  return 1
}

# Each step returns 0 = pass, 1 = fail, 2 = skipped (REASON says why). Output goes to $LOG.
step_contract() {
  need python3 || return 2
  (cd "$ROOT" && python3 tools/gen_protocol.py --check && python3 tools/gen_vectors.py --check) >>"$LOG" 2>&1
}
step_core() {
  need cmake || return 2
  need_cxx || return 2
  (
    cmake -S "$ROOT/firmware/core" -B "$BUILD/core" -DEPDJ_SANITIZE="$SAN" &&
      cmake --build "$BUILD/core" -j"$JOBS" &&
      ctest --test-dir "$BUILD/core" --output-on-failure
  ) >>"$LOG" 2>&1
}
step_drivers() {
  need node python3 || return 2
  (cd "$ROOT" && node drivers/test/run-all.mjs) >>"$LOG" 2>&1
}
step_swift() {
  need swift || return 2
  (
    cd "$ROOT/macos" || exit 1
    swift build --scratch-path "$SWIFT_SCRATCH"
    b=$?
    echo "check-all: swift build exit $b"
    swift test --scratch-path "$SWIFT_SCRATCH" --skip EPDJ3000E2ETests
    t=$?
    echo "check-all: swift test exit $t"
    [ "$b" -eq 0 ] && [ "$t" -eq 0 ]
  ) >>"$LOG" 2>&1
}
step_e2e_node() {
  need cmake node || return 2
  need_cxx || return 2
  EPDJ_E2E_BUILD=$BUILD/e2e EPDJ_E2E_SANITIZE=$SAN sh "$ROOT/tools/e2e.sh" node >>"$LOG" 2>&1
}
step_e2e_swift() {
  need cmake swift || return 2
  need_cxx || return 2
  EPDJ_E2E_BUILD=$BUILD/e2e EPDJ_E2E_SANITIZE=$SAN EPDJ_SWIFT_SCRATCH=$SWIFT_SCRATCH sh "$ROOT/tools/e2e.sh" swift >>"$LOG" 2>&1
}

# One line saying what the step's log shows, so the table is more than "PASS".
detail() {
  detail_raw "$@" | tr -s '[:space:]' ' ' | sed 's/^ //; s/ $//'
}
detail_raw() {
  case $1 in
    contract) grep -E 'up to date' "$2" | paste -sd ';' - | sed 's/;/; /g' ;;
    core) grep -E 'tests passed|tests failed' "$2" | tail -n 1 ;;
    drivers) grep -E '^[0-9]+ passed, [0-9]+ failed' "$2" | tail -n 1 ;;
    swift) { grep -E '^check-all: swift' "$2"; grep -E 'Executed [0-9]+ tests' "$2" | tail -n 1; } | paste -sd ' ' - | sed 's/check-all: //g' ;;
    e2e-node) grep -E '^[0-9]+ passed, [0-9]+ failed|^E2E:' "$2" | tail -n 2 | paste -sd ' ' - ;;
    e2e-swift) grep -E 'Executed [0-9]+ tests|^E2E:' "$2" | tail -n 2 | paste -sd ' ' - ;;
  esac
}

# A passing step can still have skipped parts (drivers without xmllint, a skipped XCTest, a suite that found no
# simulator); say so instead of letting PASS imply they ran.
skipped_inside() {
  grep -Eq "^SKIP |^ +SKIP |' skipped|[1-9][0-9]* skipped|[1-9][0-9]* tests? skipped" "$1" 2>/dev/null &&
    printf ' [parts skipped inside: see log]'
  return 0
}

RESULTS=
npass=0 nfail=0 nskip=0
total=$(printf '%s\n' $WANT | wc -l | tr -d ' ')
i=0
for name in $WANT; do
  i=$((i + 1))
  LOG=$LOGS/$name.log
  : >"$LOG"
  printf '[%s/%s] %s ... ' "$i" "$total" "$name"
  REASON=
  t0=$(date +%s)
  "step_$(printf '%s' "$name" | tr '-' '_')"
  rc=$?
  secs=$(($(date +%s) - t0))
  case $rc in
    0) status=PASS; npass=$((npass + 1)); info="$(detail "$name" "$LOG")$(skipped_inside "$LOG")" ;;
    2) status=SKIP; nskip=$((nskip + 1)); info="$REASON" ;;
    *) status=FAIL; nfail=$((nfail + 1)); info="$(detail "$name" "$LOG")" ;;
  esac
  printf '%s (%ss)\n' "$status" "$secs"
  if [ "$status" = FAIL ]; then
    echo "---- last 40 lines of $LOG"
    tail -n 40 "$LOG"
    echo "----"
  elif [ "$VERBOSE" = 1 ] && [ "$status" = PASS ]; then
    cat "$LOG"
  fi
  RESULTS="$RESULTS$(printf '%-10s %-5s %5ss  %s' "$name" "$status" "$secs" "$info")
"
done

echo
printf '%-10s %-5s %6s  %s\n' STEP RESULT TIME DETAIL
printf '%s' "$RESULTS"
echo
printf 'PASS %s   FAIL %s   SKIP %s    (logs: %s)\n' "$npass" "$nfail" "$nskip" "$LOGS"
[ "$nskip" -gt 0 ] && echo "A skipped step did not run, so nothing is claimed about it; it is not a pass."
[ "$nfail" -gt 0 ] && exit 1
[ "$STRICT" = 1 ] && [ "$nskip" -gt 0 ] && exit 1
exit 0
