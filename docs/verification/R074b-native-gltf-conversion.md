# R074.b — Editable native glTF conversion

2026-10-07. Core `d5dec6c`, final decode-budget/duplicate-name corrections
`30f3045`/`3b03ce0` as incorporated by integration `72a6bbe` (authoritative source).
Parent: [PR #169](https://github.com/zygote55/sketchyup/pull/169).
Contract: [0110](../decisions/0110-native-gltf-conversion.md).

The complete Debug build passes **147/147 CTest suites in 175.58 s**.
Four final native Wayland 2× integration checks pass in
**18.324 s** ([matrix](R074b-native-matrix.json)).
Focused conversion plus independent Blender production pass in **4.17 s**;
final conversion ASan/UBSan with leak detection and halt-on-error passes in
**8.06 s**. Shared final core includes the aggregate decoded-image budget and
unique native material labels for valid duplicate glTF material names.

Captured packages become a separate editable native document in one undoable
edit. Node groups, shared mesh definitions, transformed/reflected instances,
triangle lists/strips/fans, diffuse colors/opacity, managed PNG/JPEG images,
pinned UV mappings, KHR_texture_transform and supported cameras are converted.
The full native persistence validator checks the result before publication.
Explicit bounds cover expanded geometry, native records, hierarchy and decoded
image memory. Unsupported skinning, morph targets, compression and alpha masks
reject; omitted PBR channels, lighting, animation and other unsupported metadata
are reported. Normals derive from native geometry and supported edge smoothing.
Import does not claim lossless glTF round trips.

Independent Blender production checks textured reflected geometry, millimetre-free
metre coordinates, placement and camera conversion. Fixtures additionally cover
sparse accessors, primitive modes, texture transforms, opacity, repeated instances,
duplicate material names, geometry limits, cancellation-independent captured bytes,
persistence and undo. The source package reader remains separately bounded.

The [installed smoke](R074b-installed-smoke.json) verifies eight catalogs, fifteen
contracts, exact desktop/library-license bytes and existing lighting exports.
[Source package](R074b-source-package.json): **161 installed inputs** match byte
for byte; SHA-256 `ccd74f90da2e3853f2009282dc558819f3c137e9309aa9f9fff726dad017323a`.

R074.b is locally complete. Native File-menu and standalone CLI import follow in
R074.c. Required remote CI and ordered merges remain delivery gates; M7 acceptance
is not claimed.
