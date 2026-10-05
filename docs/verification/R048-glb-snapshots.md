# R048 immutable GLB snapshots

Date: 2026-10-05 UTC. Local acceptance passed; CI/merge acceptance pending.
[Transfer subset, limits and losses](../decisions/0032-glb-snapshots.md).

All 58 development CTest suites passed in 32.54 s. The GLB suite also passed
ASan/UBSan with leak detection enabled in 0.89 s. The final CLI regular-file guard
passed the targeted development suite again. No sanitizer checks were disabled.

The fixture parser verifies GLB headers, chunk order/alignment, buffer-view bounds,
float32 accessor counts/ranges and finite attributes. A 2 × 3 × 4 m box exports
12 triangles. Its mirrored component placement retains negative scale and shares
the same GLB mesh. Document/ancestor/tag hiding and captured transient face hiding
remove the intended geometry. Capture followed by live edits and an export on a
worker returns identical bytes and manifest for the original revision. Replacing
an asset after capture also preserves the captured payload. Packaged native model
assets export identically without external files.

Camera checks cover perspective and orthographic axes, aspect ratio, vertical
framing and clipping parameters. Auto-fit can place a camera outside the modeling
coordinate range. Invalid settings, degenerate cameras, local shear, empty visible
scenes, mismatched artifact hashes, existing export directories and mixed CLI
operations fail without replacing existing output or the source model.

Four persistent interoperability fixtures—box, mirrored instances, materials and
orthographic camera—pass Khronos glTF Validator `2.0.0-dev.3.10` with **zero errors
and zero warnings**. Informational messages note default identity matrices and
opaque managed-asset buffer views that ordinary glTF consumers do not use.
The validator package is version/integrity locked under `tests/gltf-validator`.

Actual Blender **5.2.1 LTS**, build `9e2066aef7ef`, imports all four fixtures.
Checks confirm dimensions within 0.1 mm, shared mesh identity, one mirrored object,
outward normals, camera position/direction and vertical perspective/orthographic
framing. Imported material opacity is 0.35 and a 0.5 sRGB swatch maps to approximately
0.214041 linear base color. Script errors exit nonzero; auto-execution is disabled.
CI runs the same validator and Blender script independently of native package tests.

The R047 room also passes official validation with zero errors/warnings and imports
into Blender with six geometry objects and the expected 6 × 4 × 2.7 m bounds.
The installed CLI exports that saved revision-two room headlessly using the
installed settings example, verifies its GLB hash and preserves native source
bytes. The source archive contains exporter, capture code, tests, pinned validator
metadata, settings example and contract. Package CI checks the installed export
and settings artifact removal.

These checks establish the documented GLB subset, not full native-material parity.
UV texture application and different front/back rendering remain reported losses.
R049 owns worker execution/cancellation; R050 owns native camera capture and render
UI. Blender remains optional for editing, persistence and GLB export.
