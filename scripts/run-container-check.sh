#!/usr/bin/env bash
# One bounded local acceptance job. All temporary files live on persistent disk.
set -euo pipefail
[[ $# -ge 3 && $1 =~ ^[a-z0-9][a-z0-9-]{0,40}$ ]] || {
  echo 'Usage: run-container-check.sh RUN_NAME LOCAL_IMAGE COMMAND [ARGUMENTS...]' >&2
  exit 2
}
run_name=$1
image=$2
shift 2
source_root=$(git rev-parse --show-toplevel)
common_git=$(git rev-parse --path-format=absolute --git-common-dir)
state_root=$(dirname "$common_git")/build/local-checks
mkdir -p "$state_root"
state_root=$(realpath "$state_root")
disk_path() {
  local path=$1
  [[ $path != /tmp && $path != /tmp/* && $path != *','* ]] || {
    echo "Check storage must be disk-backed and outside /tmp: $path" >&2
    exit 2
  }
  case $(stat -f -c %T "$path") in
    tmpfs|ramfs) echo "Refusing memory-backed check storage: $path" >&2; exit 2 ;;
  esac
}
disk_path "$state_root"
disk_path "$source_root"
exec 9>"$state_root/active.lock"
flock -n 9 || { echo 'Another local acceptance job holds the shared lock.' >&2; exit 75; }
if [[ -n $(docker ps -q --filter label=io.sketchyup.local-check=true) ]]; then
  echo 'An owned acceptance container is still active; inspect it before starting another.' >&2
  exit 75
fi
available_kib=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
[[ $available_kib =~ ^[0-9]+$ && $available_kib -ge 6291456 ]] || {
  echo 'At least 6 GiB available host memory is required before starting a 4 GiB job.' >&2
  exit 75
}
docker image inspect "$image" >/dev/null # Never download an image implicitly.
run_root=$state_root/$run_name
mkdir "$run_root" # Refuse to overwrite an earlier attempt's evidence.
mkdir "$run_root/tmp" "$run_root/capture" "$run_root/package"
package_root=$(realpath "${SKETCHYUP_CHECK_PACKAGE_DIR:-$run_root/package}")
disk_path "$package_root"
cpu_slot=$(python3 -c 'import os; print(max(os.sched_getaffinity(0)))')
container=sketchyup-check-$run_name
created=false
wait_pid=
finish() {
  if $created; then
    state=$(docker inspect --format '{{.State.Status}}' "$container")
    if [[ $state == paused ]]; then docker unpause "$container" >/dev/null; fi
    if [[ $state == running || $state == paused ]]; then
      docker stop --timeout 5 "$container" >/dev/null
    fi
    if [[ -n $wait_pid ]]; then wait "$wait_pid" || true; fi
    docker logs "$container" > "$run_root/output.log" 2>&1
    docker inspect --format '{{json .State}}' "$container" > "$run_root/state.json"
  fi
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
docker create --name "$container" --pull=never --init --restart=no \
  --label io.sketchyup.local-check=true \
  --memory=4g --memory-swap=4g --cpus=1 --cpuset-cpus="$cpu_slot" --pids-limit=256 \
  --mount "type=bind,source=$source_root,target=/source,readonly" \
  --mount "type=bind,source=$run_root/tmp,target=/tmp" \
  --mount "type=bind,source=$run_root/capture,target=/capture" \
  --mount "type=bind,source=$package_root,target=/work/package" \
  --workdir /source \
  -e TMPDIR=/tmp -e CMAKE_BUILD_PARALLEL_LEVEL=1 -e MAKEFLAGS=-j1 \
  -e OMP_NUM_THREADS=1 -e OPENBLAS_NUM_THREADS=1 \
  -e XDG_CONFIG_HOME=/capture/config -e XDG_DATA_HOME=/capture/data \
  -e XDG_CACHE_HOME=/capture/cache -e SKETCHYUP_PACKAGE_SANDBOX=1 \
  "$image" "$@" > "$run_root/container-id.txt"
created=true
docker inspect --format '{{json .HostConfig}}' "$container" > "$run_root/limits.json"
printf 'Running %s: one CPU, 4 GiB memory, no extra swap; logs: %s/output.log\n' \
  "$container" "$run_root"
docker start "$container" >/dev/null
# Waiting on a background monitor lets TERM/INT immediately run our stop trap,
# rather than deferring it until a foreground attached container exits.
docker wait "$container" > "$run_root/exit-code.txt" &
wait_pid=$!
wait "$wait_pid"
exit "$(cat "$run_root/exit-code.txt")"
