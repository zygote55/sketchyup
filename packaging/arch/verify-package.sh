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
for attempt in 1 2 3; do
  pacman -S --noconfirm --needed desktop-file-utils xdg-utils perl-file-mimeinfo jq ttf-dejavu && break
  (( attempt < 3 )) || exit 1
  sleep $((attempt * 30))
done
chown -R builder:builder /work/package
cp PKGBUILD PKGBUILD.current
trap 'if [[ -f PKGBUILD.current ]]; then mv PKGBUILD.current PKGBUILD; chown builder:builder PKGBUILD; fi' EXIT
# PKGBUILD is this script's one version source; package-source.sh already checked it
# against CMakeLists.txt, and the extracted source is checked again below.
package_version=$(sed -n 's/^pkgver=\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)$/\1/p' PKGBUILD)
package_release=$(sed -n 's/^pkgrel=\([0-9][0-9]*\)$/\1/p' PKGBUILD)
[[ -n $package_version && $package_release =~ ^[1-9][0-9]*$ ]] || {
  echo 'Package acceptance requires an x.y.z pkgver and an integer pkgrel.' >&2
  exit 1
}
# Both packages build from the same source. The prior package is the previous release
# in this series: a later pkgrel upgrades from the previous pkgrel of the same version;
# the first pkgrel of a patch version x.y.z (z >= 1) upgrades from x.y.(z-1)-1; and
# 1.0.0-1 upgrades from the last development package, 0.1.0-3. The previous release of
# any other x.y.0 is not implied by its version, so this script refuses to guess it.
IFS=. read -r version_major version_minor version_patch <<< "$package_version"
if (( package_release >= 2 )); then
  prior_version=$package_version prior_release=$((package_release - 1))
elif (( version_patch >= 1 )); then
  prior_version=$version_major.$version_minor.$((version_patch - 1)) prior_release=1
elif [[ $package_version == 1.0.0 ]]; then
  prior_version=0.1.0 prior_release=3
else
  echo "No prior release is defined for $package_version-$package_release; extend verify-package.sh." >&2
  exit 1
fi
(( $(vercmp "$package_version-$package_release" "$prior_version-$prior_release") > 0 )) || {
  echo "Package $package_version-$package_release is not newer than $prior_version-$prior_release." >&2
  exit 1
}
source_dir=/work/package/src/sketchyup-$package_version
sed -i -e "s/^pkgver=$package_version$/pkgver=$prior_version/" \
  -e "s/^pkgrel=$package_release$/pkgrel=$prior_release/" \
  -e "s/^_srcname=.*$/_srcname=sketchyup-$package_version/" PKGBUILD
runuser -u builder -- env CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-1}" makepkg --noconfirm --force
grep -qx "project(SketchyUp VERSION $package_version LANGUAGES CXX)" "$source_dir/CMakeLists.txt" || {
  echo "CMakeLists.txt in the source archive does not declare version $package_version." >&2
  exit 1
}
pacman -U --noconfirm "sketchyup-$prior_version-$prior_release-x86_64.pkg.tar.zst"
[[ $(pacman -Q sketchyup) == "sketchyup $prior_version-$prior_release" ]]
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
printf '[General]\npackageSentinel=retain\ntheme=1\ninterfaceTextPercent=150\nreducedMotion=true\nrecoverySeconds=0\nfieldOfView=67\n' > "$XDG_CONFIG_HOME/SketchyUp/SketchyUp.conf"
printf 'retain-data\n' > "$XDG_DATA_HOME/SketchyUp/sentinel"
printf 'retain-cache\n' > "$XDG_CACHE_HOME/SketchyUp/sentinel"
sketchyup-cli --script "$SKETCHYUP_SOURCE_DIR/examples/room-shell.json" --output '/work/acceptance/Room model.sketchyup' > /work/acceptance/created.json
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
runuser -u builder -- env SKETCHYUP_SOURCE_DIR="$source_dir" xvfb-run -a /work/acceptance/launch.sh
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
cp /work/acceptance/config/SketchyUp/SketchyUp.conf /work/acceptance/preferences-before-upgrade.conf
runuser -u builder -- env CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-1}" makepkg --noconfirm --force --nocheck
pacman -U --noconfirm "sketchyup-$package_version-$package_release-x86_64.pkg.tar.zst"
pacman -Qkk sketchyup
[[ $(pacman -Q sketchyup) == "sketchyup $package_version-$package_release" ]]
[[ $(runuser -u builder -- sketchyup-cli --version) == "sketchyup-cli $package_version" ]]
runuser -u builder -- sketchyup-cli --input '/work/acceptance/Room model.sketchyup' > /work/acceptance/reopened.json
cmp /work/acceptance/expected.json /work/acceptance/reopened.json
runuser -u builder -- python "$source_dir/scripts/verify-installed-migrations.py" \
  --cli /usr/bin/sketchyup-cli \
  --fixtures "$source_dir/tests/fixtures" \
  --output /work/acceptance/migrations
runuser -u builder -- env XDG_CONFIG_HOME=/work/acceptance/config \
  XDG_DATA_HOME=/work/acceptance/data XDG_CACHE_HOME=/work/acceptance/cache \
  QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a timeout 40s sketchyup --smoke > /work/acceptance/upgraded-smoke.json
python - <<'PYPREFS'
import configparser
before, after = configparser.ConfigParser(), configparser.ConfigParser()
before.read('/work/acceptance/preferences-before-upgrade.conf')
after.read('/work/acceptance/config/SketchyUp/SketchyUp.conf')
for key in ('packageSentinel', 'theme', 'interfaceTextPercent', 'reducedMotion', 'recoverySeconds', 'fieldOfView'):
    if before['General'][key] != after['General'][key]:
        raise SystemExit('Upgrade changed preference: ' + key)
PYPREFS
pacman -Qlq sketchyup > /work/acceptance/package-owned-paths.txt
pacman -R --noconfirm sketchyup
while IFS= read -r path; do
  [[ $path == */ ]] || [[ ! -e $path && ! -L $path ]]
done < /work/acceptance/package-owned-paths.txt
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
[[ -s /work/acceptance/migrations/m4-complete-v11.sketchyup/migrated.sketchyup ]]
jq -e '.passed and (.fixtures | length >= 20)' /work/acceptance/migrations/report.json
grep -q packageSentinel=retain /work/acceptance/config/SketchyUp/SketchyUp.conf
grep -q retain-data /work/acceptance/data/SketchyUp/sentinel
grep -q retain-cache /work/acceptance/cache/SketchyUp/sentinel
printf 'Install, desktop launch, MIME, upgrade, reopen and removal acceptance passed.\n'

[[ ! -e /usr/share/doc/sketchyup/examples/room-window-resize-recipe-v1.json ]]

[[ ! -e /usr/share/doc/sketchyup/examples/render-settings-v1.json ]]
