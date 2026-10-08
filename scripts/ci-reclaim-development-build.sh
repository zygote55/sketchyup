#!/usr/bin/env bash
# Normal checks have finished; retain only the later fractional-scale consumers.
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$project_root"
[[ -f CMakeLists.txt && -f CMakePresets.json && -d build/dev ]]
[[ ! -L build && ! -L build/dev && ! -e build/ci-retained-dev ]]
retained=(viewport_tests keyboard_modeling_input_tests text_size_input_tests
          keyboard_outliner_input_tests shortcut_input_tests
          sketchyup-text-worker sketchyup-extension-worker)
for target in "${retained[@]}"; do
  [[ -f build/dev/$target && -x build/dev/$target && ! -L build/dev/$target ]]
done
du -sh build/dev
df -h .
mkdir -p build/ci-test-logs
if [[ -d build/dev/Testing ]]; then
  cp -a build/dev/Testing build/ci-test-logs/dev
fi
sha256sum "${retained[@]/#/build/dev/}" > build/ci-test-logs/dev-retained.sha256
mkdir build/ci-retained-dev
for target in "${retained[@]}"; do
  mv -- "build/dev/$target" "build/ci-retained-dev/$target"
done
rm -rf -- build/dev
mv -- build/ci-retained-dev build/dev
sha256sum --check build/ci-test-logs/dev-retained.sha256
du -sh build/dev
df -h .
