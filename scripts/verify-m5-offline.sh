#!/usr/bin/env bash
# Deterministic M5 evidence. Never reads real credentials or calls a provider.
set -euo pipefail
if (( $# < 2 || $# > 3 )); then
  echo 'Usage: verify-m5-offline.sh BUILD_DIR NEW_EVIDENCE_DIR [ABSOLUTE_BLENDER]' >&2
  exit 2
fi
m5_repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
m5_build=$(realpath -- "$1")
m5_evidence=$(realpath -m -- "$2")
if (( $# == 3 )) && [[ "$3" != /* || ! -x "$3" ]]; then
  echo 'Supply an existing absolute Blender executable or omit the third argument.' >&2
  exit 2
fi
command -v ffmpeg >/dev/null
command -v xvfb-run >/dev/null
[[ ! -e "$m5_evidence" ]]
mkdir -m 700 -- "$m5_evidence"
cd -- "$m5_repo"
for m5_suite in assistant native_assistant_session openai_provider ollama_provider \
  transaction_coordinator transaction_dispatch automation_session recipe mcp persistence blender_job glb_export; do
  "$m5_build/${m5_suite}_tests" > "$m5_evidence/$m5_suite.log" 2>&1
done
"$m5_build/model_recipes_tests" "$m5_evidence/fixtures" > "$m5_evidence/fixtures.log" 2>&1
"$m5_build/sketchyup-cli" --recipe examples/room-window-resize-recipe-v1.json --new \
  --output "$m5_evidence/cli-room.sketchyup" --outcomes "$m5_evidence/cli-outcomes" \
  > "$m5_evidence/cli-recipe.json"
"$m5_build/sketchyup-cli" --input "$m5_evidence/fixtures/room-after.sketchyup" \
  --export-glb "$m5_evidence/render-snapshot" --render-settings examples/render-settings-v1.json \
  > "$m5_evidence/export.json"
# Native interactions use disposable preferences and credential/transport helpers.
scripts/record-native-x11.sh "$m5_evidence/manual.mp4" "$m5_build/m4_workflow_tests" --capture "$m5_evidence/manual.png" \
  > "$m5_evidence/manual.log" 2>&1
scripts/record-native-x11.sh "$m5_evidence/assistant-fixture.mp4" \
  "$m5_build/assistant_panel_tests" "$m5_evidence/assistant-images" \
  > "$m5_evidence/assistant.log" 2>&1
export SKETCHYUP_RENDER_MODEL="$m5_evidence/fixtures/room-after.sketchyup"
unset SKETCHYUP_BLENDER_TEST
export SKETCHYUP_RENDER_UI_EVIDENCE="$m5_evidence/render"
if (( $# == 3 )); then
  export SKETCHYUP_BLENDER_TEST="$3"
fi
scripts/record-native-x11.sh "$m5_evidence/render.mp4" "$m5_build/render_input_tests" \
  > "$m5_evidence/render.log" 2>&1
python3 - "$m5_evidence" "$m5_repo" "${3:-}" <<'PY'
import datetime, hashlib, json, pathlib, subprocess, sys
root = pathlib.Path(sys.argv[1])
measurements = json.loads((root / "fixtures/measurements.json").read_text())
render = json.loads((root / "render-manifest.json").read_text())
assert render["documentId"] == measurements["documentId"] and render["revision"] == "2"

repo = pathlib.Path(sys.argv[2])
def git(*args):
    try:
        proc = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True)
        return proc.stdout.strip() if proc.returncode == 0 else None
    except FileNotFoundError:
        return None
commit, dirty = git("rev-parse", "HEAD"), git("status", "--porcelain")
code_paths = [repo / "CMakeLists.txt"]
for folder in ("src", "tests", "scripts"):
    code_paths.extend((repo / folder).rglob("*"))
result = {
    "recordedAt": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "sourceCommit": commit,
    "sourceDirty": bool(dirty) if dirty is not None else None,
    "sourceCodeSha256": {str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in sorted(code_paths) if p.is_file()},
    "deterministicChecksPassed": True,
    "liveProviderTested": False,
    "m5GatePassed": False,
    "blenderExecutable": sys.argv[3] or None,
    "renderSourceMatchesWidenedFixture": True,
    "artifacts": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in sorted(root.rglob("*")) if p.is_file()},
}
(root / "acceptance.json").write_text(json.dumps(result, indent=2) + "\n")
PY
printf 'Deterministic M5 evidence retained in %s; live-provider acceptance remains separate.\n' "$m5_evidence"
