#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Usage: scripts/package-source.sh /absolute/output/directory}
mkdir -p -- "$output"
output=$(realpath -- "$output")
# Include the current reviewable source without build output or Git metadata.
tar -C "$root" --transform='s,^,sketchyup-0.1.0/,' \
  -czf "$output/sketchyup-0.1.0.tar.gz" \
  CMakeLists.txt CMakePresets.json LICENSE src tests scripts third_party resources examples docs/api docs/decisions docs/M5_ACCEPTANCE.md docs/M6_ACCEPTANCE.md
cp -- "$root/packaging/arch/PKGBUILD" "$output/PKGBUILD"
digest=$(sha256sum "$output/sketchyup-0.1.0.tar.gz" | cut -d ' ' -f 1)
sed -i "s/REPLACE_WITH_SOURCE_SHA256/$digest/" "$output/PKGBUILD"
sha256sum "$output/sketchyup-0.1.0.tar.gz"
