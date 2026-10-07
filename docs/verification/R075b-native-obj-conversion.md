# R075.b — Native OBJ conversion

2026-10-07. Conversion `18d465d` development series, redo correction `2c5ec9f`, independent
Blender producer `856110c`, contract `2713290` and CI `aed6aca`; integration `1adbe8f`.
Parent: [PR #172](https://github.com/zygote55/sketchyup/pull/172).
Contract: [0113](../decisions/0113-native-obj-conversion.md).

The complete Debug build passes **151/151 CTest suites in 168.88 s**, including
actual Blender fixtures. The four final native Wayland 2× integration checks pass
in **18.399 s** ([matrix](R075b-native-matrix.json)).
Dedicated OBJ conversion and independent Blender producer checks pass **2/2 in
1.16 s**. ASan/UBSan conversion passes in **0.55 s**, with leak detection and
halt-on-error enabled.

Contained resource capture rejects traversal, escaping symlinks and remote paths.
Missing supported images/materials yield explicit fallback notices. Decoded image,
package, geometry, body and element limits reject before publication. Converted
objects/groups, wires, diffuse materials, affine UV faces, triangulated non-affine
UVs and smoothing/normal seams become editable native topology in one undoable
operation. Fixtures check reflected Blender geometry, texture capture, world units,
redo/persistence and source preservation. Unsupported source fidelity is reported;
no source script executes.

The [installed smoke](R075b-installed-smoke.json) verifies eight catalogs,
eighteen contracts, installed glTF import, existing/source-file protection,
desktop/license bytes and installed/relocated export behavior.
[Source package](R075b-source-package.json): **164 installed inputs** match byte
for byte; SHA-256 `d2ee55aa9721d885b083cce623643e014ec3ed7490137685e13ce8312dc8aa23`.

OBJ export and native/CLI workflows follow in R075.c–d. Required remote CI and
ordered merges remain delivery gates. M7 acceptance is not claimed while its
prerequisite PRs remain open.
