#!/usr/bin/env bash
# Private CI client library for the independently reproduced Wayland 1.26 leak.
# This does not install or replace the system Wayland libraries.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Usage: scripts/ci-wayland-client.sh /absolute/build/directory}
mkdir -p -- "$output"
output=$(realpath -- "$output")
if [[ $(pkg-config --modversion wayland-client) != 1.26.0 ]]; then
  echo 'Re-evaluate the destroyed-proxy fix for this Wayland version.' >&2
  exit 1
fi
curl -fLsS --retry 3 --max-time 120 \
  https://gitlab.freedesktop.org/wayland/wayland/-/releases/1.26.0/downloads/wayland-1.26.0.tar.xz \
  -o "$output/wayland-1.26.0.tar.xz"
echo "64176eaa46e4969903e286f8e5ef8331affc17fdf03ac9b58381d2b23162b7a3  $output/wayland-1.26.0.tar.xz" | sha256sum -c -
tar -xf "$output/wayland-1.26.0.tar.xz" -C "$output"
patch --batch --fuzz=0 -d "$output/wayland-1.26.0" -p1 < "$root/scripts/wayland-1.26-destroyed-proxy.patch"
meson setup "$output/build" "$output/wayland-1.26.0" \
  -Ddocumentation=false -Ddtd_validation=false
ninja -C "$output/build" -j2
meson test -C "$output/build" --print-errorlogs
mkdir -p "$output/lib"
# Keep the compositor/server and all other system libraries unchanged.
cp -L "$output/build/src/libwayland-client.so.0" "$output/lib/libwayland-client.so.0"
cp "$output/wayland-1.26.0/COPYING" "$output/lib/Wayland-COPYING"
c++ -std=c++20 -g -fsanitize=address -fno-omit-frame-pointer \
  "$root/docs/verification/probes/wayland-destroyed-proxy.cpp" \
  -o "$output/proxy-probe" $(pkg-config --cflags --libs wayland-client wayland-server) -pthread
LD_LIBRARY_PATH="$output/lib" ASAN_OPTIONS=detect_leaks=1 "$output/proxy-probe"
LD_LIBRARY_PATH="$output/lib" ASAN_OPTIONS=detect_leaks=1 "$output/proxy-probe" --dispatch-first
