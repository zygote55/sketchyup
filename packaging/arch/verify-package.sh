#!/usr/bin/env bash
# Run only inside the disposable pinned Arch build container, at /work/package.
set -euo pipefail
[[ -f /.dockerenv && ${SKETCHYUP_PACKAGE_SANDBOX:-} == 1 && $EUID == 0 ]] || {
  echo 'This acceptance script requires the disposable package container.' >&2
  exit 1
}
cd /work/package
# The minimal Arch image excludes documentation. Include it in this disposable
# acceptance environment so the installed API contracts are actually verified.
sed -i '/^[[:space:]]*NoExtract[[:space:]]*=/s@usr/share/doc/\*@@g' /etc/pacman.conf
pacman -S --noconfirm --needed desktop-file-utils xdg-utils perl-file-mimeinfo jq ttf-dejavu
chown -R builder:builder /work/package
cp PKGBUILD PKGBUILD.current
sed -i 's/^pkgrel=2$/pkgrel=1/' PKGBUILD
runuser -u builder -- env CMAKE_BUILD_PARALLEL_LEVEL=4 makepkg --noconfirm --force
pacman -U --noconfirm sketchyup-0.1.0-1-x86_64.pkg.tar.zst
pacman -Qkk sketchyup
test -s /usr/share/doc/sketchyup/decisions/0029-native-mcp.md
QT_QPA_PLATFORM=offscreen sketchyup --mcp-inspection-capabilities | jq -S . > /work/package/mcp-native-installed.json
jq -S . /usr/share/doc/sketchyup/api/mcp-native-2026-07-28.json > /work/package/mcp-native-schema.json
cmp /work/package/mcp-native-installed.json /work/package/mcp-native-schema.json
test -s /usr/share/doc/sketchyup/decisions/0028-local-mcp.md
sketchyup-cli --mcp-capabilities | jq -S . > /work/package/mcp-installed.json
jq -S . /usr/share/doc/sketchyup/api/mcp-2026-07-28.json > /work/package/mcp-schema.json
cmp /work/package/mcp-installed.json /work/package/mcp-schema.json
test -s /usr/share/doc/sketchyup/decisions/0027-transaction-recipes.md
sketchyup-cli --recipe-capabilities | jq -S . > /work/package/recipe-installed.json
jq -S . /usr/share/doc/sketchyup/api/recipe-v1.json > /work/package/recipe-schema.json
cmp /work/package/recipe-installed.json /work/package/recipe-schema.json
test -s /usr/share/doc/sketchyup/decisions/0026-headless-session.md
sketchyup-cli --session-capabilities | jq -S . > /work/package/headless-installed.json
jq -S . /usr/share/doc/sketchyup/api/headless-session-v1.json > /work/package/headless-schema.json
cmp /work/package/headless-installed.json /work/package/headless-schema.json
test -s /usr/share/doc/sketchyup/api/transactions-v1.json
test -s /usr/share/doc/sketchyup/decisions/0025-transaction-dispatch.md
sketchyup-cli --capabilities | jq -S '.transactions' > /work/package/transactions-installed.json
jq -S . /usr/share/doc/sketchyup/api/transactions-v1.json > /work/package/transactions-schema.json
cmp /work/package/transactions-installed.json /work/package/transactions-schema.json
test -s /usr/share/doc/sketchyup/api/inspection-v1.json
test -s /usr/share/doc/sketchyup/api/inspection-session-v1.json
test -s /usr/share/doc/sketchyup/decisions/0019-bounded-inspection.md
sketchyup-cli --capabilities | jq -S '.inspection' > /work/package/inspection-installed.json
jq -S . /usr/share/doc/sketchyup/api/inspection-v1.json > /work/package/inspection-schema.json
cmp /work/package/inspection-installed.json /work/package/inspection-schema.json
sketchyup-cli --capabilities | jq -S '.inspectionSession' > /work/package/session-installed.json
jq -S . /usr/share/doc/sketchyup/api/inspection-session-v1.json > /work/package/session-schema.json
cmp /work/package/session-installed.json /work/package/session-schema.json
test -s /usr/share/doc/sketchyup/decisions/0021-desktop-inspection.md
QT_QPA_PLATFORM=offscreen sketchyup --inspection-capabilities | jq -S . > /work/package/desktop-inspection-installed.json
jq -S . /usr/share/doc/sketchyup/api/inspection-desktop-v1.json > /work/package/desktop-inspection-schema.json
cmp /work/package/desktop-inspection-installed.json /work/package/desktop-inspection-schema.json
desktop-file-validate /usr/share/applications/io.sketchyup.SketchyUp.desktop
update-mime-database /usr/share/mime
install -d -o builder -g builder /work/acceptance
cat > /work/acceptance/launch.sh <<'LAUNCH'
#!/usr/bin/env bash
set -euo pipefail
export PATH=/usr/bin/vendor_perl:$PATH
export XDG_CONFIG_HOME=/work/acceptance/config
export XDG_DATA_HOME=/work/acceptance/data
export XDG_CACHE_HOME=/work/acceptance/cache
export XDG_RUNTIME_DIR=/work/acceptance/runtime
mkdir -p "$XDG_CONFIG_HOME/SketchyUp" "$XDG_DATA_HOME/SketchyUp" "$XDG_CACHE_HOME/SketchyUp" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
printf '[General]\npackageSentinel=retain\n' > "$XDG_CONFIG_HOME/SketchyUp/SketchyUp.conf"
printf 'retain-data\n' > "$XDG_DATA_HOME/SketchyUp/sentinel"
printf 'retain-cache\n' > "$XDG_CACHE_HOME/SketchyUp/sentinel"
sketchyup-cli --script /work/package/src/sketchyup-0.1.0/examples/room-shell.json --output '/work/acceptance/Room model.sketchyup' > /work/acceptance/created.json
sketchyup-cli --input '/work/acceptance/Room model.sketchyup' > /work/acceptance/expected.json
[[ $(xdg-mime query filetype '/work/acceptance/Room model.sketchyup') == application/x-sketchyup ]]
cp '/work/acceptance/Room model.sketchyup' /work/acceptance/no-extension
[[ $(xdg-mime query filetype /work/acceptance/no-extension) == application/x-sketchyup ]]
xdg-mime default io.sketchyup.SketchyUp.desktop application/x-sketchyup
[[ $(xdg-mime query default application/x-sketchyup) == io.sketchyup.SketchyUp.desktop ]]
mkdir -p /work/acceptance/bin
# Preserve the installed desktop entry and its %f expansion. The test PATH shim
# adds only capture/exit behavior to the actual installed application.
cat > /work/acceptance/bin/sketchyup <<'WRAPPER'
#!/usr/bin/env bash
printf '%s\n' "$@" > /work/acceptance/launcher-args.txt
exec /usr/bin/sketchyup "$@" --capture /work/acceptance/launcher.png > /work/acceptance/launcher.json 2> /work/acceptance/launcher.stderr
WRAPPER
chmod +x /work/acceptance/bin/sketchyup
export PATH=/work/acceptance/bin:$PATH
export QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1
: > /work/acceptance/launcher.json
gio launch /usr/share/applications/io.sketchyup.SketchyUp.desktop '/work/acceptance/Room model.sketchyup'
for attempt in {1..300}; do
  [[ -s /work/acceptance/launcher.json ]] && break
  sleep .1
