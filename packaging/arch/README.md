# Arch package

The app links the system Qt libraries dynamically. Own code is MIT; the static
Clipper2 retains BSL-1.0, Manifold Apache-2.0, cgltf MIT and the header-only CDT
triangulation library MPL-2.0 (with its BSD-3-Clause exact predicates). Third-party notices
ship in `/usr/share/licenses/sketchyup`.
Build inputs are pinned by `Dockerfile.build` and the dated Arch archive mirror.
The PKGBUILD builds two packages from one source, `sketchyup` and the optional `sketchyup-ifc`
(see [IFC helper](#ifc-helper-package-sketchyup-ifc)); the application package carries none of the
LGPL libraries of the second.
The 1.0 native format and versioning rules are in [ADR 0108](../../docs/decisions/0108-public-native-format.md)
and [ADR 0162](../../docs/decisions/0162-release-versioning.md).

On an Arch workstation with the documented build dependencies installed (`makepkg -s` installs
them; for the IFC helper they are `boost`, `eigen`, `opencascade` and `patchelf`):

```sh
scripts/package-source.sh /absolute/path/to/package-output
cd /absolute/path/to/package-output
makepkg
sudo pacman -U sketchyup-1.3.0-1-x86_64.pkg.tar.zst
sudo pacman -U sketchyup-ifc-1.3.0-1-x86_64.pkg.tar.zst   # optional: IFC helper
```

The version has one source: `project(SketchyUp VERSION ...)` in `CMakeLists.txt`
([ADR 0162](../../docs/decisions/0162-release-versioning.md)). `pkgver` must match it;
`scripts/release-version.sh` prints the version and fails if they differ, and
`package-source.sh` runs it first. The source archive is deterministic for a commit:
file order, owners and times (the commit time, or `SOURCE_DATE_EPOCH`) are fixed and
gzip stores no timestamp.

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

Use an existing local image built from the pinned Dockerfile (`Dockerfile.build` installs the
IFC helper's build packages `boost`, `eigen`, `opencascade` and `patchelf` since R097.a, so an image
built before that must be rebuilt under a new tag); the runner never
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
explicit test flag. It builds the preceding package and then the current release from
the same source to exercise clean install and upgrade hooks. The preceding package is
the previous release in the series: `pkgrel - 1` of the same version; for the first
`pkgrel` of a patch version, the previous patch at `pkgrel` 1 (`1.0.1-1` upgrades from
`1.0.0-1`); and for the first `pkgrel` of `x.y.0`, the newest earlier entry of the
script's `released_packages` table (`1.3.0-1` upgrades from `1.2.0-1`, `1.2.0-1` from
`1.1.1-1`, `1.1.0-1` from
`1.0.1-1`, `1.0.0-1` from
the development package `0.1.0-3`). `vercmp` must order the table and each pair. A new
minor version whose previous series is missing from the table stops with an error, so
add each published release to it. After the upgrade it
checks `pacman -Q` and `sketchyup-cli --version`. Both packages contain the current
source, so this is a package lifecycle test, not proof of a historical format migration. It validates the installed desktop file,
MIME glob/magic, real desktop `%f` argument expansion (including spaces), renderer
startup, saved identity/revision, upgrade/reopen and uninstall. A PATH shim adds
capture/exit behavior to the installed app without changing its desktop entry.
The capture report must match the installed CLI's document metadata.

With `SKETCHYUP_PRIOR_RELEASE_PACKAGE` set to a published release package, the script
also checks the format downgrade window ([ADR 0163](../../docs/decisions/0163-documentation-sheets.md)).
It verifies the package's SHA-256 (pinned in the script per file name, or
`SKETCHYUP_PRIOR_RELEASE_SHA256`), unpacks it privately without installing it, and
passes its CLI to `check()` as `SKETCHYUP_PRIOR_CLI`. After installation, the released
CLI must refuse every retained fixture newer than the schemas it reads and leave it
unchanged, and must accept a sheet-free document saved by the new package. CI downloads
the latest release, `sketchyup-1.2.0-1-x86_64.pkg.tar.zst` from the v1.2.0 release.
The 1.2.0-1 CLI reads schema 28, and no retained fixture is newer, so for 1.3.0 the
schema-29 (terrain) refusal is checked in `check()`: `terrain_io` passes only if the
released CLI refuses a schema-29 file and leaves it unchanged, and `component_parameter_io`
requires it to open schema-28 files unchanged.

