#!/usr/bin/env bash
# R085 hotplug check: the owner physically unplugs and replugs an external
# output while SketchyUp runs with a model open and an unsaved edit. This
# script never changes monitor configuration; it starts an owned app, records
# `hyprctl monitors`, the app window, log and window screenshots, and waits for
# the owner. See docs/M9_HARDWARE_SESSION.md, section 3.
set -euo pipefail
if (( $# < 1 || $# > 3 )) || [[ $1 != wayland && $1 != xcb ]]; then
  echo 'Usage: session-hotplug-check.sh wayland|xcb [MODEL.sketchyup] [OUTPUT]' >&2
  echo '  MODEL defaults to the built-in demo; OUTPUT defaults to DP-7.' >&2
  exit 2
fi
platform=$1
model=${2:-}
output=${3:-DP-7}
source "$(dirname -- "${BASH_SOURCE[0]}")/session-lib.sh"
command -v hyprctl >/dev/null && command -v jq >/dev/null && command -v grim >/dev/null ||
  session_die 'hyprctl, jq and grim are required.'
hyprctl -j monitors | jq -e --arg o "$output" 'map(select(.name == $o and .dpmsStatus)) | length == 1' >/dev/null ||
  session_die "$output must be connected and awake before starting."
(( $(hyprctl -j monitors | jq length) >= 2 )) || session_die 'Two outputs are required.'

cat <<EOF
R085 hotplug ($platform, output $output). Steps:
  1. The script starts SketchyUp (private settings) ${model:+with a copy of $model}${model:-with the demo model}
     and moves only its window to $output.
  2. You make one edit (for example push/pull the slab top by 500 mm) and do not save.
  3. You unplug $output. The script records monitors, window, log and a screenshot.
  4. You check the app on the remaining output, then replug $output; the script records again.
  5. You save a copy with File > Save As into the evidence directory to prove the edit survived.
Nothing here changes monitor settings; unplugging is the only output change.
EOF
session_confirm 'Start the hotplug check?' || exit 1
session_init "hotplug-$platform"
start=$(date '+%Y-%m-%d %H:%M:%S')
session_monitors before
args=(--demo)
if [[ -n $model ]]; then
  cp -- "$model" "$session_evidence/model.sketchyup"
  args=("$session_evidence/model.sketchyup")
fi
session_app_start "$platform" "${args[@]}"
trap session_app_stop EXIT
session_wait_window 30
target=$(hyprctl -j monitors | jq -r --arg o "$output" '.[] | select(.name == $o) | .activeWorkspace.id')
if [[ $(session_app_window | jq -r .monitor) != $(hyprctl -j monitors | jq -r --arg o "$output" '.[] | select(.name == $o) | .id') ]]; then
  session_confirm "Move only the SketchyUp window to workspace $target on $output?" && session_move_window "$target"
fi
sleep 1
session_pause "Make one edit in SketchyUp and leave it unsaved."
read -r -p 'Describe the edit in a few words: ' edit
session_note "edit before unplug: $edit"
session_window_record before
session_screenshot before

wait_output() { # wait_output present|absent SECONDS
  local deadline=$((SECONDS + $2)) count
  while :; do
    count=$(hyprctl -j monitors | jq --arg o "$output" 'map(select(.name == $o)) | length')
    [[ $1 == present && $count == 1 || $1 == absent && $count == 0 ]] && return 0
    (( SECONDS < deadline )) || return 1
    sleep 0.25
  done
}

echo ">>> Unplug $output now. Waiting up to 120 s..."
wait_output absent 120 || session_die "$output was not removed within 120 s; evidence kept."
session_note "$output removed"
sleep 5
session_monitors unplugged
session_app_alive && session_note 'app alive after unplug' || session_note 'APP NOT RUNNING after unplug'
session_window_record unplugged
session_screenshot unplugged
cat <<EOF
Check now, on the remaining output:
  - SketchyUp is still running and its window is visible on the remaining output.
  - The viewport is drawn at the right scale (sharp, not tiny or blurred; text crisp).
  - Orbit and select still work; the unsaved edit is still there (Edit menu shows Undo).
EOF
session_pause 'Record what you saw in the result at the end.'

echo ">>> Replug $output now. Waiting up to 120 s..."
wait_output present 120 || session_die "$output did not return within 120 s; evidence kept."
session_note "$output returned"
sleep 5
session_monitors replugged
session_app_alive && session_note 'app alive after replug' || session_note 'APP NOT RUNNING after replug'
session_window_record replugged
session_screenshot replugged
session_pause "Check the window and viewport again, then use File > Save As to save $session_evidence/after-hotplug.sketchyup."
if [[ -f $session_evidence/after-hotplug.sketchyup ]]; then
  sha256sum "$session_evidence/after-hotplug.sketchyup" >> "$session_evidence/notes.txt"
else
  session_note 'after-hotplug.sketchyup was not saved'
fi
coredumpctl list --since "$start" --no-pager > "$session_evidence/coredumps.txt" 2>&1 || true
session_note "core dumps since start: $(grep -c -E 'sketchyup|Hyprland|Xwayland' "$session_evidence/coredumps.txt" || true)"
session_result
