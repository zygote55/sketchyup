# Arch development package

The app links the system Qt libraries dynamically. Own code is MIT; the static
Clipper2 retains BSL-1.0, Manifold Apache-2.0 and cgltf MIT. Third-party notices
ship in `/usr/share/licenses/sketchyup`.
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
defaults to one; override `CMAKE_BUILD_PARALLEL_LEVEL` if needed. The package check
phase runs all noninteractive CTest targets. Runtime requires system Qt base,
Qt Wayland, the shared MIME database and DejaVu fonts (so a minimal install has
readable UI text). The desktop launcher accepts one local
file (`%f`); native file dialogs and CLI provide other entry points.

PKGBUILD sets `SKETCHYUP_NATIVE_FIXTURES_IN_ALL=OFF` to avoid repeatedly linking
the opt-in native display fixtures which its CTest phase does not execute.
Every registered CTest executable and installed application/helper still builds.
The option defaults to `ON` for development and native CI; individual fixture
targets remain explicitly buildable in either configuration.

For repeatable acceptance without installing anything on the workstation:

```sh
scripts/package-source.sh "$PWD/build/package-acceptance"
cp packaging/arch/verify-package.sh build/package-acceptance/
SKETCHYUP_CHECK_PACKAGE_DIR="$PWD/build/package-acceptance" \
  scripts/run-container-check.sh package-acceptance sketchyup-build:20261002 \
  bash /work/package/verify-package.sh
docker cp sketchyup-check-package-acceptance:/work/acceptance build/package-evidence
```

Use an existing local image built from the pinned Dockerfile; the runner never
pulls an image implicitly. Use a fresh output directory/run name for each run.
The checkout is mounted read-only at `/source`; package work is writable at
`/work/package`. Logs, capture/config/data/cache and container `/tmp` use persistent
storage under the main checkout's `build/local-checks/RUN_NAME`. Host `/tmp` and
memory-backed filesystems are refused. The shared worktree lock and running-container
check permit one local acceptance job at a time.

The runner enforces one CPU (including affinity), 4 GiB RAM, no additional swap
and a 256-process limit. PKGBUILD also passes `-flto=1` at link time when LTO is
enabled: GCC's automatic worker count can still see the host's CPUs. It sets build
parallelism to one and requires at least 6 GiB available host memory before starting.
Interruptions stop the owned container when the shell can handle the signal; after
an abrupt harness/host failure, a still-running labeled container blocks another
job until it has been inspected and stopped. Containers never auto-restart. Logs,
resource configuration and final state remain available; run names cannot overwrite
prior attempts. These local limits are not reference-performance conditions.

The same runner can execute a native check with disk-backed scratch space:

```sh
scripts/run-container-check.sh viewport-check sketchyup-build:20261002 \
  bash -lc 'scripts/test-wayland.sh /source/build/dev/viewport_tests'
```

Do not reuse earlier containers that bind the host's `/tmp` to `/capture`.
The lifecycle harness refuses execution outside a Docker container with its
explicit test flag. It builds release 1 then release 2 from the same source
to exercise clean install and upgrade hooks; this is a package lifecycle test, not
proof of a historical format migration. It validates the installed desktop file,
MIME glob/magic, real desktop `%f` argument expansion (including spaces), renderer
startup, saved identity/revision, upgrade/reopen and uninstall. A PATH shim adds
capture/exit behavior to the installed app without changing its desktop entry.
The capture report must match the installed CLI's document metadata.

Qt's QSettings stores theme/recent-file preferences at
`$XDG_CONFIG_HOME/SketchyUp/SketchyUp.conf` (default
`~/.config/SketchyUp/SketchyUp.conf`). Recovery copies and other application data
use Qt's standard XDG paths;
the package never manages files under user homes. Removal deletes only package-owned
binaries, launchers, MIME definition, icon and license notices. User models, backups,
settings, data and cache sentinels survive acceptance uninstall.

The lifecycle harness retains theme, interface text size, reduced motion,
recovery interval and field-of-view preferences through upgrade, and checks
removal against the package manager's complete owned-file list. Its isolated
build defaults to one job; override `CMAKE_BUILD_PARALLEL_LEVEL` when needed.
Blender and Secret Service credential support are optional; core modeling and
file operations do not require either service.
