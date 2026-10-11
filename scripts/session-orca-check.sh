#!/usr/bin/env bash
# R084 interactive screen-reader session: Orca with speech, a debug log of what
# it speaks, and an owned SketchyUp demo model. The owner follows the checklist
# by keyboard and notes what is heard. Only an Orca started here is stopped
# here. See docs/M9_HARDWARE_SESSION.md, section 9.
set -euo pipefail
if (( $# != 1 )) || [[ $1 != wayland && $1 != xcb ]]; then
  echo 'Usage: session-orca-check.sh wayland|xcb' >&2
  exit 2
fi
platform=$1
source "$(dirname -- "${BASH_SOURCE[0]}")/session-lib.sh"
command -v orca >/dev/null || session_die 'Orca is not installed (see the runbook preflight).'
command -v spd-say >/dev/null || session_die 'speech-dispatcher (spd-say) is not installed.'
! pgrep -x orca >/dev/null || session_die 'Orca is already running; quit it so this run owns the debug log.'

checklist=$(cat <<'EOF'
R084 screen reader (keyboard only; write down what you hear):
 S1 SketchyUp starts with the demo. Orca announces the window title.
 S2 F6 until the viewport has focus. Expect "3D model viewport" and the selection
    summary ("No selection").
 S3 F6 to the model panel, open the Outliner tab, arrows or Home to the slab (choosing
    an entity selects it). Expect its name and role to be spoken.
 S4 Back in the viewport (F6): the selection summary now names the selection
    (for example "1 group" or "1 face").
 S5 Switch the model panel to Info (Entity info) by keyboard (Ctrl+Tab, or arrows on the
    tab bar). Note how you got there. Expect the measured values to be read:
    8 x 6 x 0.15 m bounds, 100.2 m2 area, 7.2 m3 volume (wording may differ).
 S6 Ctrl+K, type "rect", arrow through results. Expect each command name spoken.
 S7 Escape out; Ctrl+Z. Expect the undo to be announced or the summary to change.
EOF
)
printf '%s\n' "$checklist"
session_confirm 'Start Orca (speech on) and SketchyUp?' || exit 1
session_init "orca-$platform"
printf '%s\n' "$checklist" > "$session_evidence/checklist.txt"
{
  pacman -Q orca speech-dispatcher espeak-ng at-spi2-core 2>&1 || true
  command -v gsettings >/dev/null && echo "toolkit-accessibility: $(gsettings get org.gnome.desktop.interface toolkit-accessibility 2>&1)"
} > "$session_evidence/accessibility-before.txt"
spd-say -w 'SketchyUp screen reader session' || session_die 'Speech output failed; fix audio before continuing.'
(cd "$session_evidence" && exec orca --replace --debug-file="$session_evidence/orca-debug.txt" \
  > "$session_evidence/orca.log" 2>&1) &
orca_pid=$!
stop_all() {
  session_app_stop
  if kill -0 "$orca_pid" 2>/dev/null; then kill -TERM "$orca_pid"; wait "$orca_pid" 2>/dev/null || true; fi
}
trap stop_all EXIT
sleep 3
kill -0 "$orca_pid" 2>/dev/null || session_die 'Orca exited; see orca.log.'
session_app_env=(QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1)
session_app_start "$platform" --demo
session_wait_window 30
echo 'Type a step id (for example S3) and Enter to note what you heard; Enter alone to finish.'
while :; do
  read -r -p 'step> ' step
  [[ -z $step ]] && break
  [[ $step =~ ^[A-Za-z0-9-]{1,12}$ ]] || { echo 'Use a short step id.'; continue; }
  read -r -p "  heard for $step: " heard
  session_note "$step heard: $heard"
  session_screenshot "step-$step-$(date +%H%M%S)"
done
stop_all
trap - EXIT
grep -h -E 'SPEECH OUTPUT|BRAILLE LINE' "$session_evidence"/orca-debug*.txt "$session_evidence"/debug-*.out \
  > "$session_evidence/spoken.txt" 2>/dev/null || true
echo "Spoken lines extracted: $(wc -l < "$session_evidence/spoken.txt")"
command -v gsettings >/dev/null &&
  echo "toolkit-accessibility after: $(gsettings get org.gnome.desktop.interface toolkit-accessibility 2>&1)" \
    > "$session_evidence/accessibility-after.txt"
session_result
