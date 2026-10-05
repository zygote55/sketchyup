#!/usr/bin/env bash
# Record only an owned, isolated X server. Never captures the user's desktop.
set -euo pipefail
if (( $# < 2 )); then
  echo 'Usage: record-native-x11.sh NEW_VIDEO.mp4 executable [arguments...]' >&2
  exit 2
fi
record_output=$(realpath -m -- "$1")
shift
[[ ! -e "$record_output" && -d "$(dirname -- "$record_output")" ]]
command -v ffmpeg >/dev/null
xvfb-run -a -s '-screen 0 1600x1000x24' bash -c '
  set -euo pipefail
  record_output=$1
  shift
  ffmpeg -nostdin -hide_banner -loglevel warning -n -f x11grab -framerate 15 \
    -video_size 1600x1000 -i "$DISPLAY" -c:v libx264 -preset ultrafast -crf 25 \
    -pix_fmt yuv420p "$record_output" > "$record_output.log" 2>&1 &
  recorder_pid=$!
  trap '\''kill -INT "$recorder_pid" 2>/dev/null || true'\'' EXIT
  sleep 0.4
  kill -0 "$recorder_pid"
  test_status=0
  unset QT_SCREEN_SCALE_FACTORS
  env QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR=1 LIBGL_ALWAYS_SOFTWARE=1 timeout 120s "$@" || test_status=$?
  kill -INT "$recorder_pid" 2>/dev/null || true
  recorder_status=0
  wait "$recorder_pid" || recorder_status=$?
  trap - EXIT
  # FFmpeg returns 255 when its normal termination was requested by a signal.
  (( recorder_status == 0 || recorder_status == 255 ))
  [[ -s "$record_output" ]]
  exit "$test_status"
' bash "$record_output" "$@"
