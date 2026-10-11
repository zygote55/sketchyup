#!/usr/bin/env bash
# R085 suspend/resume check. SketchyUp opens a private copy of the 100 MB
# fixture; the owner makes an unsaved edit, waits for a recovery checkpoint and
# starts a long save (or open). The script suspends the machine with
# `systemctl suspend` as soon as that file operation touches the disk, but only
# after an explicit confirmation. After resume it records the GL/viewport,
# document and recovery-journal state. See docs/M9_HARDWARE_SESSION.md, section 4.
set -euo pipefail
if (( $# != 3 )) || [[ $1 != wayland && $1 != xcb ]] || [[ $2 != save && $2 != open ]]; then
  echo 'Usage: session-suspend-check.sh wayland|xcb save|open FIXTURE.sketchyup' >&2
  exit 2
fi
platform=$1
mode=$2
fixture=$(realpath -e -- "$3")
source "$(dirname -- "${BASH_SOURCE[0]}")/session-lib.sh"
command -v inotifywait >/dev/null || session_die 'inotifywait (inotify-tools) is required.'

cat <<EOF
R085 suspend/resume ($platform, $mode). Steps:
  1. The script copies the fixture into a new evidence directory and starts SketchyUp
     ($( [[ $mode == save ]] && echo 'with that copy open' || echo 'with the demo model' )).
  2. Save mode: you make one edit and leave it unsaved, then wait about 40 s so
     the 30 s recovery checkpoint covers it. The script records the journal.
     (Open mode replaces the document, so it has no unsaved-edit step.)
  3. The script arms a watcher and you $( [[ $mode == save ]] && echo 'press Ctrl+S in SketchyUp' || echo 'open the copied fixture with Ctrl+O' ).
     As soon as the operation touches the file, the script runs systemctl suspend.
  4. Wake the machine and unlock. The script then records monitors, window,
     screenshot, log, journal and file hashes, and you check the app.
Close or save other work first: the whole machine suspends.
EOF
session_confirm 'Start the suspend/resume check?' || exit 1
session_init "suspend-$platform-$mode"
start=$(date '+%Y-%m-%d %H:%M:%S')
mkdir "$session_evidence/model"
cp -- "$fixture" "$session_evidence/model/fixture.sketchyup"
sha256sum "$session_evidence/model/fixture.sketchyup" > "$session_evidence/model-before.sha256"
{
  echo "mem_sleep: $(cat /sys/power/mem_sleep 2>/dev/null)"
  systemd-inhibit --list --no-pager 2>&1
  echo "hypridle: $(systemctl --user is-active hypridle 2>&1 || true)"
} > "$session_evidence/suspend-settings-before.txt"
session_monitors before
if [[ $mode == save ]]; then
  session_app_start "$platform" "$session_evidence/model/fixture.sketchyup"
else
  session_app_start "$platform" --demo
fi
trap session_app_stop EXIT
session_wait_window 60
if [[ $mode == save ]]; then
  session_pause 'Make one edit in SketchyUp and leave it unsaved.'
  read -r -p 'Describe the edit in a few words: ' edit
  session_note "unsaved edit: $edit"
  echo 'Waiting 40 s for the recovery checkpoint...'
  sleep 40
fi
session_recovery before
session_window_record before
session_screenshot before

if [[ $mode == save ]]; then
  # QSaveFile and the .bak copy create new files beside the target.
  watch=(-e create "$session_evidence/model")
  action='press Ctrl+S in SketchyUp'
else
  watch=(-e open "$session_evidence/model/fixture.sketchyup")
  action="press Ctrl+O in SketchyUp and open $session_evidence/model/fixture.sketchyup"
fi
session_confirm 'Arm the watcher? The machine suspends as soon as the file operation starts.' || exit 1
echo ">>> Now $action. Waiting up to 120 s..."
session_note 'watcher armed'
if ! event=$(timeout 120 inotifywait -q "${watch[@]}" --format '%e %f'); then
  session_die 'No file operation seen within 120 s; nothing was suspended.'
fi
session_note "file event: $event; suspending"
sync -f "$session_evidence/notes.txt"
systemctl suspend
sleep 3 # Suspend begins asynchronously; this returns after resume.
session_pause 'After waking and unlocking, return here.'
session_note 'resumed'
sleep 2
journalctl -b --since "$start" --no-pager -g 'PM: suspend (entry|exit)' > "$session_evidence/suspend-journal.txt" 2>&1 || true
session_note "suspend journal lines: $(grep -c 'PM: suspend' "$session_evidence/suspend-journal.txt" || true)"
session_monitors after
session_app_alive && session_note 'app alive after resume' || session_note 'APP NOT RUNNING after resume'
session_window_record after
session_screenshot after
cat <<EOF
Check now:
  - The viewport redraws (orbit with the middle button); no black or frozen area.
  - The $mode finished, or reported a clear error; the progress window is gone.
  - Title and Undo show the expected state: after save, the edit is saved; after a
    failed save, the edit is kept and still marked unsaved.
  - File > Recover work lists nothing unexpected (do not discard anything).
EOF
session_pause 'Then come back here.'
session_recovery after
sha256sum "$session_evidence"/model/* > "$session_evidence/model-after.sha256" 2>&1 || true
grep -i -E 'opengl|context|egl|glx|error|warning' "$session_evidence/app.log" > "$session_evidence/app-gl-lines.txt" || true
coredumpctl list --since "$start" --no-pager > "$session_evidence/coredumps.txt" 2>&1 || true
{
  echo "mem_sleep: $(cat /sys/power/mem_sleep 2>/dev/null)"
  systemd-inhibit --list --no-pager 2>&1
  echo "hypridle: $(systemctl --user is-active hypridle 2>&1 || true)"
} > "$session_evidence/suspend-settings-after.txt"
diff -q "$session_evidence/suspend-settings-before.txt" "$session_evidence/suspend-settings-after.txt" >/dev/null ||
  echo 'Note: inhibitor or idle state differs from before; see suspend-settings-*.txt.'
session_result
