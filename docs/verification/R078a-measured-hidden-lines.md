# R078.a — Measured orthographic hidden-line geometry

2026-10-07. Geometry contract `5964d65`; integration `e217e02`.
Parent: [PR #183](https://github.com/zygote55/sketchyup/pull/183).
Contract: [0124](../decisions/0124-measured-orthographic-hidden-lines.md).

The complete desktop/CLI targets build with the new pure geometry layer. Final
geometric-oracle checks pass **1/1 in 0.07 s**; the identical geometry/test sources
previously passed ASan/UBSan **1/1 in 0.03 s**, with leak detection and halt-on-error.
Checks cover physical scale, rotated frames, page margins, clipping, visible/hidden
intervals, foreground/behind/coplanar triangles, winding/duplicate-occluder invariance,
changing depth, collapsed projections, invalid settings and dense-grid work limits.

An orthonormal world frame maps to physical millimetres, so a one-metre edge at
1:50 is 20 mm. Bounded projected-triangle occlusion splits visible/hidden line
intervals after page clipping. A 32×32 broad-phase grid and explicit input/work/output
limits reject excessive work without returning a partial drawing. This layer has
no Qt, document mutation or file IO. Document capture, PDF/SVG and native/CLI
workflows follow in R078.b–e; native output acceptance is recorded with those layers.

The [installed smoke](R078a-installed-smoke.json) verifies eight catalogs,
twenty-nine contracts and existing DXF/STL/OBJ/glTF workflows with source protection.
[Source package](R078a-source-package.json): **175 installed inputs** match byte for
byte; SHA-256 `316d6865e7a6c8772f33e19defa4510581784c01827b3cb37e1f93e3692a14c8`.

Complete remote CI, ordered merges and M7/M8 milestone acceptance remain required.
