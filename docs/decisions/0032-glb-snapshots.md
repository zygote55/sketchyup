# 0032: Immutable GLB render snapshots

Status: accepted for the M5 interchange subset.

R061.d extends this subset with [textured GLB/Cycles transfer](0064-textured-glb-export.md):
validated embedded images, independent side UVs, image alpha and bounded decoding.
The original M5 limitations below describe the baseline; that later contract
supersedes its opaque-image and absent-UV behavior.

Capture copies a history-free document snapshot, camera, render settings and
optional transient hidden entities on the document owner thread. Published model
records and asset payloads are immutable. A worker exports only the captured
value; later edits, visibility changes and asset replacements cannot change its
bytes or source revision. Blender is not needed to capture or export.

## Transfer contract

The exporter writes GLB 2.0 with embedded binary data and a JSON sidecar. Native
coordinates are metres with Z up. A proper root rotation maps `(x,y,z)` to
`(x,z,-y)` for glTF. Original body/group hierarchy and local transforms remain
below that root. Mirrored scales remain signed. Local shear is rejected because
glTF node matrices must decompose to translation, rotation and scale. Independent
imports test both dimensions and orientation against the
[Khronos glTF specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).

Visible native faces are triangulated into nonindexed float32 positions and corner
normals, grouped by appearance. Normals are flat by default; R057.g derives smooth
fans only across explicitly smooth native edges ([shading contract](0052-smooth-shading.md)). Identical mesh buffers/material bindings share a
mesh, including repeated component instances. Nodes carry stable body IDs in
extras; the sidecar maps body, parent, face/primitive/vertex ranges and component
member/definition identities. This is a rendering transfer, not a native topology
or history round trip. Float32 coordinates have lower precision than native
geometry; very large coordinates can lose small details.

Persistent hidden bodies, hidden tags and ancestors are excluded. A caller can
also capture transient hidden bodies/faces. Locks do not hide geometry. Guides,
wire edges, selection overlays and grid are omitted. Analytic face boundaries use
the existing tessellation; analytic curve records themselves are not transferred.
No sections, lights, UV mapping or editor overlay effects are exported in this
initial subset. Omitted content is counted in the manifest.

## Appearance and managed assets

Native sRGB swatches convert to linear PBR base colors, with metallic zero,
roughness 0.8 and the recorded opacity. Transparent materials use alpha blending.
R057.d preserves distinct sides with opposite single-sided triangles; identical
appearances retain one double-sided surface. The Cycles worker converts paired
triangles into a single surface with front/back shaders. See the
[two-sided transfer contract](0050-two-sided-export.md) for metadata, limits and
historical snapshot compatibility.

Assets referenced by visible front/back materials are copied from the native
managed payloads into GLB buffer views. Their IDs, names, media types, byte ranges
and SHA-256 digests appear in extras and the sidecar; byte offsets are relative
to the GLB BIN payload. Missing assets have explicit markers. No external paths
or URIs are resolved. Because the editor has not yet authored UV coordinates,
these payloads are preserved as opaque managed assets, not applied as glTF
textures. Materials use their color/opacity fallback and the loss report records
unmapped assets. Arbitrary asset bytes are not passed off as validated images.

## Camera, bounds and publication

Perspective and orthographic cameras retain their position, target/up,
vertical field of view or half-height, and clipping planes. The output resolution
determines aspect ratio. A headless caller may omit the camera for a visible-bounds
fit; desktop camera capture is the R050 integration step. Samples and seed are
captured for the subsequent renderer even though GLB itself does not execute a
render. The sidecar identifies document, revision, camera, settings, axes, units,
native bounds and the exact GLB hash.

Settings JSON requires `apiVersion: 1`, accepts optional `width`, `height`,
`samples`, `seed`, and `camera`, and rejects unknown fields and malformed values.
Camera requires `projection`, `position`, `target`; optional fields are `up`,
`verticalFov` (radians), `yMag` (metres), `nearClip`, `farClip`. See
`examples/render-settings-v1.json`. Limits are 64–4096 pixels per dimension,
1–1024 samples and seed 0–1,000,000. Camera coordinates are bounded separately from
model coordinates so the camera can frame models at the edge of the modeling
range.

Capture is limited to 256 MiB of retained document data and 20,000 records (the
current model also has its own tighter limits). Export caps 100,000 visible faces,
2,000,000 face-vertex references, 1,000,000 triangles and 4,096 appearances. GLB
is capped at 256 MiB, JSON and sidecar at 16 MiB each. Unsupported geometry or
limits fail before publication.

`sketchyup-cli --input MODEL --export-glb NEW_DIRECTORY` is an exclusive mode;
`--render-settings FILE` is optional and limited to 16 KiB. It never overwrites an
existing directory or the input model. Publication creates a private directory,
writes `scene.glb`, then writes the hash-bearing `manifest.json` as the completion
marker. Failed writes clean up owned partial artifacts. Consumers must verify the
manifest hash; directory publication is not a multi-file crash-durability promise.
This filesystem operation is not exposed as a model-controlled assistant tool.

## Interoperability verification

The development suite parses binary chunks/accessors and checks snapshot
isolation, hierarchy, shared mirrored meshes, visibility, packaged assets,
cameras, malformed settings and CLI publication. The official
[Khronos validator](https://github.com/KhronosGroup/glTF-Validator/tree/main/node)
is pinned to `2.0.0-dev.3.10` with an integrity-locked test dependency, retaining its
Apache-2.0 license. It is not an application runtime dependency.

A Blender script imports the original four fixtures with script auto-execution disabled and a
nonzero Python-error exit code. It checks dimensions, reflected instance count,
shared mesh identity, outward normals, camera framing and material color/opacity.
Blender is an optional external application, not linked or bundled with SketchyUp.

R057.d adds sided export fixtures, actual Cycles pixels and malformed-pair rejection
through `scripts/verify-blender-sides.py`; all eleven GLBs pass the same validator.
