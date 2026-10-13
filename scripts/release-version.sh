#!/usr/bin/env bash
# Print the release version. CMake's project(SketchyUp VERSION x.y.z) is the single
# source of truth (ADR 0162); the Arch PKGBUILD's pkgver must agree with it.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=$(sed -n 's/^project(SketchyUp VERSION \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)[ )].*$/\1/p' \
  "$root/CMakeLists.txt")
pkgver=$(sed -n 's/^pkgver=\([^[:space:]]*\)$/\1/p' "$root/packaging/arch/PKGBUILD")
if [[ -z $version || $(wc -l <<< "$version") -ne 1 ]]; then
  echo 'release-version: cannot read project(SketchyUp VERSION x.y.z) from CMakeLists.txt' >&2
  exit 1
fi
if [[ $version != "$pkgver" ]]; then
  echo "release-version: CMakeLists.txt says $version but packaging/arch/PKGBUILD pkgver is '${pkgver}'" >&2
  exit 1
fi
printf '%s\n' "$version"