done
[[ $(cat /work/acceptance/launcher-args.txt) == '/work/acceptance/Room model.sketchyup' ]]
[[ -s /work/acceptance/launcher.png ]]
jq -e --slurpfile expected /work/acceptance/expected.json \
  '.passed and .documentId == $expected[0].documentId and .revision == $expected[0].revision and .bodies == ($expected[0].bodies | length)' \
  /work/acceptance/launcher.json
LAUNCH
chmod +x /work/acceptance/launch.sh
runuser -u builder -- xvfb-run -a /work/acceptance/launch.sh
runuser -u builder -- env -u DISPLAY -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM \
  sketchyup-cli --recipe /usr/share/doc/sketchyup/examples/transaction-face-recipe.json \
  --new --output /work/acceptance/recipe.sketchyup --outcomes /work/acceptance/recipe-outcomes \
  > /work/acceptance/recipe.jsonl
jq -e 'select(.id == "measurement") | .ok and (.result.data.area == 6)' /work/acceptance/recipe.jsonl
runuser -u builder -- env -u DISPLAY -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM \
  sketchyup-cli --recipe /usr/share/doc/sketchyup/examples/room-window-resize-recipe-v1.json \
  --new --output /work/acceptance/recipe-room.sketchyup --outcomes /work/acceptance/recipe-room-outcomes \
  > /work/acceptance/recipe-room.jsonl
