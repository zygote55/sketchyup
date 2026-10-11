#!/usr/bin/env bash
# Run one R082 hardware campaign under the resource controls of the earlier
# records (R082.i/j/m 8-CPU, R082.an 1-CPU, R085.g/R082.bb AMD). Each case runs
# in its own transient systemd user unit with 4 GiB RAM and no swap. Benchmark
# cases open their own fullscreen window, so the script asks before starting.
# See docs/M9_HARDWARE_SESSION.md, sections 1 and 2.
set -euo pipefail
usage() {
  cat >&2 <<'EOF'
Usage: session-benchmark-campaign.sh PROFILE KIND [--dry-run]
  PROFILE  intel-8cpu | intel-1cpu | amd
  KIND     frame         complete frame/edit/inference (R082.i/j): 5 smokes + 15 x 1,000
           history       rendered history and memory, 5 cycles (R082.an): 5 smokes + 5 x 1,000
           capacity-10k  10,000-placement repeated fixture, 5 cycles (needs R082.gg)
           file          100 MB open/save heartbeat (R082.m/bb): 3 warm + 3 verified-cold
Environment:
  SKETCHYUP_SESSION_BUILD    directory holding real_model_benchmark,
                             file_operation_input_tests and sketchyup-text-worker
  SKETCHYUP_SESSION_FIXTURE  100 MB .sketchyup fixture (file only)
  SKETCHYUP_SESSION_ROOT     evidence root on persistent disk
  SESSION_FROM               resume at this case label (new evidence directory)
EOF
  exit 2
}
(( $# >= 2 && $# <= 3 )) || usage
profile=$1
kind=$2
dry_run=false
[[ ${3:-} == --dry-run ]] && dry_run=true
[[ -z ${3:-} || $dry_run == true ]] || usage
case $profile in intel-8cpu|intel-1cpu|amd) ;; *) usage ;; esac
case $kind in frame|history|capacity-10k|file) ;; *) usage ;; esac
[[ $kind != file || $profile != intel-1cpu ]] ||
  { echo 'File timing uses the 8-CPU controls of R082.m/R082.bb; use intel-8cpu or amd.' >&2; exit 2; }
source "$(dirname -- "${BASH_SOURCE[0]}")/session-lib.sh"

# The AMD guest is driven over SSH; borrow the running X session's display.
if [[ $profile == amd && -z ${DISPLAY:-} ]]; then
  desktop_pid=$(pgrep -u "$USER" -x xfce4-session | head -1) ||
    session_die 'No XFCE session found; log in on the AMD machine first.'
  while IFS= read -r -d '' item; do
    case $item in
      DISPLAY=*|XAUTHORITY=*|XDG_RUNTIME_DIR=*|DBUS_SESSION_BUS_ADDRESS=*) export "${item?}" ;;
    esac
  done < "/proc/$desktop_pid/environ"
fi

# Controls, copied from the earlier records' controllers.
props=(MemoryMax=4G MemorySwapMax=0 LimitCORE=0)
envs=(QT_QPA_PLATFORMTHEME=generic QT_SCALE_FACTOR=1 OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 LP_NUM_THREADS=1)
if [[ $profile == amd ]]; then
  envs+=(QT_QPA_PLATFORM=xcb)
  if [[ $kind == file ]]; then
    cpus=0-7; props+=(CPUQuota=800% TasksMax=128 RuntimeMaxSec=90s)
    envs+=(QT_STYLE_OVERRIDE=Fusion QT_IM_MODULE=compose)
  else
    cpus=$(( $(nproc --all) - 1 )); props+=(CPUQuota=100% TasksMax=128 RuntimeMaxSec=900s)
  fi
else
  envs+=(QT_QPA_PLATFORM=wayland)
  if [[ $profile == intel-8cpu ]]; then
    cpus=0,1,3,6,8,10,12,13; props+=(CPUQuota=800% TasksMax=256)
    [[ $kind == file ]] && props+=(RuntimeMaxSec=120s) || props+=(RuntimeMaxSec=300s)
  else
    cpus=13; props+=(CPUQuota=100% TasksMax=128 RuntimeMaxSec=900s)
  fi
