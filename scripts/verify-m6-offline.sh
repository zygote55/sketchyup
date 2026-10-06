#!/usr/bin/env bash
# Deterministic M6 evidence. Synthetic models only; never reads provider credentials.
set -euo pipefail
if (( $# < 2 || $# > 3 )); then
  echo 'Usage: verify-m6-offline.sh BUILD_DIR NEW_EVIDENCE_DIR [ABSOLUTE_BLENDER]' >&2
  exit 2
fi
m6_repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
m6_build=$(realpath -- "$1")
m6_evidence=$(realpath -m -- "$2")
if (( $# == 3 )) && [[ "$3" != /* || ! -x "$3" ]]; then
  echo 'Supply an existing absolute Blender executable or omit the third argument.' >&2
  exit 2
fi
if [[ -n "${SKETCHYUP_M6_NATIVE_CONTAINER:-}" ]]; then
  command -v docker >/dev/null
  [[ "$m6_build" == "$m6_repo/"* ]]
else
  command -v ffmpeg >/dev/null
  command -v xvfb-run >/dev/null
fi
[[ ! -e "$m6_evidence" ]]
mkdir -m 700 -- "$m6_evidence"
cd -- "$m6_repo"
unset SKETCHYUP_BLENDER_TEST
if (( $# == 3 )); then export SKETCHYUP_BLENDER_TEST="$3"; fi
ctest --test-dir "$m6_build" --output-on-failure > "$m6_evidence/regression.log" 2>&1
"$m6_build/m6_workflow_tests" "$m6_evidence/fixtures" > "$m6_evidence/fixtures.log" 2>&1
for m6_recipe in roof stair table cabinet site; do
  "$m6_build/sketchyup-cli" --recipe "examples/$m6_recipe-recipe-v1.json" --new \
    --output "$m6_evidence/$m6_recipe.sketchyup" --outcomes "$m6_evidence/$m6_recipe-outcomes" \
    > "$m6_evidence/$m6_recipe-recipe.jsonl"
done
"$m6_build/sketchyup-cli" --input "$m6_evidence/fixtures/m6-joinery-after.sketchyup" \
  --export-glb "$m6_evidence/render-snapshot" --render-settings examples/render-settings-v1.json \
  > "$m6_evidence/export.json"
record_native() {
  local m6_stage=$1 m6_binary=$2
  if [[ -z "${SKETCHYUP_M6_NATIVE_CONTAINER:-}" ]]; then
    scripts/record-native-x11.sh "$m6_evidence/$m6_stage.mp4" "$m6_binary"
    return
  fi
  local m6_temporary m6_status=0
  m6_temporary=$(docker exec "$SKETCHYUP_M6_NATIVE_CONTAINER" mktemp -d /tmp/sketchyup-m6-XXXXXX)
  if [[ -n "${SKETCHYUP_RENDER_MODEL:-}" ]]; then
    docker cp "$SKETCHYUP_RENDER_MODEL" "$SKETCHYUP_M6_NATIVE_CONTAINER:$m6_temporary/model.sketchyup"
  fi
  docker exec \
    -e "SKETCHYUP_CAPTURE_DIR=$m6_temporary/native" \
    -e "SKETCHYUP_RENDER_MODEL=${SKETCHYUP_RENDER_MODEL:+$m6_temporary/model.sketchyup}" \
    -e "SKETCHYUP_RENDER_FOCUS=${SKETCHYUP_RENDER_FOCUS:-}" \
    -e "SKETCHYUP_RENDER_UI_EVIDENCE=$m6_temporary/render" \
    -e "SKETCHYUP_BLENDER_TEST=${SKETCHYUP_BLENDER_TEST:-}" \
    "$SKETCHYUP_M6_NATIVE_CONTAINER" /work/scripts/record-native-x11.sh \
    "$m6_temporary/$m6_stage.mp4" "/work/${m6_binary#"$m6_repo/"}" || m6_status=$?
  docker cp "$SKETCHYUP_M6_NATIVE_CONTAINER:$m6_temporary/." "$m6_evidence/"
  docker exec "$SKETCHYUP_M6_NATIVE_CONTAINER" rm -rf -- "$m6_temporary"
  return "$m6_status"
}
unset SKETCHYUP_RENDER_MODEL SKETCHYUP_RENDER_FOCUS
export SKETCHYUP_CAPTURE_DIR="$m6_evidence/native"
record_native native "$m6_build/m6_workflow_input_tests" > "$m6_evidence/native.log" 2>&1
export SKETCHYUP_RENDER_MODEL="$m6_evidence/fixtures/m6-joinery-after.sketchyup"
export SKETCHYUP_RENDER_FOCUS
SKETCHYUP_RENDER_FOCUS=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["site"])' "$m6_evidence/fixtures/measurements.json")
export SKETCHYUP_RENDER_UI_EVIDENCE="$m6_evidence/render"
record_native render "$m6_build/render_input_tests" > "$m6_evidence/render.log" 2>&1
python3 - "$m6_evidence" "$m6_repo" "${3:-}" <<'PY'
import datetime, hashlib, json, os, pathlib, subprocess, sys
root, repo = map(pathlib.Path, sys.argv[1:3])
measurement = json.loads((root/'fixtures/measurements.json').read_text())
assert measurement['materialsAndMappingsPreserved'] and measurement['invalidSolidRejected']
recipes = {}
for name in ('roof','stair','table','cabinet','site'):
    rows=[json.loads(line) for line in (root/f'{name}-recipe.jsonl').read_text().splitlines()]
    assert len(rows)==(11 if name=='site' else 8) and all(row['ok'] for row in rows)
    recipes[name]={'steps':len(rows),'passed':True}
render=json.loads((root/'render-manifest.json').read_text())
export=json.loads((root/'render-snapshot/manifest.json').read_text())
assert render['documentId']==export['documentId'] and render['revision']==export['revision']
def git(*args):
    proc=subprocess.run(['git','-C',str(repo),*args],capture_output=True,text=True)
    return proc.stdout.strip() if proc.returncode==0 else None
paths=[repo/'CMakeLists.txt']
for directory in ('src','tests','scripts'):
    paths.extend((repo/directory).rglob('*'))
report={
    'recordedAt':datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'sourceCommit':git('rev-parse','HEAD'),'sourceDirty':bool(git('status','--porcelain')),
    'sourceCodeSha256':{str(p.relative_to(repo)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths) if p.is_file()},
    'deterministicChecksPassed':True,'liveProviderTested':False,'m6GatePassed':False,
    'nativeContainer':os.environ.get('SKETCHYUP_M6_NATIVE_CONTAINER'),
    'recipes':recipes,'blenderExecutable':sys.argv[3] or None,'renderSourceMatchesJoinedStudy':True,
    'artifacts':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*')) if p.is_file()}
}
(root/'acceptance.json').write_text(json.dumps(report,indent=2)+'\n')
PY
printf 'Deterministic M6 evidence retained in %s; live-provider and CI acceptance are separate.\n' "$m6_evidence"
