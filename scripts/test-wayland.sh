#!/usr/bin/env bash
# Run one native test in an isolated, software-rendered Wayland session.
set -euo pipefail
if (( $# == 0 )); then
  echo "Usage: $0 executable [arguments...]" >&2
  exit 2
fi
test_scale=${SKETCHYUP_TEST_SCALE:-1}
case "$test_scale" in
  1|2) ;;
  *) echo 'SKETCHYUP_TEST_SCALE must be 1 or 2' >&2; exit 2 ;;
esac
if [[ -n ${SKETCHYUP_WAYLAND_CLIENT_LIBRARY:-} &&
      ! -f "$SKETCHYUP_WAYLAND_CLIENT_LIBRARY/libwayland-client.so.0" ]]; then
  echo 'Private Wayland client library is missing.' >&2
  exit 2
fi
test_runtime=$(mktemp -d)
trap 'rm -rf "$test_runtime"' EXIT
export XDG_RUNTIME_DIR="$test_runtime"
export WAYLAND_DISPLAY=wayland-test
export QT_QPA_PLATFORM=wayland
export QT_SCALE_FACTOR=1
unset QT_SCREEN_SCALE_FACTORS
export LIBGL_ALWAYS_SOFTWARE=1
export SKETCHYUP_TEST_EXIT_FILE="$test_runtime/test-status"
# Weston exits with zero even when the launched program fails. Preserve the
# program's actual status independently, including missing/aborted executions.
weston_status=0
timeout 60s weston --backend=headless --renderer=gl --fake-seat \
  --width="$((1600 * test_scale))" --height="$((1000 * test_scale))" \
  --scale="$test_scale" --idle-time=0 --socket="$WAYLAND_DISPLAY" \
  --no-config --log="$test_runtime/weston.log" -- bash -c '
    if [[ -n ${SKETCHYUP_WAYLAND_CLIENT_LIBRARY:-} ]]; then
      export LD_LIBRARY_PATH="$SKETCHYUP_WAYLAND_CLIENT_LIBRARY${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    fi
    "$@"
    test_status=$?
    printf "%s\n" "$test_status" > "$SKETCHYUP_TEST_EXIT_FILE"
    exit "$test_status"
  ' bash "$@" || weston_status=$?
if (( weston_status == 124 )); then
  echo "Timed out after 60 s: $*" >&2
elif [[ ! -f "$SKETCHYUP_TEST_EXIT_FILE" ]]; then
  echo "Weston exited ($weston_status) without recording a test status: $*" >&2
fi
if (( weston_status != 0 )) || [[ ! -f "$SKETCHYUP_TEST_EXIT_FILE" ]]; then
  cat "$test_runtime/weston.log" >&2
  exit 1
fi
test_status=$(cat "$SKETCHYUP_TEST_EXIT_FILE")
if (( test_status != 0 )); then
  tail -40 "$test_runtime/weston.log" >&2
fi
exit "$test_status"
