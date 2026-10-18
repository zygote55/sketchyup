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
# D-021: Qt marks a window unexposed when its frame callback is 100 ms late, a heuristic for a
# compositor that stopped drawing a hidden window. Here Weston draws nothing, and so sends no
# frame callback, until its desktop-shell helper commits the background; on a loaded host that
# took 8-20 s, so the test window turned unexposed 100 ms after its first frame and stayed so
# past the tests' 5 s qWaitForWindowExposed(). Nothing hides a window in this session, so let
# Qt wait for the callback within the deadline instead. (Waiting for the background before
# starting the test also works but spends that time inside the deadline.)
export QT_WAYLAND_FRAME_CALLBACK_TIMEOUT=${QT_WAYLAND_FRAME_CALLBACK_TIMEOUT:-60000}
export SKETCHYUP_TEST_EXIT_FILE="$test_runtime/test-status"
# Weston exits with zero even when the launched program fails. Preserve the
# program's actual status independently, including missing/aborted executions.
# On the deadline, test-deadline.sh prints every client thread's stack before stopping Weston.
weston_status=0
# D-040: Weston 15's show_input_panel_surface() destroys the input panel's slide animation
# without cancelling its queued idle destroy, the only double destroy found for the CI crash in
# weston_view_animation_destroy. An empty input-method path starts no weston-keyboard, so no
# panel and no slide; no test uses the input method.
printf '[input-method]\npath=\n' > "$test_runtime/weston.ini"
# Weston 15 multiplies --width/--height by --scale, so 2x is 3200x2000 logical (6400x4000
# device). The intended 3200x2000 device size cost +18 % wall time and +36 % CPU on 4 CPUs,
# because a smaller output throttles frame callbacks less; see D-021 in R089a-defect-triage.md.
"$(dirname "${BASH_SOURCE[0]}")/test-deadline.sh" 60 weston --backend=headless --renderer=gl --fake-seat \
  --width="$((1600 * test_scale))" --height="$((1000 * test_scale))" \
  --scale="$test_scale" --idle-time=0 --socket="$WAYLAND_DISPLAY" \
  --config="$test_runtime/weston.ini" --log="$test_runtime/weston.log" -- bash -c '
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
if (( test_status > 128 )); then
  echo "Test killed by signal $(kill -l "$test_status" 2> /dev/null || echo $((test_status - 128))): $*" >&2
elif (( test_status != 0 )); then
  echo "Test exited with status $test_status: $*" >&2
fi
if (( test_status != 0 )); then
  tail -40 "$test_runtime/weston.log" >&2
fi
exit "$test_status"
