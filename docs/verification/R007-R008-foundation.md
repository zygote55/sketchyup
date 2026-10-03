# R007/R008 foundation evidence

Date: 2026-10-03. Implementation pending PR merge; M1 as a whole remains open.

The CMake core is C++20 without Qt. The CLI/persistence library adds Qt Core; the
native shell adds Qt Widgets/OpenGL. Vendored Clipper2 is pinned; CMake downloads
nothing. `dependencies.json` records licenses, minimum versions and provenance.
`.clang-format` is the C++ formatting policy. The Arch CI base image is pinned by
digest and its package repositories by date (2026/10/02), rather than rolling
`latest` packages. The local Dockerfile uses the same mirror and base image.

Clean container packages:

```text
cmake 4.4.3-2
ninja 1.13.2-3
gcc 16.2.1+r23+gd564253eb6c8-1
qt6-base 6.11.2-3
qt6-wayland 6.11.2-1
mesa 1:26.2.3-2
```

Scene records now contain an affine local transform, optional parent and bounded
typed properties. World transforms compose parent × local; mirrors are allowed.
Cycles, missing parents, singular matrices, excessive depth and out-of-bounds
world geometry reject atomically. Moving a child applies a world-space delta
through its parent's inverse, preserving local geometry. Undo/redo restores the
same records. Picking, rendered triangles, edges, fitting and area queries all
use world geometry. No component/group editing UI is claimed by these records.

Raw JSON schema v2 adds transforms, parent references, properties and persistent
content revision. Schema v1 migrates with identity transforms, empty properties
and revision zero, preserving entity/document IDs and geometry. Future versions
are rejected. This is still the raw document chunk; R012 adds the envelope and
stronger save ordering selected in ADR 0005. Revision exhaustion fails before
mutation. The new scene-transform command is available to the headless driver;
scene queries expose world matrices and world areas.

Verification targets:

- `scene_tests`: unit conversion, nested mirrors, inverse/area checks, world-space
  movement, cycle/parent/singular/bounds rejection, immutable properties and
  history; runs in the no-Qt core and sanitizer builds.
- `desktop_tests`: exact v2 roundtrip including revision and nested properties,
  original v1 migration, existing save/short-write and atomic batch regressions.
- `viewport_tests`: nested mirrored triangles render at their picked position;
  transform undo updates picking before repaint, alongside existing render tests.
- Existing native drawing, dialog and layout tests retain their coverage.

Reproduce with dev/headless/sanitize presets. The clean container also runs all
native widget suites under Xvfb. Use `docker build -f
packaging/arch/Dockerfile.build -t sketchyup-build:20261002 .` to build the isolated
toolchain. The container never changes the host's package repositories.
