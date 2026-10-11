#!/usr/bin/env bash
# Shared helpers for the owner-attended M9 hardware session scripts
# (docs/M9_HARDWARE_SESSION.md). Source this file; do not execute it.
#
# Every helper here is read-only towards the desktop except session_app_start
# (which starts an owned SketchyUp process) and session_screenshot (which reads
# pixels). Nothing here changes monitors, suspends or moves windows; the
# individual scripts do that only after session_confirm.

[[ ${BASH_SOURCE[0]} != "$0" ]] || { echo 'Source session-lib.sh; do not run it.' >&2; exit 2; }

session_repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
# Release build of the named session revision (preflight step P2).
session_build=${SKETCHYUP_SESSION_BUILD:-$session_repo/build/hw-session-release}
# Evidence root on persistent disk, one directory per check run.
session_root=${SKETCHYUP_SESSION_ROOT:-$HOME/sketchyup-verification/m9-hardware-session}
session_evidence=
session_app_pid=
session_app_env=() # Extra NAME=VALUE pairs for session_app_start.

session_die() { echo "$*" >&2; exit 1; }

session_disk_path() {
  local path=$1
  [[ $path != /tmp && $path != /tmp/* ]] || session_die "Evidence must be outside /tmp: $path"
  case $(stat -f -c %T "$path") in
    tmpfs|ramfs) session_die "Refusing memory-backed evidence storage: $path" ;;
  esac
}

# session_init NAME: create a fresh evidence directory and record the environment.
session_init() {
  local name=$1
  [[ $name =~ ^[a-z0-9][a-z0-9-]{0,40}$ ]] || session_die "Invalid check name: $name"
  mkdir -p "$session_root"
  session_disk_path "$session_root"
  session_evidence=$session_root/$name-$(date +%Y%m%d-%H%M%S)
  mkdir "$session_evidence" # Never reuse an earlier attempt's evidence.
  mkdir "$session_evidence"/{config,data,cache,tmp}
  {
    echo "check: $name"
    echo "started: $(date --iso-8601=seconds)"
    echo "source: $(git -C "$session_repo" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "kernel: $(uname -r)"
    if command -v hyprctl >/dev/null; then echo "compositor: $(hyprctl version 2>/dev/null | head -1)"; fi
    pacman -Q qt6-base qt6-wayland mesa 2>/dev/null | sed 's/^/package: /' || true
    if [[ -x $session_build/sketchyup ]]; then
      echo "app sha256: $(sha256sum "$session_build/sketchyup" | cut -d' ' -f1)"
    fi
  } > "$session_evidence/environment.txt"
  echo "Evidence: $session_evidence"
}

# session_confirm PROMPT: proceed only on an explicit "yes" from a terminal.
session_confirm() {
  local reply
  [[ -t 0 ]] || session_die 'Refusing to act without an interactive terminal.'
  read -r -p "$1 Type yes to continue: " reply
  [[ $reply == yes ]]
}

# session_pause TEXT: print a step and wait for Enter.
session_pause() {
  [[ -t 0 ]] || session_die 'Interactive terminal required.'
  read -r -p "$1 [Enter when done] " _
}

session_note() { printf '%s %s\n' "$(date +%H:%M:%S)" "$*" >> "$session_evidence/notes.txt"; }

# session_monitors LABEL: read-only snapshot of every output.
session_monitors() {
  hyprctl -j monitors all > "$session_evidence/monitors-$1.json"
  hyprctl monitors all > "$session_evidence/monitors-$1.txt"
  session_note "monitors recorded: $1"
}

# session_app_start PLATFORM [ARGS...]: start SketchyUp with private settings,
# data (recovery journal), cache and temporary directories. PLATFORM is wayland or xcb.
session_app_start() {
  local platform=$1
  shift
  [[ $platform == wayland || $platform == xcb ]] || session_die 'Platform must be wayland or xcb'
  [[ -x $session_build/sketchyup ]] || session_die "Missing $session_build/sketchyup (preflight P2)"
  env XDG_CONFIG_HOME="$session_evidence/config" XDG_DATA_HOME="$session_evidence/data" \
    XDG_CACHE_HOME="$session_evidence/cache" TMPDIR="$session_evidence/tmp" \
    QT_QPA_PLATFORM="$platform" QT_LOGGING_RULES='qt.qpa.screen=true' \
    ${session_app_env[@]+"${session_app_env[@]}"} \
    "$session_build/sketchyup" "$@" > "$session_evidence/app.log" 2>&1 &
  session_app_pid=$!
  echo "$session_app_pid" > "$session_evidence/app.pid"
  session_note "app started: pid $session_app_pid platform $platform args $*"
}

session_app_alive() { [[ -n $session_app_pid ]] && kill -0 "$session_app_pid" 2>/dev/null; }

# session_app_window: the mapped Hyprland client JSON for the owned app (largest window).
session_app_window() {
  hyprctl -j clients | jq -c --argjson pid "$session_app_pid" \
    '[.[] | select(.pid == $pid and .mapped)] | sort_by(-(.size[0] * .size[1])) | .[0] // empty'
}

# session_wait_window SECONDS: wait until the app maps a window.
session_wait_window() {
  local deadline=$((SECONDS + $1))
  until [[ -n $(session_app_window) ]]; do
    session_app_alive || session_die 'SketchyUp exited before mapping a window; see app.log.'
    (( SECONDS < deadline )) || session_die 'SketchyUp window did not appear.'
    sleep 0.2
  done
}

# session_window_record LABEL: the app window's monitor, position, size and workspace.
session_window_record() {
  session_app_window > "$session_evidence/window-$1.json"
  session_note "window $1: $(jq -c '{monitor, at, size, workspace: .workspace.name}' "$session_evidence/window-$1.json" 2>/dev/null)"
}

# session_move_window WORKSPACE: move only the owned app window (by address,
# never the focused window) without following it, as scripts/test-hyprland-output.py does.
session_move_window() {
  local workspace=$1 address
  address=$(session_app_window | jq -r '.address // empty')
  [[ $address =~ ^0x[0-9a-fA-F]+$ ]] || session_die 'No owned app window to move.'
  [[ $workspace =~ ^([1-9][0-9]*|special:[a-z0-9-]+)$ ]] || session_die "Invalid workspace: $workspace"
  hyprctl dispatch "hl.dsp.window.move({workspace=\"$workspace\",follow=false,window=\"address:$address\"})" \
    > "$session_evidence/move-$(date +%H%M%S%N).txt" 2>&1
  session_note "moved app window $address to workspace $workspace"
}

# session_screenshot LABEL: capture only the app window's rectangle.
session_screenshot() {
  local window geometry
  window=$(session_app_window)
  if [[ -z $window ]]; then
    session_note "screenshot $1 skipped: no mapped app window"
    return 0
  fi
  geometry=$(jq -r '"\(.at[0]),\(.at[1]) \(.size[0])x\(.size[1])"' <<< "$window")
  if grim -g "$geometry" "$session_evidence/$1.png"; then
    session_note "screenshot $1: $geometry"
  else
    session_note "screenshot $1 failed"
  fi
}

# session_recovery LABEL: list the private recovery journal with hashes.
session_recovery() {
  find "$session_evidence/data" -path '*recovery*' -type f -printf '%TY-%Tm-%Td %TH:%TM:%TS %s %p\n' \
    | sort > "$session_evidence/recovery-$1.txt"
  find "$session_evidence/data" -path '*recovery*' -type f -exec sha256sum {} + \
    >> "$session_evidence/recovery-$1.txt" 2>/dev/null || true
  session_note "recovery listing $1: $(grep -c ' /' "$session_evidence/recovery-$1.txt" || true) lines"
}

# session_result: record the owner's pass/fail verdict and notes.
session_result() {
  local verdict= notes=
  [[ -t 0 ]] || session_die 'Interactive terminal required.'
  until [[ $verdict == pass || $verdict == fail || $verdict == blocked ]]; do
    read -r -p 'Result (pass/fail/blocked): ' verdict
  done
  read -r -p 'One-line observation: ' notes
  jq -n --arg v "$verdict" --arg n "$notes" --arg t "$(date --iso-8601=seconds)" \
    '{result: $v, observation: $n, recordedAt: $t}' > "$session_evidence/result.json"
  echo "Recorded $verdict in $session_evidence/result.json"
}

# session_app_stop: close the owned app if it is still running.
session_app_stop() {
  if session_app_alive; then
    kill -TERM "$session_app_pid" 2>/dev/null || true
    wait "$session_app_pid" 2>/dev/null || true
  fi
  if [[ -n $session_app_pid ]]; then
    session_note "app stopped"
  fi
}
