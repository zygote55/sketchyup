#!/usr/bin/env bash
# Assemble release artifacts for the committed HEAD into OUTPUT_DIR:
#   sketchyup-VERSION.tar.gz + PKGBUILD   (scripts/package-source.sh)
#   THIRD_PARTY_NOTICES                   (every file CMake installs to share/licenses/sketchyup)
#   SHA256SUMS                            (archive, notices, and any matching .pkg.tar.zst)
#   MANIFEST.json                         (version, commit, digests, pinned build image, date)
# Packages are not built here. Build them separately in the pinned Arch container, as
# CI's "Arch package acceptance" job does, and copy them into OUTPUT_DIR first.
# Creates no tags or releases.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?Usage: scripts/release-artifacts.sh OUTPUT_DIR}
fail() { echo "release-artifacts: $*" >&2; exit 1; }

git -C "$root" rev-parse --verify HEAD > /dev/null 2>&1 || fail 'not a Git checkout'
[[ -z $(git -C "$root" status --porcelain --untracked-files=all) ]] ||
  fail 'the working tree is not clean; commit or stash changes first'
version=$("$root/scripts/release-version.sh")
pkgrel=$(sed -n 's/^pkgrel=\([0-9][0-9]*\)$/\1/p' "$root/packaging/arch/PKGBUILD")
[[ $pkgrel =~ ^[1-9][0-9]*$ ]] || fail 'cannot read an integer pkgrel from packaging/arch/PKGBUILD'
commit=$(git -C "$root" rev-parse HEAD)
image=$(awk '/^  package:/ { job = 1 } job && /^ *container:/ { print $2; exit }' \
  "$root/.github/workflows/native.yml")
[[ $image == *@sha256:* ]] || fail 'cannot find the package job pinned image digest in native.yml'

mkdir -p -- "$output"
output=$(realpath -- "$output")
archive=sketchyup-$version.tar.gz
# Packages already present must come from this version, pkgrel and source archive.
shopt -s nullglob
packages=()
for path in "$output"/*.pkg.tar.zst; do
  name=${path##*/}
  [[ $name == sketchyup-$version-$pkgrel-*.pkg.tar.zst || $name == sketchyup-debug-$version-$pkgrel-*.pkg.tar.zst ]] ||
    fail "$name is not a $version-$pkgrel package; use a fresh OUTPUT_DIR"
  packages+=("$name")
done
shopt -u nullglob
previous=
[[ -f $output/$archive ]] && previous=$(sha256sum "$output/$archive" | cut -d ' ' -f 1)

SOURCE_DATE_EPOCH=$(git -C "$root" log -1 --format=%ct) \
  "$root/scripts/package-source.sh" "$output" > /dev/null
archive_sha=$(sha256sum "$output/$archive" | cut -d ' ' -f 1)
if [[ -n $previous && $previous != "$archive_sha" ]]; then
  fail "existing $archive differs from HEAD's archive; packages there were built from other source"
fi
# Every archived file must be tracked at HEAD; ignored files must not leak into a release.
untracked=$(LC_ALL=C comm -23 \
  <(tar -tzf "$output/$archive" | grep -v '/$' | sed "s,^sketchyup-$version/,," | LC_ALL=C sort) \
  <(git -C "$root" ls-files | LC_ALL=C sort))
[[ -z $untracked ]] || fail "the archive would include untracked files: $(head -n 5 <<< "$untracked" | tr '\n' ' ')"

# Third-party notices: exactly the files CMake installs into share/licenses/sketchyup.
notices=$output/THIRD_PARTY_NOTICES
{
  printf 'SketchyUp %s third-party notices\n' "$version"
  printf 'Collected from the files installed to /usr/share/licenses/sketchyup.\n'
  sed -n 's,^install(FILES \([^ ]*\) DESTINATION share/licenses/sketchyup\( RENAME \([^ )]*\)\)\{0,1\})$,\1 \3,p' \
    "$root/CMakeLists.txt" |
    while read -r source installed; do
      [[ -f $root/$source ]] || fail "license file $source is missing"
      printf '\n==============================================================================\n'
      printf '%s (source: %s)\n' "${installed:-${source##*/}}" "$source"
      printf '==============================================================================\n\n'
      cat -- "$root/$source"
    done
} > "$notices"
grep -q '(source: LICENSE)' "$notices" || fail 'the SketchyUp LICENSE install rule was not found'

(cd -- "$output" && sha256sum -- "$archive" THIRD_PARTY_NOTICES "${packages[@]}") > "$output/SHA256SUMS"

date_epoch=${SOURCE_DATE_EPOCH:-$(date +%s)}
python3 -I - "$output" "$version" "$pkgrel" "$commit" "$image" "$date_epoch" "$archive" \
  "${packages[@]}" <<'PY'
import datetime, hashlib, json, sys
from pathlib import Path
output, version, pkgrel, commit, image, epoch, archive, *packages = sys.argv[1:]
root = Path(output)
def digest(name):
    return hashlib.sha256((root / name).read_bytes()).hexdigest()
manifest = {
    "name": "sketchyup",
    "version": version,
    "pkgver": version,
    "pkgrel": int(pkgrel),
    "gitCommit": commit,
    "date": datetime.datetime.fromtimestamp(int(epoch), datetime.timezone.utc)
            .strftime("%Y-%m-%dT%H:%M:%SZ"),
    "buildImage": image,
    "sourceArchive": {"file": archive, "sha256": digest(archive)},
    "packages": [{"file": name, "sha256": digest(name)} for name in packages],
    "thirdPartyNotices": {"file": "THIRD_PARTY_NOTICES", "sha256": digest("THIRD_PARTY_NOTICES")},
}
(root / "MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
PY
printf 'Release artifacts for %s (%s) written to %s\n' "$version" "$commit" "$output"
