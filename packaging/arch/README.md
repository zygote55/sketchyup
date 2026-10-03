# Arch development package

The app links the system Qt libraries dynamically. Own code is MIT; the static
Clipper2 library retains BSL-1.0. Both notices ship in `/usr/share/licenses/sketchyup`.
Build inputs are pinned by `Dockerfile.build` and the dated Arch archive mirror.
The resulting experimental package is not a stable-format or release promise.

On an Arch workstation with the documented build dependencies installed:

```sh
scripts/package-source.sh /absolute/path/to/package-output
cd /absolute/path/to/package-output
makepkg
sudo pacman -U sketchyup-0.1.0-2-x86_64.pkg.tar.zst
```

The source archive contains no Git credentials, build outputs or developer config.
`package-source.sh` writes the exact archive checksum into PKGBUILD. Build parallelism
defaults to four; override `CMAKE_BUILD_PARALLEL_LEVEL` if needed. The package check
phase runs all six noninteractive CTest targets. Runtime requires system Qt base,
Qt Wayland, the shared MIME database and DejaVu fonts (so a minimal install has
readable UI text). The desktop launcher accepts one local
file (`%f`); native file dialogs and CLI provide other entry points.

For repeatable acceptance without installing anything on the workstation:

```sh
docker build -f packaging/arch/Dockerfile.build -t sketchyup-build:20261002 .
scripts/package-source.sh "$PWD/build/package-acceptance"
cp packaging/arch/verify-package.sh build/package-acceptance/
docker run --name sketchyup-package-acceptance \
  -e SKETCHYUP_PACKAGE_SANDBOX=1 \
  -v "$PWD/build/package-acceptance:/work/package" \
  sketchyup-build:20261002 bash /work/package/verify-package.sh
docker cp sketchyup-package-acceptance:/work/acceptance build/package-evidence
docker rm sketchyup-package-acceptance
```

Use a fresh output directory/container name for each run. Only that dedicated build
output directory is mounted. The script refuses execution outside a Docker container
with its explicit test flag. It builds release 1 then release 2 from the same source
to exercise clean install and upgrade hooks; this is a package lifecycle test, not
proof of a historical format migration. It validates the installed desktop file,
MIME glob/magic, real desktop `%f` argument expansion (including spaces), renderer
startup, saved identity/revision, upgrade/reopen and uninstall. A PATH shim adds
capture/exit behavior to the installed app without changing its desktop entry.
The capture report must match the installed CLI's document metadata.

Qt's QSettings stores theme/recent-file preferences at
`$XDG_CONFIG_HOME/SketchyUp/SketchyUp.conf` (default
`~/.config/SketchyUp/SketchyUp.conf`). The app currently writes no automatic recovery,
data or cache files. Future app data/cache belong under Qt's standard XDG paths;
the package never manages files under user homes. Removal deletes only package-owned
binaries, launchers, MIME definition, icon and license notices. User models, backups,
settings, data and cache sentinels survive acceptance uninstall.
