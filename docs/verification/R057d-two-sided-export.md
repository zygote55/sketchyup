# R057.d — Two-sided GLB and Cycles appearance

Date: 2026-10-06 UTC. Depends on R057.c. Local export/render acceptance passes;
CI and dependency merges remain pending. Edge display semantics remain later
R057 work. [Transfer contract](../decisions/0050-two-sided-export.md).

Distinct front/back materials now export as opposite single-sided GLB triangles.
The fixed Blender worker verifies those pairs, imports one physical surface and
mixes independent Principled shaders using Backfacing. Identical appearances
retain the original double-sided transfer. Reversed and mirrored faces retain
physical color placement, opacity, native face mappings and shared mesh storage.
The source snapshot is immutable and private import copies are removed.

Validation on Blender 5.2.1 LTS and the development toolchain:

- Full development build and **77/77 CTest tests**, including the real Blender
  worker lifecycle with distinct front/back materials: **48.75 seconds**.
- Native X11 render setup, cancellation, revision capture, actual Blender result
  and image saving pass in the isolated display container (Blender 5.2.2).
- Export and render-job suites pass ASan/UBSan/leak detection: **2/2**, 4.33 seconds.
- All **eleven GLBs** pass the pinned Khronos validator `2.0.0-dev.3.10` with
  **zero errors and zero warnings**. The original box, mirrored instance, managed
  material and orthographic imports still satisfy dimension/camera/material checks.
- Six sided sheet fixtures cover ordinary, reversed, reflected, reflected and
  reversed, transparent-front, and holed geometry under rotated nonuniform
  placements. All **twelve actual Cycles pixel checks** pass. Opaque physical-front
  RGBA is approximately `(0.9804, 0.1608, 0.1608, 1)` and physical-back RGBA
  `(0.1608, 0.1608, 0.9804, 1)` across these transforms. Transparent-front alpha is
  zero while its back remains opaque blue. The test camera accounts for Blender's
  local glTF coordinate conversion independently of imported face normals.
- A box with one reversed face uses the same native red/blue materials in opposite
  pair roles. Two reflected component instances retain a shared twelve-triangle
  Blender mesh and red physical exterior after conversion.
- Seven malformed-pair mutations reject: unsupported version, duplicate pair,
  missing primitive, invalid color, mismatched normal, mismatched position and
  wrong appearance identity. Historical v1/no-pair snapshots remain compatible.

Reproduction:

```sh
build/dev/glb_export_tests build/r057d-final
node scripts/verify-glb.mjs build/gltf-validator build/r057d-final
blender --background --factory-startup --disable-autoexec --python-exit-code 1 \
  --python scripts/verify-blender-glb.py -- build/r057d-final
blender --background --factory-startup --disable-autoexec --python-exit-code 1 \
  --python scripts/verify-blender-sides.py -- build/r057d-final
SKETCHYUP_BLENDER_TEST=/usr/bin/blender ctest --test-dir build/dev --output-on-failure
```

The fixture directory must be fresh for the exporter. CI runs both Blender
scripts. Local reports are retained under `build/r057d-final`:

- `glb-validation.json`: `88e74fbc7f8792b1c5048c2b7e78a453c24611be8ca6f896754d065484d5887f`
- `sided-blender-validation.json`: `ca9fe9344d0293fe01e4dbae6c91127ded78697d16a491f3b506872c47be6e38`
- `blender-validation.json`: `028891e5ecda8809850ac0cd392bd2b133fe45eb93f1acc95f48bbc8c59316a0`

No live AI-provider acceptance is claimed for this export slice. Unmapped texture
assets and other pre-existing transfer limitations remain explicit.
