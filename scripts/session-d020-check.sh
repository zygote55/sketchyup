#!/usr/bin/env bash
# D-020 check on Hyprland: hide SketchyUp's window (move it to a special
# workspace, Hyprland's equivalent of minimizing) after a long open starts but
# before the 150 ms progress popup appears, and confirm the compositor survives.
# Weston 15.0.1 crashed in this situation (docs/verification/R083f-file-worker-hang.md).
# Only the owned SketchyUp window is moved. See docs/M9_HARDWARE_SESSION.md, section 8.
set -euo pipefail
if (( $# != 1 )); then
  echo 'Usage: session-d020-check.sh FIXTURE.sketchyup   (the 100 MB fixture)' >&2
  exit 2
fi
fixture=$(realpath -e -- "$1")
source "$(dirname -- "${BASH_SOURCE[0]}")/session-lib.sh"
command -v hyprctl >/dev/null && command -v jq >/dev/null || session_die 'hyprctl and jq are required.'
hidden=special:sketchyup-d020

cat <<EOF
D-020 hidden-window progress check. Steps:
  1. SketchyUp starts with the demo model (private settings).
  2. You press Ctrl+O and choose the copied 100 MB fixture. When the "Open model"
     dialog closes, the script moves only the SketchyUp window to $hidden
     (hidden, not followed) within a few tens of milliseconds, before the popup's
     150 ms delay ends.
  3. After 10 s the script checks that Hyprland and SketchyUp are still running,
     brings the window back and records a screenshot.
  4. Optionally repeat with a save (step printed at the end).
If Hyprland crashes, the session ends: log in again and rerun this script to collect the
core dump listing, or run: coredumpctl list Hyprland
EOF
session_confirm 'Start the D-020 check? Save other work first.' || exit 1
session_init d020-hyprland
start=$(date '+%Y-%m-%d %H:%M:%S')
mkdir "$session_evidence/model"
cp -- "$fixture" "$session_evidence/model/fixture.sketchyup"
echo "Open this file: $session_evidence/model/fixture.sketchyup"
session_monitors before
session_app_start wayland --demo
trap session_app_stop EXIT
session_wait_window 30
home=$(session_app_window | jq -r '.workspace.id')
hyprland_pid=$(pgrep -x Hyprland | head -1)
session_note "Hyprland pid $hyprland_pid, app workspace $home"

dialog_open() {
  hyprctl -j clients | jq -e 'any(.[]; .mapped and (.title | test("^Open model")))' >/dev/null
}
echo '>>> Press Ctrl+O in SketchyUp now (waiting up to 60 s for the dialog)...'
deadline=$((SECONDS + 60))
until dialog_open; do
  (( SECONDS < deadline )) || session_die 'The Open model dialog did not appear.'
  sleep 0.1
done
session_note 'open dialog shown'
echo '>>> Choose the fixture and press Open. The window will disappear; that is expected.'
until ! dialog_open; do sleep 0.02; done
session_move_window "$hidden"
session_note 'open dialog closed; window hidden'
sleep 10
if kill -0 "$hyprland_pid" 2>/dev/null && hyprctl version >/dev/null 2>&1; then
  session_note 'Hyprland alive after hidden open'
else
  session_note 'HYPRLAND NOT RESPONDING after hidden open'
fi
session_app_alive && session_note 'app alive after hidden open' || session_note 'APP NOT RUNNING after hidden open'
session_window_record hidden
session_move_window "$home"
sleep 1
session_window_record restored
session_screenshot after-open
cat <<EOF
Check: the model opened (title shows fixture.sketchyup), the progress window is gone,
and the viewport draws. Optional save variant: make an edit, press Ctrl+S, and at once
move SketchyUp to another workspace with your usual keybinding; note whether the
progress window had already appeared.
EOF
session_pause 'Do the checks (and the optional save variant).'
coredumpctl list --since "$start" --no-pager > "$session_evidence/coredumps.txt" 2>&1 || true
grep -n -i -E 'protocol error|wl_|xdg_|popup' "$session_evidence/app.log" > "$session_evidence/app-wayland-lines.txt" || true
session_result
