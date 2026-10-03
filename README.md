# SketchyUp

An independent native Linux 3D modeler in development. C++20, Qt 6 and OpenGL;
ordinary editing runs locally without an account or browser runtime. Own code
is MIT licensed. Arch/Omarchy is the primary development environment.

## Current build

**Working native spike, not a complete editor or release.** Draw ground-plane
rectangles and circles, select faces, extrude an isolated face, translate and
color objects, undo/redo, and save/reopen native `.sketchyup` files. Faces retain
editable loops and holes. A headless driver uses the same core operations.

The [108-entry roadmap](docs/PR_ROADMAP.md) has passed its M0 feasibility and
M1 native foundation gates; editable topology is in progress. Adjacent-face
push/pull, components, inference, recovery, AI providers,
Blender integration and exchange formats are not implemented. Circle geometry is
currently a 48-sided polygon. Surface topology and file format remain experimental.
Save explicitly: there is no autosave or recovery journal. Explicit saves use a
checksummed container and preserve the previous valid file as `.sketchyup.bak`.
Files and the containing directory are synced before showing Saved. Open a backup
through the file dialog's all-files filter or the CLI if you need the previous save. Light, Dark and System
themes are available in View. The experimental scene records support nested/mirrored
transforms through the headless command API; component editing UI remains planned.

## Build and run

Requires CMake 3.25+, Ninja, a C++20 compiler, Qt 6.8+ base/Wayland development
packages and OpenGL 3.3. Verified locally with Qt 6.11.2 on Wayland and X11.
The pinned geometry dependency is included in `third_party/`; CMake does not
download code. See [measured evidence and limitations](docs/verification/native-spike.md).

```sh
cmake --preset dev
cmake --build --preset dev --parallel 4
ctest --preset dev
./build/dev/sketchyup --demo
```

On Arch, build dependencies are `cmake ninja gcc qt6-base qt6-wayland`.
For the core alone, with no Qt or graphical session:

```sh
cmake --preset headless
cmake --build --preset headless
ctest --preset headless
```

## Modeling

- `R`: rectangle. Click the first corner, then the second; or enter `width, depth`
  in Measurements and press Enter. Values are meters; pointer snapping is 0.1 m.
- `C`: circle. Click center, then radius; or enter the radius.
- `Space`: select. `P`: select an isolated face, enter extrusion distance.
- `M`: translate selected object numerically. `B`: change its color. `Delete`: remove it.
- Middle drag or `O`: orbit. Right drag or `H`: pan. Wheel: zoom. `Shift+Z`: fit.
- `1/2/3`: perspective/top/front. `Escape`: cancel drawing.
- `Ctrl+Z` / `Ctrl+Shift+Z`: undo/redo. `Ctrl+K`: commands, objects and recent files.
- `F6` / `Shift+F6`: move between window regions. `Ctrl+Shift+T`: toggle the Model panel.
- `Ctrl+O` / `Ctrl+S`: native open/save dialogs. Unsaved changes prompt before replacement or close.

## Headless commands

```sh
./build/dev/sketchyup-cli --capabilities
./build/dev/sketchyup-cli --describe-command geometry.translate
./build/dev/sketchyup-cli --script examples/room-shell.json --output /tmp/room.sketchyup
./build/dev/sketchyup-cli --input /tmp/room.sketchyup
./build/dev/sketchyup /tmp/room.sketchyup
./build/dev/sketchyup-cli --input /tmp/room.sketchyup --query geometry.inspect --context 1
./build/dev/sketchyup-cli --script examples/split-wire.json --output /tmp/wire.sketchyup
./build/dev/sketchyup-cli --script examples/planar-grid.json --output /tmp/grid.sketchyup
```

The topology query exposes stable context-scoped edges, oriented loops and radial
adjacency. The wire recipe demonstrates a loose edge and a propagated edge split;
these operations currently have command paths while direct edge tools are in progress.
The planar-grid recipe forms four editable faces from finite segments. Planar insertion
handles intersections, overlaps and holes within the documented arrangement limits.

A script is a local JSON array, validated and committed as one batch. The in-process
API checks document identity and revision; failed batches change nothing. This is
an experimental local driver, not yet the durable AI/MCP protocol. The example's
IDs are specific to its empty-document fixture. Query live IDs before editing an
existing document; do not reuse them across documents.

## Viewport verification

The [viewport follow-up](docs/verification/R002-viewport.md) records native pixel,
picking, GPU-cache and context-recreation checks, including both physical display
scales. Run `build/dev/viewport_tests` in a graphical session. The `--benchmark`
option now measures independent triangle buffers; add `--instanced` for the
original repeated-triangle comparison. Transparency and clipping are currently
renderer test APIs, not finished material or section tools.

## Development package

```sh
./scripts/package-source.sh /tmp/sketchyup-package
cd /tmp/sketchyup-package
makepkg -s
```

The script creates a source archive and writes its checksum into the generated
PKGBUILD. Package output contains the native app, CLI, desktop entry, icon, MIME
definition and license notices. [Clean Arch acceptance](docs/verification/R014-package.md)
covers install, desktop launch, upgrade/reopen and removal. See the
[package workflow](packaging/arch/README.md) for reproduction. This remains an
experimental development package.

## Design and delivery

- [Build plan](docs/BUILD_PLAN.md) and [scope matrix](docs/SCOPE.md)
- [PR roadmap](docs/PR_ROADMAP.md)
- [UX design and mockups](docs/UX_DESIGN.md)
- [AI modeling contract](docs/AI_MODELING.md) — proposed future behavior
- [Baseline/license decision](docs/decisions/0001-baseline.md)
- [Native architecture experiments](docs/decisions/0002-native-spikes.md)
- [Compatibility gaps](docs/decisions/0003-compatibility-gaps.md)

The original local Formline/Electron prototype was removed at the owner’s request.
The native application now lives in the main project checkout; supplied UX design
references remain preserved.
SketchyUp is not affiliated with SketchUp or Trimble.
