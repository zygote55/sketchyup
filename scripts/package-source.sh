#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Usage: scripts/package-source.sh /absolute/output/directory}
# Fails loudly when CMakeLists.txt and PKGBUILD disagree about the version.
version=$("$root/scripts/release-version.sh")
archive="sketchyup-$version.tar.gz"
mkdir -p -- "$output"
output=$(realpath -- "$output")
# Normalize times, owners and order so the same commit yields the same archive bytes.
epoch=${SOURCE_DATE_EPOCH:-$(git -C "$root" log -1 --format=%ct 2>/dev/null || date +%s)}
# Include the current reviewable source without build output or Git metadata.
tar -C "$root" --transform="s,^,sketchyup-$version/," \
  --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner \
  -cf - \
  CMakeLists.txt CMakePresets.json LICENSE src tests scripts third_party resources examples docs/api docs/decisions docs/M5_ACCEPTANCE.md docs/M6_ACCEPTANCE.md docs/M7_ACCEPTANCE.md docs/M8_ACCEPTANCE.md docs/M9_ACCEPTANCE.md docs/TOOL_REFERENCE.md docs/AI_MODELING.md docs/USER_GUIDE.md docs/SUPPORT_MATRIX.md \
  | gzip -n > "$output/$archive"
cp -- "$root/packaging/arch/PKGBUILD" "$output/PKGBUILD"
digest=$(sha256sum "$output/$archive" | cut -d ' ' -f 1)
sed -i "s/REPLACE_WITH_SOURCE_SHA256/$digest/" "$output/PKGBUILD"
sha256sum "$output/$archive"
