# 0050 — Preserve physical face sides in GLB and Cycles

Status: R057.d implementation; local acceptance and CI status are recorded in
[verification](../verification/R057d-two-sided-export.md). Edge display semantics
remain a separate R057 layer.

R061.d extends these physical-side rules to [independent textures and UVs](0064-textured-glb-export.md).
Textured snapshots use version 2 of the sided metadata; the worker still accepts
the version 1 color/opacity snapshots described here.

## Standard interchange

Identical front/back appearances retain one double-sided primitive. Different
appearances emit two coincident, oppositely wound, single-sided triangle sets,
with opposite normals and independent material color/opacity. No geometric offset
or thickness is introduced. Pair-specific material identities keep the same
native material independent when it is front in one pair and back in another.
Repeated instances continue sharing identical mesh buffers, including mirrors.
This follows the standard [glTF sidedness and determinant conventions](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).
Viewers must implement glTF backface culling, including negative transforms.

The sidecar retains the existing front primitive/range fields and adds
`backAppearance`, `backPrimitive`, `backFirstVertex`, `backVertexCount` on distinct
sides. Both appearances retain their source material IDs. `facesWithDistinctSides`
counts represented faces; new snapshots omit the former
`differentBackAppearancesUseFront` loss. Existing result UI still understands that
historical loss. Material assets remain opaque payloads without UV mapping.

Every material carries its GLB index as `extras.sketchyupAppearance`. Root extras
contain `sketchyupSidedMaterials: {version: 1, pairs: [{front, back}]}`. These are
optional application metadata; standard viewers need no extension to draw the
triangles. The existing million-triangle budget counts both emitted sides and the
4,096-appearance cap counts both material entries. File/JSON limits are unchanged.
The metadata describes render duplicates, never native topology edits.

## Cycles adapter

The Blender 5.2 importer sets a material culling flag for single-sided glTF, but
that flag alone does not supply two independent Cycles shaders. The fixed worker
imports a private derived GLB with only the paired front primitives, then uses
Geometry Backfacing to mix the front and back Principled BSDFs. Color, opacity,
metallic zero and roughness 0.8 match the transfer contract. Actual Cycles fixtures
verify both physical sides through reflection/reversal. Keeping one physical
surface avoids ambiguous coincident ray intersections and double opacity.

Before dropping a back primitive the worker validates the versioned, bounded
material pairs, unique indices, supported appearance subset, matching vertex
counts, byte-exact reverse position order, and opposite finite normals. Each
pair must occur completely in each mesh that uses it. Validation is linear in
present primitives and paired triangle data, with at most one million paired
triangles and 2,048 pairs. Unsupported or malformed pairs fail before import.
The derived copy retains binary offsets, geometry normals, node hierarchy and
shared mesh identity. It lives in a private temporary directory and is removed
after import. The published snapshot bytes and hashes remain unchanged.

Historical v1 snapshots without the metadata retain their original behavior and
loss reports. No model-supplied Python or expressions are evaluated. The helper
is part of the same fixed, version-pinned worker used by native render jobs.
