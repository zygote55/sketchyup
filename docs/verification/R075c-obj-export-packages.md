# R075.c — OBJ material and texture export packages

2026-10-07. Export `351b45b`, vertex identity and independent consumer `f743ced`,
contract `ddd9ab4`; final integration `258df9c`.
Parent: [PR #173](https://github.com/zygote55/sketchyup/pull/173).
Contract: [0114](../decisions/0114-contained-obj-export-packages.md).

The complete Debug build passes **153/153 CTest suites in 174.89 s**, including
actual Blender interchange. Four final native Wayland 2× integration checks pass
in **18.305 s** ([matrix](R075c-native-matrix.json)).
Dedicated OBJ import/export and actual Blender producer/consumer checks pass
**4/4 in 2.26 s**. ASan/UBSan import/export passes **2/2 in 0.82 s**, with leak
detection and halt-on-error enabled.

Export captures all world-space model geometry, including hidden objects and
unclipped surfaces. It preserves separate coincident vertex identities, supported
polygons and wires, transformed normals, reflected winding, diffuse materials,
opacity and embedded PNG/JPEG textures. Holes/oversized polygons triangulate;
unsupported native/back-face metadata is counted. Tests and the actual Blender
consumer verify bounds, faces, material/texture assignment and reflection behavior.

Publication requires a new package directory. Generated names contain resource
paths; all payload bytes are verified and the manifest is written last. Existing
directories reject. Handled failures remove the newly created incomplete package;
an abrupt process interruption may leave an incomplete directory without a manifest.
Native source bytes, identity and history remain unchanged.

The [installed smoke](R075c-installed-smoke.json) verifies eight catalogs,
nineteen contracts, installed glTF import, existing/source-file protection,
desktop/license bytes and installed/relocated export behavior.
[Source package](R075c-source-package.json): **165 installed inputs** match byte
for byte; SHA-256 `c035a15f9f0ec14c0980f978042914234c2a83543da5ed619af3d7cdb06cef09`.

The File-menu and CLI OBJ workflows follow in R075.d. Required remote CI and
ordered merges remain delivery gates; M7 acceptance is not claimed while its
prerequisite PRs remain open.
