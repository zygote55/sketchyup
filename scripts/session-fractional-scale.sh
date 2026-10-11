#!/usr/bin/env bash
# N04/R085 physical fractional-scale check: temporarily set one output to
# 150% and check SketchyUp's wide and 640 px layouts. The script records every
# output first, writes a restore script before changing anything, restores on
# exit (including Ctrl+C) and verifies the restored state. It never edits the
# Hyprland configuration files. See docs/M9_HARDWARE_SESSION.md, section 5.
set -euo pipefail
if (( $# > 2 )); then
  echo 'Usage: session-fractional-scale.sh [OUTPUT] [SCALE]   (defaults: DP-7 1.5)' >&2
  exit 2
fi
output=${1:-DP-7}
scale=${2:-1.5}
[[ $scale =~ ^[0-9]+(\.[0-9]+)?$ ]] || { echo 'Invalid scale' >&2; exit 2; }
source "$(dirname -- "${BASH_SOURCE[0]}")/session-lib.sh"
command -v hyprctl >/dev/null && command -v jq >/dev/null || session_die 'hyprctl and jq are required.'
hyprctl -j monitors | jq -e --arg o "$output" 'map(select(.name == $o and .dpmsStatus)) | length == 1' >/dev/null ||
  session_die "$output must be connected and awake."

# One hl.monitor() call per enabled output, reproducing the current state.
monitor_lines() { # monitor_lines JSON_FILE
  jq -r '.[] | select(.disabled | not) |
    "hl.monitor({ output = \"\(.name)\", mode = \"\(.width)x\(.height)@\((.refreshRate * 1000 | round) / 1000)\", position = \"\(.x)x\(.y)\", scale = \(.scale), transform = \(.transform) })"' "$1"
}
same_state() { # same_state A.json B.json
  jq -n -e --slurpfile a "$1" --slurpfile b "$2" '
    def key: map(select(.disabled | not) | {name, width, height, x, y, scale, transform, r: (.refreshRate | round)}) | sort_by(.name);
    ($a[0] | key) == ($b[0] | key)' >/dev/null
}

cat <<EOF
N04 fractional scale: $output temporarily at scale $scale. Steps:
  1. Record all outputs and write restore-monitors.sh into the evidence directory.
  2. Apply scale $scale to $output (and move an output that sits directly to its right,
     so the layouts do not overlap). Nothing is written to ~/.config/hypr.
  3. Start SketchyUp on $output; you check the wide layout, then resize to 640 px.
  4. Restore every output and verify the result matches the recording exactly.
EOF
session_confirm "Change $output to scale $scale for this check?" || exit 1
session_init "fractional-scale-$output"
session_monitors before
config=${XDG_CONFIG_HOME:-$HOME/.config}/hypr/monitors.lua
[[ -f $config ]] && { cp -- "$config" "$session_evidence/monitors.lua.before"; sha256sum "$config" > "$session_evidence/monitors.lua.sha256"; }
{
  echo '#!/usr/bin/env bash'
  echo "# Restores the outputs recorded at $(date --iso-8601=seconds). Fallback: hyprctl reload"
  monitor_lines "$session_evidence/monitors-before.json" | while IFS= read -r line; do
    printf 'hyprctl eval %q\n' "$line"
  done
} > "$session_evidence/restore-monitors.sh"
chmod +x "$session_evidence/restore-monitors.sh"
echo "Restore script: $session_evidence/restore-monitors.sh"

applied=false
restore() {
  $applied || return 0
  applied=false
  session_app_stop
  bash "$session_evidence/restore-monitors.sh" > "$session_evidence/restore.log" 2>&1 || true
  sleep 2
  hyprctl -j monitors all > "$session_evidence/monitors-restored.json"
  if same_state "$session_evidence/monitors-before.json" "$session_evidence/monitors-restored.json"; then
    session_note 'outputs restored exactly'
    echo 'Outputs restored and verified.'
  else
    session_note 'RESTORE MISMATCH'
    echo "Restored state differs from the recording. Run: hyprctl reload" >&2
    echo "then compare with $session_evidence/monitors-before.json" >&2
  fi
  if [[ -f $config ]] && ! sha256sum -c --quiet "$session_evidence/monitors.lua.sha256"; then
    echo 'Warning: monitors.lua changed during the check.' >&2
  fi
}
trap restore EXIT
trap 'exit 130' INT TERM

# Build the change from the recorded state.
read -r ox owidth oscale < <(jq -r --arg o "$output" '.[] | select(.name == $o) | "\(.x) \(.width) \(.scale)"' "$session_evidence/monitors-before.json")
old_right=$(jq -n "$ox + ($owidth / $oscale | round)")
new_right=$(jq -n "$ox + ($owidth / $scale | round)")
change=$(jq --arg o "$output" --argjson s "$scale" --argjson old "$old_right" --argjson new "$new_right" '
  map(select(.disabled | not) | if .name == $o then .scale = $s
      elif .x == $old then .x = $new else empty end)' "$session_evidence/monitors-before.json")
printf '%s\n' "$change" > "$session_evidence/monitors-requested.json"
applied=true
monitor_lines "$session_evidence/monitors-requested.json" | while IFS= read -r line; do
  session_note "apply: $line"
  hyprctl eval "$line" >> "$session_evidence/apply.log" 2>&1
done
for _ in $(seq 20); do
  hyprctl -j monitors | jq -e --arg o "$output" --argjson s "$scale" '.[] | select(.name == $o) | .scale == $s' >/dev/null && break
  sleep 0.25
done
session_monitors scaled
jq -e --arg o "$output" --argjson s "$scale" '.[] | select(.name == $o) | .scale == $s' "$session_evidence/monitors-scaled.json" >/dev/null ||
  session_die "Scale $scale was not applied (see apply.log); restoring."

session_app_start wayland --demo
session_wait_window 30
workspace=$(jq -r --arg o "$output" '.[] | select(.name == $o) | .activeWorkspace.id' "$session_evidence/monitors-scaled.json")
session_move_window "$workspace"
cat <<EOF
Wide layout: maximize or tile SketchyUp across $output.
  - Tool rail, Commands button, model panel tabs and Measurements are all visible and usable.
  - Text and icons are crisp; the viewport fills its area with no offset or blur.
  - Hover an edge endpoint: the inference marker sits under the pointer; clicking
    selects exactly what is under the pointer (picking and overlays aligned).
  - Select a face and push/pull with a typed distance in Measurements.
EOF
session_pause 'Check the wide layout.'
session_window_record wide
session_screenshot wide
echo 'Now make the window float and resize it to about 640 px wide (current logical width shown).'
echo 'Press Enter when it is close to 640.'
while ! read -r -t 0.5 _; do
  printf '\r  width %s px   ' "$(session_app_window | jq -r '.size[0]')"
done
echo
cat <<EOF
640 px layout:
  - The same actions remain reachable (Commands button, tool rail or palette).
  - Measurements is visible and accepts a typed value.
EOF
session_pause 'Check the 640 px layout.'
session_window_record narrow
session_screenshot narrow
session_result
