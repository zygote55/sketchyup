#!/usr/bin/env bash
# Run one native test with a real XKB keyboard layout. Xvfb applies the layout to
# keys injected through XTest. The wayland platform nests Weston's x11 backend in
# that server, so the compositor takes the same keymap and forwards the keys.
# -noreset matters: an X server without clients resets to its default (US) keymap
# as soon as setxkbmap disconnects.
set -euo pipefail
if (( $# < 2 )); then
  echo "Usage: SKETCHYUP_TEST_KEYBOARD_LAYOUT=us|de|fr $0 xcb|wayland executable [arguments...]" >&2
  exit 2
fi
case ${SKETCHYUP_TEST_KEYBOARD_LAYOUT:-} in
  us|de|fr) ;;
  *) echo 'SKETCHYUP_TEST_KEYBOARD_LAYOUT must be us, de or fr' >&2; exit 2 ;;
esac
case $1 in
  xcb|wayland) ;;
  *) echo 'Platform must be xcb or wayland' >&2; exit 2 ;;
esac
export LIBGL_ALWAYS_SOFTWARE=1 QT_SCALE_FACTOR=1
unset QT_SCREEN_SCALE_FACTORS WAYLAND_DISPLAY
exec xvfb-run -a -s '-screen 0 1600x1000x24 -noreset' bash -c '
  set -uo pipefail
  platform=$1
  shift
  setxkbmap -model pc105 -layout "$SKETCHYUP_TEST_KEYBOARD_LAYOUT" || exit 3
  setxkbmap -query
  export SKETCHYUP_TEST_XTEST_DISPLAY=$DISPLAY
  if [[ $platform == xcb ]]; then
    QT_QPA_PLATFORM=xcb exec timeout 90s "$@"
  fi
  runtime=$(mktemp -d)
  trap "rm -rf \"$runtime\"" EXIT
  export XDG_RUNTIME_DIR=$runtime WAYLAND_DISPLAY=wayland-layout QT_QPA_PLATFORM=wayland
  export SKETCHYUP_TEST_EXIT_FILE=$runtime/test-status
  # Weston exits with zero even when its client fails; keep the client status.
  timeout 90s weston --backend=x11 --renderer=gl --width=1600 --height=1000 \
    --idle-time=0 --socket="$WAYLAND_DISPLAY" --no-config --log="$runtime/weston.log" -- \
    bash -c "\"\$@\"; status=\$?; echo \$status > \"\$SKETCHYUP_TEST_EXIT_FILE\"; exit \$status" \
    bash "$@"
  weston_status=$?
  if (( weston_status != 0 )) || [[ ! -f $SKETCHYUP_TEST_EXIT_FILE ]]; then
    echo "Weston exited ($weston_status) without a passing test status: $*" >&2
    cat "$runtime/weston.log" >&2
    exit 1
  fi
  test_status=$(cat "$SKETCHYUP_TEST_EXIT_FILE")
  (( test_status == 0 )) || tail -40 "$runtime/weston.log" >&2
  exit "$test_status"
' bash "$@"
