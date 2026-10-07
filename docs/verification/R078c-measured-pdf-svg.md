# R078.c — Physically scaled PDF and SVG

2026-10-07. Serializer contract `1d89c22`; integration `84a9b0d`.
Parent: [PR #185](https://github.com/zygote55/sketchyup/pull/185).
Contract: [0126](../decisions/0126-measured-pdf-svg-output.md).

Desktop/CLI and affected test targets build successfully. Final measured projection,
capture, serializer and independent Poppler checks pass **4/4 in 1.34 s**.
Fresh ASan/UBSan serializer verification passes **1/1 in 0.34 s**, with leak detection
and halt-on-error. Native units and annotation regression tests both pass on
Wayland at 2× scale (**8.233 s** total); shared formatting moved to presentation
headers without changing native formatting behavior.

Independent XML and PDF rendering verify physical page dimensions, a 2 m edge
at 1:50 as 40 mm, standalone wires, clipped raster appearance and the embedded
PDF report. Unicode labels use portable vector outlines; missing glyphs and
rasterization are reported. The retained multilingual dimension PDF was rendered
with Poppler and visually inspected. Invalid raster dimensions, byte/hash integrity
and non-replacing publication are covered. PDF paper edges round to Qt's whole-point
representation; geometry retains physical scale. Outlined PDF text is not searchable.

The [installed smoke](R078c-installed-smoke.json) verifies eight catalogs,
thirty-one contracts and existing DXF/STL/OBJ/glTF workflows. New drawing formats
are verified through the serializer here; their CLI and native workflows follow.
[Source package](R078c-source-package.json): **177 installed inputs** match byte for
byte; SHA-256 `19b6e127649336036ca7fc549728f8442b84b32f73c2f80bc12bbfbed5212d81`.
[Native regression results](R078c-native-matrix.json) retain individual timings.

Complete remote CI, ordered merges and M7/M8 milestone acceptance remain required.
