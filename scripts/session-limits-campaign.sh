#!/usr/bin/env bash
# R114: run the limit families of limits_hardware_benchmark on the GPU PC, one transient
# systemd user unit per case (benchmark only; no product limit is read or changed here).
# Run it on the PC, detached so that an ssh drop does not stop it:
#   setsid nohup scripts/session-limits-campaign.sh ROOT FAMILY:TIER[:RUNS] ... > ROOT/campaign.log 2>&1 &
# Without RUNS a tier runs three times while each run takes under five minutes, and once otherwise.
# A family stops at its first tier that fails (a refusal, a crash or the 30-minute limit): later
# tiers of that family are skipped and noted in load.log.
# ROOT holds bin/limits_hardware_benchmark (built with -DSKETCHYUP_BENCHMARK_LIMIT_SCALE=8)
# and receives runs/FAMILY-TIER-rN/ with the JSON result, stderr, exit status, GPU samples
# (gpu.csv), uptime before and after, and the load log (load.log).
# Each case: MemoryMax=18G, MemorySwapMax=0, RuntimeMaxSec=1800, no core dumps, fullscreen on the
# owner's display. Before a case the script waits while the GPU is busy (the owner may be using
# the machine); it never closes anything it did not start.
set -euo pipefail
usage() {
  echo 'Usage: session-limits-campaign.sh ROOT FAMILY:TIER[:RUNS] ...' >&2
  exit 2
}
(( $# >= 2 )) || usage
root=$1
shift
bin=$root/bin/limits_hardware_benchmark
[[ -x $bin ]] || { echo "Missing $bin" >&2; exit 1; }
samples=${LIMIT_EDIT_SAMPLES:-20}
runs_dir=$root/runs
mkdir -p "$runs_dir"
log=$root/load.log
note() { printf '%s %s\n' "$(date --iso-8601=seconds)" "$*" >> "$log"; }

# The AMD card: the first DRM card whose vendor is AMD (0x1002).
device=
for card in /sys/class/drm/card[0-9]; do
  if [[ -r $card/device/vendor ]] && [[ $(cat "$card/device/vendor") == 0x1002 ]]; then
    device=$card/device
    break
  fi
done
[[ -n $device ]] || { echo 'No AMD DRM device found' >&2; exit 1; }

# The running X session's display, as the M9 campaign script borrows it.
desktop_pid=$(pgrep -u "$USER" -x xfce4-session | head -1) || { echo 'No XFCE session' >&2; exit 1; }
while IFS= read -r -d '' item; do
  case $item in
    DISPLAY=*|XAUTHORITY=*|XDG_RUNTIME_DIR=*|DBUS_SESSION_BUS_ADDRESS=*) export "${item?}" ;;
  esac
done < "/proc/$desktop_pid/environ"

sampler_pid=
start_sampler() {
  local csv=$1
  echo 'time,vram_used,gtt_used,gpu_busy_percent' > "$csv"
  while :; do
    printf '%s,%s,%s,%s\n' "$(date +%s.%N)" "$(cat "$device/mem_info_vram_used")" \
      "$(cat "$device/mem_info_gtt_used")" "$(cat "$device/gpu_busy_percent")" >> "$csv"
    sleep 0.5
  done &
  sampler_pid=$!
}
stop_sampler() {
  if [[ -n $sampler_pid ]]; then
    kill "$sampler_pid" 2>/dev/null || true
    wait "$sampler_pid" 2>/dev/null || true
    sampler_pid=
  fi
}
trap 'stop_sampler' EXIT

# Free disk: stop below 50 GB, as the owner's allowance requires.
free_gb() { df -BG --output=avail "$root" | tail -1 | tr -dc '0-9'; }

run_case() {
  local family=$1 tier=$2 run=$3
  local label=$family-$tier-r$run
  local dir=$runs_dir/$label
  mkdir -p "$dir"
  # Wait until the GPU is quiet: the owner may be using the machine.
  local waited=0
  while (( $(cat "$device/gpu_busy_percent") > 10 )); do
    if (( waited == 0 )); then note "$label: waiting, GPU busy at $(cat "$device/gpu_busy_percent") percent"; fi
    sleep 60
    waited=$((waited + 60))
  done
  if (( $(free_gb) < 50 )); then
    note "$label: stopped, free disk below 50 GB"
    last_status=99
    return 0
  fi
  note "$label: start; uptime: $(uptime | sed 's/.*load average/load average/'); MemAvailable $(awk '/MemAvailable/ {print $2}' /proc/meminfo) kB; GPU busy $(cat "$device/gpu_busy_percent")%; waited ${waited}s"
  start_sampler "$dir/gpu.csv"
  local unit=r114-$family-$tier-r$run-$$
  local status=0 started_at
  started_at=$(date +%s)
  # The unit's environment: the X session, the Qt platform, and the case's work directory.
  local envs=(DISPLAY="$DISPLAY" XAUTHORITY="${XAUTHORITY:-}" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}"
              DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-}" QT_QPA_PLATFORM=xcb
              QT_QPA_PLATFORMTHEME=generic QT_SCALE_FACTOR=1 SKETCHYUP_BENCHMARK_FULLSCREEN=1
              LIMIT_WORK_DIR="$dir")
  local setenv=()
  for e in "${envs[@]}"; do setenv+=(--setenv="$e"); done
  systemd-run --user --wait --pipe --collect --unit="$unit" \
    -p MemoryMax=18G -p MemorySwapMax=0 -p LimitCORE=0 -p RuntimeMaxSec=1800 \
    "${setenv[@]}" "$bin" "$family" "$tier" "$samples" > "$dir/result.json" 2> "$dir/stderr.txt" || status=$?
  stop_sampler
  echo "$status" > "$dir/exit.txt"
  last_status=$status
  last_seconds=$(( $(date +%s) - started_at ))
  note "$label: end, exit $status; uptime: $(uptime | sed 's/.*load average/load average/'); GPU busy $(cat "$device/gpu_busy_percent")%"
  return 0
}

last_status=0
last_seconds=0
stopped=
for spec in "$@"; do
  IFS=: read -r family tier runs <<< "$spec"
  [[ $family =~ ^[a-z-]+$ && $tier =~ ^[0-9]+$ ]] || usage
  if [[ $family == "$stopped" ]]; then
    note "$family-$tier: skipped, the family stopped at an earlier tier"
    continue
  fi
  max_runs=3
  if [[ -n ${runs:-} ]]; then
    [[ $runs =~ ^[0-9]+$ ]] || usage
    max_runs=$runs
  fi
  for (( r = 1; r <= max_runs; r++ )); do
    run_case "$family" "$tier" "$r"
    if (( last_status != 0 )); then
      stopped=$family
      note "$family-$tier: failed with status $last_status; the family stops here"
      break
    fi
    # Adaptive repeats: a run of five minutes or more is not repeated.
    if [[ -z ${runs:-} ]] && (( last_seconds >= 300 )); then
      break
    fi
  done
done
note 'campaign finished'
