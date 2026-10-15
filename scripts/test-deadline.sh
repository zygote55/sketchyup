#!/usr/bin/env bash
# Run one test command with a deadline, like `timeout SECONDS command`, but when the
# deadline passes, first print the thread stacks of every process the command started,
# so a hang explains itself in the CI log (D-034). Stacks come from gdb, or eu-stack when
# gdb is missing; attaching needs ptrace permission (CAP_SYS_PTRACE in a container). Per-thread
# names and states are always printed. With gdb, the process's C stdio buffers are then flushed:
# a test's std::cout output is block-buffered into a pipe, so without this a hung test's
# earlier messages are lost and it looks as if it printed nothing. The command is then stopped
# with SIGTERM, and every process it started is killed if still alive after five seconds.
# A crash anywhere in the command's process tree prints a report (scripts/crash-trace.c), and a
# non-zero exit status or a fatal signal is named. Exit status: the command's, or 124 when the
# deadline passed (as with timeout(1)).
set -uo pipefail
if (( $# < 2 )) || [[ ! $1 =~ ^[1-9][0-9]*$ ]]; then
  echo "Usage: $0 SECONDS command [arguments...]" >&2
  exit 2
fi
limit=$1
shift

# Every descendant of a process, parents before children, from /proc.
descendants() {
  local root=$1 file line pid ppid rest queue next
  local -A children=()
  for file in /proc/[0-9]*/stat; do
    { read -r line < "$file"; } 2>/dev/null || continue
    pid=${line%% *}
    rest=${line##*) } # Fields after the command name, which may contain spaces.
    read -r _ ppid _ <<< "$rest"
    children[$ppid]+=" $pid"
  done
  queue=("$root")
  while (( ${#queue[@]} )); do
    next=${queue[0]}
    queue=("${queue[@]:1}")
    for pid in ${children[$next]:-}; do
      printf '%s\n' "$pid"
      queue+=("$pid")
    done
  done
}

# A process's start time (field 22 of /proc/PID/stat), empty when it is gone or a zombie, so
# a recorded process is never confused with a later one that reuses its PID.
started_at() {
  local line rest fields
  { read -r line < "/proc/$1/stat"; } 2>/dev/null || return 0
  rest=${line##*) }
  read -r -a fields <<< "$rest"
  [[ ${fields[0]} != Z ]] && printf '%s' "${fields[19]}"
}

dump_stacks() {
  local pid exe task line rest state name
  echo "=== Thread stacks at the ${limit} s deadline: $* ===" >&2
  for pid in "$child" $(descendants "$child"); do
    exe=$(readlink "/proc/$pid/exe" 2>/dev/null) || continue
    case ${exe##*/} in
      bash|sh|dash|env|sleep|timeout|xvfb-run|dbus-run-session|dbus-daemon) continue ;;
    esac
    echo "--- pid $pid: $(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null)" >&2
    for task in "/proc/$pid/task/"*; do
      { read -r line < "$task/stat"; } 2>/dev/null || continue
      rest=${line##*) }
      state=${rest%% *}
      name=$(cat "$task/comm" 2>/dev/null)
      printf '    thread %s (%s) state %s\n' "${task##*/}" "$name" "$state" >&2
    done
    if command -v gdb > /dev/null; then
      timeout 60s gdb -p "$pid" -batch -nx -q -iex 'set debuginfod enabled off' \
        -ex 'set pagination off' \
        -ex 'set print frame-arguments none' -ex 'thread apply all bt' \
        -ex 'call (int) ((int (*)(void *)) fflush)(0)' < /dev/null >&2 2>&1
    elif command -v eu-stack > /dev/null; then
      timeout 60s eu-stack -p "$pid" < /dev/null >&2 2>&1
    else
      echo '    (no gdb or eu-stack: install one for symbolized stacks)' >&2
    fi
  done
  echo "=== End of thread stacks ===" >&2
}

# Preload the crash reporter (scripts/crash-trace.c), built once per source version. ASan
# processes keep ASan's report; the link-order check would otherwise reject the preload.
crash_trace() {
  local source dir library
  [[ ${SKETCHYUP_CRASH_TRACE:-1} != 0 ]] && command -v cc > /dev/null || return 0
  source=$(dirname "${BASH_SOURCE[0]}")/crash-trace.c
  dir=${TMPDIR:-/tmp}/sketchyup-crash-trace-$(id -u)
  library=$dir/$(sha256sum < "$source" | cut -c1-16).so
  if [[ ! -f $library ]]; then
    mkdir -p "$dir" && cc -shared -fPIC -O1 -o "$library.$$" "$source" &&
      mv -f "$library.$$" "$library" || return 0
  fi
  export LD_PRELOAD="$library${LD_PRELOAD:+ $LD_PRELOAD}"
  export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}verify_asan_link_order=0"
}
crash_trace

# Keep standard input: a background command would otherwise read /dev/null.
"$@" <&0 &
child=$!
sleep "$limit" < /dev/null > /dev/null 2>&1 &
timer=$!
interrupted=
trap 'interrupted=1' TERM INT
finished=
wait -n -p finished "$child" "$timer"
status=$?
if [[ -n $interrupted ]]; then
  # An outer deadline or cancellation: pass it on and report like timeout(1) does.
  kill -TERM "$child" 2> /dev/null
  kill "$timer" 2> /dev/null
  wait "$child"
  exit 143
fi
if [[ $finished == "$child" ]]; then
  kill "$timer" 2> /dev/null
  wait "$timer" 2> /dev/null
  if (( status > 128 )); then
    echo "Killed by signal $(kill -l "$status" 2> /dev/null || echo $((status - 128))): $*" >&2
  elif (( status != 0 )); then
    echo "Exited with status $status: $*" >&2
  fi
  exit "$status"
fi
started=()
for pid in "$child" $(descendants "$child"); do
  started+=("$pid:$(started_at "$pid")")
done
dump_stacks "$@"
kill -TERM "$child" 2> /dev/null
for _ in {1..50}; do
  [[ -n $(started_at "$child") ]] || break
  sleep 0.1
done
# A deadlocked test can outlive its compositor or X server; never leave it running.
for entry in "${started[@]}"; do
  pid=${entry%%:*}
  [[ -n ${entry#*:} && $(started_at "$pid") == "${entry#*:}" ]] && kill -KILL "$pid" 2> /dev/null
done
wait "$child" 2> /dev/null
exit 124