jq -e 'select(.id == "resize_draw") | .ok and (.result.createdIds.recipeOperations[0].outerWidth == 1.4) and .result.createdIds.recipeOperations[0].madeUnique' /work/acceptance/recipe-room.jsonl
runuser -u builder -- env -u DISPLAY -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM \
  sketchyup-cli --input /work/acceptance/recipe-room.sketchyup \
  --export-glb /work/acceptance/glb \
  --render-settings /usr/share/doc/sketchyup/examples/render-settings-v1.json \
  > /work/acceptance/glb.json
python - <<'PYGLB'
import hashlib, json
from pathlib import Path
root = Path('/work/acceptance/glb')
manifest = json.loads((root / 'manifest.json').read_text())
assert manifest['revision'] == '2' and manifest['units'] == 'm'
assert hashlib.sha256((root / 'scene.glb').read_bytes()).hexdigest() == manifest['scene']['sha256']
PYGLB
mv PKGBUILD.current PKGBUILD
chown builder:builder PKGBUILD
runuser -u builder -- env CMAKE_BUILD_PARALLEL_LEVEL=4 makepkg --noconfirm --force --nocheck
pacman -U --noconfirm sketchyup-0.1.0-2-x86_64.pkg.tar.zst
pacman -Qkk sketchyup
runuser -u builder -- sketchyup-cli --input '/work/acceptance/Room model.sketchyup' > /work/acceptance/reopened.json
cmp /work/acceptance/expected.json /work/acceptance/reopened.json
runuser -u builder -- env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a timeout 40s sketchyup --smoke > /work/acceptance/upgraded-smoke.json
pacman -R --noconfirm sketchyup
[[ ! -e /usr/bin/sketchyup && ! -e /usr/bin/sketchyup-cli ]]
[[ ! -e /usr/share/doc/sketchyup/api/mcp-native-2026-07-28.json ]]
[[ ! -e /usr/share/doc/sketchyup/decisions/0029-native-mcp.md ]]
[[ ! -e /usr/share/doc/sketchyup/api/mcp-2026-07-28.json ]]
[[ ! -e /usr/share/doc/sketchyup/decisions/0028-local-mcp.md ]]
[[ ! -e /usr/share/doc/sketchyup/api/recipe-v1.json ]]
[[ ! -e /usr/share/doc/sketchyup/decisions/0027-transaction-recipes.md ]]
[[ ! -e /usr/share/doc/sketchyup/examples/transaction-face-recipe.json ]]
[[ ! -e /usr/share/doc/sketchyup/api/headless-session-v1.json ]]
[[ ! -e /usr/share/doc/sketchyup/decisions/0026-headless-session.md ]]
[[ ! -e /usr/share/doc/sketchyup/api/transactions-v1.json ]]
[[ ! -e /usr/share/doc/sketchyup/decisions/0025-transaction-dispatch.md ]]
[[ ! -e /usr/share/doc/sketchyup/api/inspection-v1.json ]]
[[ ! -e /usr/share/doc/sketchyup/api/inspection-desktop-v1.json ]]
[[ ! -e /usr/share/doc/sketchyup/api/inspection-session-v1.json ]]
[[ ! -e /usr/share/applications/io.sketchyup.SketchyUp.desktop ]]
[[ ! -e /usr/share/icons/hicolor/scalable/apps/io.sketchyup.SketchyUp.svg ]]
[[ ! -e /usr/share/mime/packages/io.sketchyup.SketchyUp.xml ]]
[[ -s '/work/acceptance/Room model.sketchyup' ]]
grep -q packageSentinel=retain /work/acceptance/config/SketchyUp/SketchyUp.conf
grep -q retain-data /work/acceptance/data/SketchyUp/sentinel
grep -q retain-cache /work/acceptance/cache/SketchyUp/sentinel
printf 'Install, desktop launch, MIME, upgrade, reopen and removal acceptance passed.\n'

[[ ! -e /usr/share/doc/sketchyup/examples/room-window-resize-recipe-v1.json ]]

[[ ! -e /usr/share/doc/sketchyup/examples/render-settings-v1.json ]]