After upgrading, the installed CLI migrates the retained historical native fixtures
to new files. `scripts/verify-installed-migrations.py` checks source hashes, identity,
revision, geometry/resource inspection, and byte equality with the ordinary load/save
path. Existing destinations and writable in-place sources must survive rejected
migrations; truncated inputs must fail without publishing a file. The report and
all resulting documents remain in the acceptance directory after uninstall. This
exercises historical document formats, not installation of an older application
package. The script requires an explicit CLI path, fixture directory and new output
directory, so it can also verify an already installed package without rebuilding.

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

## IFC helper package (`sketchyup-ifc`)

[ADR 0171](../../docs/decisions/0171-ifc-exchange-subset.md) decision 22: the PKGBUILD is a split
package (`pkgbase=sketchyup`, `pkgname=(sketchyup sketchyup-ifc)`). `sketchyup` is installed with
`cmake --install --component Unspecified` and `sketchyup-ifc` with `--component ifc`, so the default
install stays small and the LGPL and OpenCASCADE obligations attach only to the second package.

What `sketchyup-ifc` contains, all in the flat private directory `/usr/lib/sketchyup` (no new
`PATH` or `ld.so` entry):

- `sketchyup-ifc-worker`, the one executable that links IfcOpenShell (decision 19), with a
  `RUNPATH` of `$ORIGIN`;
- the IfcOpenShell 0.9.0 libraries built from `third_party/ifcopenshell` (`libifcopenshell.parse`,
  `.plugin`, `.geometry`, the three schema libraries, the three geometry mapping plugins and the
  OpenCASCADE and pass-through kernels), also `RUNPATH $ORIGIN`;