fi
[[ $kind == file ]] || envs+=(SKETCHYUP_BENCHMARK_FULLSCREEN=1)

# Case table: label and benchmark arguments.
cases=()
scenarios=(repeated unique deep far textures)
case $kind in
  frame)
    for s in "${scenarios[@]}"; do cases+=("$s-smoke|25 $s"); done
    for s in "${scenarios[@]}"; do for r in 1 2 3; do cases+=("$s-1000-$r|1000 $s"); done; done ;;
  history)
    for s in "${scenarios[@]}"; do cases+=("$s-25-smoke|25 $s 5"); done
    for s in "${scenarios[@]}"; do cases+=("$s-1000-1|1000 $s 5"); done ;;
  capacity-10k)
    for r in 1 2 3; do cases+=("repeated-10000-$r|10000 repeated 5"); done ;;
  file)
    for r in 1 2 3; do cases+=("warm-$r|warm" "verified-cold-$r|verified-cold"); done ;;
esac


echo "Profile $profile, kind $kind, CPUs $cpus, ${#cases[@]} cases"
printf '  %s\n' "${props[@]}" "${envs[@]}"
printf '  case %s\n' "${cases[@]%%|*}"
$dry_run && exit 0

if [[ $kind == file ]]; then
  binary=$session_build/file_operation_input_tests
  fixture=$(realpath -e -- "${SKETCHYUP_SESSION_FIXTURE:?Set the 100 MB fixture path}")
  [[ -x $session_build/sketchyup-text-worker ]] || session_die 'sketchyup-text-worker missing from the build'
else
  binary=$session_build/real_model_benchmark
fi
[[ -x $binary ]] || session_die "Missing $binary (preflight P2)"

session_init "bench-$profile-$kind"
sha256sum "$binary" > "$session_evidence/binary.sha256"
[[ $kind == file ]] && sha256sum "$fixture" > "$session_evidence/fixture.sha256"
printf '%s\n' "${props[@]}" "${envs[@]}" "cpus=$cpus" > "$session_evidence/controls.txt"
available=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
(( available >= 6 * 1024 * 1024 )) || session_die 'At least 6 GiB available memory is required.'
if [[ $profile == amd ]]; then
  glxinfo -B > "$session_evidence/glxinfo.txt" 2>&1 || true
  grep -q 'Accelerated: yes' "$session_evidence/glxinfo.txt" || session_die 'Expected accelerated AMD rendering.'
else
  session_monitors before
  jq -e 'map(select(.disabled | not)) | all(.dpmsStatus)' "$session_evidence/monitors-before.json" >/dev/null ||
    session_die 'Displays must already be awake; this script never wakes them.'
fi
echo 'Close other heavy work (builds, local models, browsers playing video) first.'
[[ $kind == file ]] || echo 'Each benchmark case shows its own fullscreen window for up to a few minutes.'
session_confirm "Run ${#cases[@]} cases now?" || exit 1

