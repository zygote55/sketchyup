#!/usr/bin/env bash
# Blender interoperability checks for CI. Generates the GLB fixtures, validates
# them with the Khronos glTF validator, then runs the independent Blender
# verifications concurrently. Each verification writes to its own output
# folder, so they do not interfere.
# Usage: scripts/ci-blender-interop.sh [JOBS]   (from the repository root,
# after building glb/texture/section/smooth_export_tests in build/dev)
set -euo pipefail
jobs=${1:-4}
logs=build/blender-logs
mkdir -p build/gltf-validator "$logs"
cp tests/gltf-validator/package*.json build/gltf-validator/
npm ci --prefix build/gltf-validator --ignore-scripts --no-audit --no-fund

build/dev/glb_export_tests build/glb-fixtures
node scripts/verify-glb.mjs build/gltf-validator build/glb-fixtures > build/glb-validation.json
build/dev/texture_export_tests build/texture-fixtures
node scripts/verify-glb.mjs build/gltf-validator build/texture-fixtures 12 > build/texture-glb-validation.json
SKETCHYUP_SECTION_EXPORT_EVIDENCE="$PWD/build/section-fixtures" build/dev/section_export_tests
node scripts/verify-glb.mjs build/gltf-validator build/section-fixtures 2 > build/section-glb-validation.json
build/dev/smooth_export_tests build/smooth-fixtures
node scripts/verify-glb.mjs build/gltf-validator build/smooth-fixtures 4 > build/smooth-glb-validation.json

# Longest first, so the slow texture checks start immediately.
checks=(
  "textures build/texture-fixtures"
  "textures build/texture-fixtures eevee"
  "solar build/solar-oracle"
  "solar build/solar-oracle-eevee eevee"
  "environment build/environment-oracle"
  "environment build/environment-oracle-eevee eevee"
  "smooth build/smooth-fixtures"
  "smooth build/smooth-fixtures eevee"
  "sections build/section-fixtures"
  "sections build/section-fixtures eevee"
  "glb build/glb-fixtures"
  "sides build/glb-fixtures"
)
run_check() {
  local name=$1 dir=$2 engine=${3:-}
  local log="$logs/${name}${engine:+-$engine}.log" start=$SECONDS
  if blender --background --factory-startup --disable-autoexec --python-exit-code 1 \
       --python "scripts/verify-blender-$name.py" -- "$dir" ${engine:+"$engine"} > "$log" 2>&1; then
    echo "passed: $name${engine:+ ($engine)} in $((SECONDS - start)) s"
  else
    echo "FAILED: $name${engine:+ ($engine)}; last lines of $log:"
    tail -40 "$log"
    return 1
  fi
}
export -f run_check
export logs
printf '%s\n' "${checks[@]}" | xargs -P "$jobs" -L 1 bash -c 'run_check "$@"' _