- a private copy of the OpenCASCADE libraries the kernel plugin records (the `libTK*` closure; decision
  18), copied from Arch's `opencascade` package at package-build time and given `RUNPATH $ORIGIN`
  with `patchelf` (a library finds its dependencies through its own `RUNPATH`, not the executable's),
  so users need not install `opencascade`, which pulls in VTK, Tcl and Tk (about 0.55 GiB);
- `share/licenses/sketchyup/IfcOpenShell-COPYING.LESSER`, `IfcOpenShell-COPYING`,
  `OpenCASCADE-LICENSE` and `OpenCASCADE-LGPL-EXCEPTION`, and `share/doc/sketchyup/IFC-SOURCE.txt`,
  the written offer of the corresponding source and the instructions for replacing the libraries.

It depends on `sketchyup`, `gcc-libs` and `glibc` (the libraries record no Boost library when linked
with `--as-needed`). `sketchyup` lists it as an optional dependency. The package's `license` array is
`MIT LGPL-3.0-or-later LGPL-2.1-or-later LicenseRef-OCCT-LGPL-exception`; the application package's
stays as before. The `LicenseRef` is for the Open CASCADE exception, which Arch's own package also
names `LicenseRef-LGPL-exception`.

The IfcOpenShell build is a separate CMake project inside this build
(`src/ifc/IfcOpenShellPrefix.cmake`): about 360 compile steps and 0.9 GB per compiler process, so
it takes 13 minutes with four jobs on four CPUs and about an hour with one. The PKGBUILD gives it
`CMAKE_BUILD_PARALLEL_LEVEL`, else the CPU count, at most four, whatever the main build uses.
`SKETCHYUP_BUILD_IFC` is `OFF` by default for developers; the PKGBUILD turns it on.
`SKETCHYUP_PACKAGE_WITHOUT_IFC=1` in the environment makes `makepkg` skip it and build only
`sketchyup`; package acceptance sets it for the preceding package, which exists only to test install
and upgrade, so the IfcOpenShell build is paid once per acceptance run, not twice.

Acceptance (`verify-package.sh`) installs `sketchyup-ifc` after the upgrade and checks, with
`scripts/verify-ifc-library.py package`, the installed files against checks L1 to L6 of ADR 0171
section 6.3: no other executable links IfcOpenShell or OpenCASCADE; `RUNPATH`s and a replacement
directory found through `LD_LIBRARY_PATH`; the notices and the source offer; no GPL-3.0-or-later
library; the OpenCASCADE closure installed exactly, and `--probe` reporting `kernel unavailable`
(without aborting) when the kernel plugin or any one of those libraries is removed. It then runs
the API probe `tests/ifc_worker_probe_tests.py` against the installed helper, records the package
sizes in `/work/acceptance/package-sizes.txt`, and removes `sketchyup-ifc` before `sketchyup`.

### Replacing IfcOpenShell

LGPL-3.0 section 4 and LGPL-2.1 section 6 entitle you to run the application with a modified
version of these libraries. The helper finds every library through its `RUNPATH`, a `RUNPATH` and not
an `RPATH`, so either way below works with no relinking:

```sh
# Build the same tag with CMake (the options are in src/ifc/IfcOpenShellPrefix.cmake)
cmake -S third_party/ifcopenshell/cmake -B build-ifcopenshell -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSCHEMA_VERSIONS='2x3;4;4x3_add2' -DWITH_OPENCASCADE=ON -DWITH_CGAL=OFF \
  -DOCC_INCLUDE_DIR=/usr/include/opencascade -DOCC_LIBRARY_DIR=/usr/lib -DEIGEN_DIR=/usr/include/eigen3 \
  -DBUILD_CONVERT=OFF -DBUILD_IFCPYTHON=OFF -DBUILD_GEOMSERVER=OFF -DBUILD_EXAMPLES=OFF \
  -DCOLLADA_SUPPORT=OFF -DCMAKE_SHARED_LINKER_FLAGS='-Wl,--as-needed' -DCMAKE_INSTALL_PREFIX="$PWD/ifc-prefix"
cmake --build build-ifcopenshell --parallel 4 && cmake --install build-ifcopenshell

# Either copy libifcopenshell*.so* and ifcopenshell_*.so from ifc-prefix/lib over /usr/lib/sketchyup,
# or run the helper against a directory of your own:
LD_LIBRARY_PATH="$PWD/ifc-prefix/lib" /usr/lib/sketchyup/sketchyup-ifc-worker --probe
```

`--probe` prints the version of the libraries and the path of every library the helper loaded, so
you can see which copy it used. The same procedure applies to the OpenCASCADE libraries: put your
build's `libTK*.so` files in the directory on `LD_LIBRARY_PATH`. `IFC-SOURCE.txt` lists where the
corresponding source is.

## Release artifacts

From a clean checkout of the commit to release:

```sh
scripts/release-artifacts.sh "$PWD/build/release-1.3.0"
```

The script refuses a dirty or untracked working tree. It writes, into the output
directory, the source archive and PKGBUILD from `package-source.sh`; `THIRD_PARTY_NOTICES`
with a header for each file `CMakeLists.txt` installs into `share/licenses/sketchyup`
(the SketchyUp license, Clipper2, Manifold license and authors, cgltf, CDT and its
predicates notice, the Poly Haven CC0 texture notice (ADR 0174 Q1), and for `sketchyup-ifc` the
IfcOpenShell LGPL and GPL texts and the OpenCASCADE licence and exception); `SHA256SUMS`
for the archive, the notices and any `sketchyup-VERSION-PKGREL-*.pkg.tar.zst` or
`sketchyup-ifc-VERSION-PKGREL-*.pkg.tar.zst` already
in the directory; and `MANIFEST.json` with the version, `pkgver`/`pkgrel`, Git commit,
SHA-256 values, the package job's pinned image digest from
`.github/workflows/native.yml`, and the date (`SOURCE_DATE_EPOCH` if set). It does not
build packages: build them in the pinned container as above or as CI's Arch package
acceptance job does, and copy the packages into the directory before running it. It
rejects packages of another version or pkgrel, and an existing archive that differs
from the commit's archive. It creates no tags or releases.