summary=$session_evidence/summary.tsv
printf 'case\texit\tmetric\tvalue\tflag\n' > "$summary"
unit=
trap '[[ -n $unit ]] && systemctl --user stop "$unit" 2>/dev/null; exit 130' INT TERM
skipping=$([[ -n ${SESSION_FROM:-} ]] && echo true || echo false)
for entry in "${cases[@]}"; do
  label=${entry%%|*}
  read -r -a args <<< "${entry#*|}"
  if $skipping; then
    [[ $label == "$SESSION_FROM" ]] || continue
    skipping=false
  fi
  # Displays must stay awake for every case; nothing here wakes them.
  if [[ $profile == amd ]]; then
    if command -v xset >/dev/null && ! xset q | grep -q 'Monitor is On'; then
      session_die "Display asleep before $label; wake it and rerun with SESSION_FROM=$label."
    fi
  else
    hyprctl -j monitors > "$session_evidence/$label-monitors.json"
    jq -e 'map(select(.disabled | not)) | all(.dpmsStatus)' "$session_evidence/$label-monitors.json" >/dev/null ||
      session_die "A display is asleep before $label; wake it and rerun with SESSION_FROM=$label."
  fi
  run_env=("${envs[@]}" TMPDIR="$session_evidence/tmp" XDG_CACHE_HOME="$session_evidence/cache"
           XDG_CONFIG_HOME="$session_evidence/config" XDG_DATA_HOME="$session_evidence/data"
           SESSION_LIMITS="$session_evidence/$label-limits.txt")
  if [[ $kind == file ]]; then
    run_env+=(SKETCHYUP_TEST_FILE_CACHE_POLICY="${args[0]}")
    args=("$fixture")
  fi
  for name in WAYLAND_DISPLAY DISPLAY XDG_RUNTIME_DIR XAUTHORITY DBUS_SESSION_BUS_ADDRESS; do
    [[ -n ${!name:-} ]] && run_env+=("$name=${!name}")
  done
  unit=sketchyup-m9-$kind-$label-$$
  cmd=(systemd-run --user --wait --pipe --collect "--unit=$unit")
  for p in "${props[@]}"; do cmd+=(-p "$p"); done
  for e in "${run_env[@]}"; do cmd+=("--setenv=$e"); done
  # The worker records the kernel limits it actually received, then reports
  # its own memory and swap peaks after the measured process exits.
  cmd+=(/usr/bin/taskset -c "$cpus" /usr/bin/bash -c '
    cg=/sys/fs/cgroup$(cut -d: -f3 /proc/self/cgroup)
    for n in memory.max memory.swap.max cpu.max pids.max; do echo "$n=$(cat "$cg/$n")"; done > "$SESSION_LIMITS"
    echo "affinity=$(taskset -pc $$ | sed "s/.*: //")" >> "$SESSION_LIMITS"
    status=0; "$@" || status=$?
    for n in memory.peak memory.swap.peak; do echo "$n=$(cat "$cg/$n" 2>/dev/null)"; done >> "$SESSION_LIMITS"
    exit "$status"' worker "$binary" "${args[@]}")
  echo "== $label"
  status=0
  "${cmd[@]}" > "$session_evidence/$label.out" 2> "$session_evidence/$label.stderr" || status=$?
  unit=
  echo "$status" > "$session_evidence/$label-exit.txt"
  # real_model_benchmark prints one indented JSON document; the file test
  # prints progress lines followed by a one-line JSON result.
  report=$(jq -c . "$session_evidence/$label.out" 2>/dev/null) ||
    report=$(grep '^{' "$session_evidence/$label.out" | tail -1 || true)
  if [[ -n $report ]]; then
    jq -r --arg c "$label" --arg s "$status" '
      def flag(k; v): if (k == "viewportGpuCompleteP95Ms" and v > 16.7)
        or ((k == "editToViewportGpuCompleteP95Ms" or k == "editAndReadbackP95Ms") and v > 100)
        or ((k == "pickP95Ms" or k == "inferenceQueryP95Ms") and v > 50)
        or (k == "operationMs" and v > 5000) or (k == "maximumHeartbeatGapMs" and v > 50)
        then "MISS" else "" end;
      [paths(numbers) as $p | {k: ($p | map(tostring) | join(".")), leaf: ($p[-1] | tostring), v: getpath($p)}
        | select(.leaf | test("P95Ms$|^operationMs$|^maximumHeartbeatGapMs$"))]
      | .[] | [$c, $s, .k, (.v | tostring), flag(.leaf; .v)] | @tsv' <<< "$report" >> "$summary"
    jq -r '"  fixture \(.fixtureSha256 // "n/a")  graphics \(.graphics // "n/a")"' <<< "$report"
  else
    printf '%s\t%s\tno-report\t\t\n' "$label" "$status" >> "$summary"
  fi
  (( status == 0 )) || { echo "Case $label failed ($status); stopping with evidence retained." >&2; break; }
done
trap - INT TERM
! $skipping || session_die "SESSION_FROM=$SESSION_FROM matches no case."
[[ $profile == amd ]] || session_monitors after
column -t -s $'\t' "$summary"
echo "Summary: $summary  (MISS marks a value above its BUILD_PLAN target)"
