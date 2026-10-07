# R077.a — Bounded planar DXF source parsing

2026-10-07. Parser/fixture contract `7672cd7`; final integration `c7f69da`.
Parent: [PR #179](https://github.com/zygote55/sketchyup/pull/179).
Contract: [0120](../decisions/0120-bounded-planar-dxf-source.md).

The complete Debug build passes **159/159 CTest suites in 169.43 s**, including
existing actual Blender interchange. Five final native Wayland 2× integration
checks pass in **22.326 s**
([matrix](R077a-native-matrix.json)). Dedicated source parsing passes **1/1 in
0.03 s** and ASan/UBSan **1/1 in 0.07 s**, with leak detection and halt-on-error.
Independent ezdxf-produced R12/R2018 source fixtures cover the supported entity
representations and unit contracts.

The bounded ASCII parser handles world-XY lines, circles/arcs, lightweight and
legacy polylines, including bulges and layer metadata. Unitless sources require
an explicit override; supported header units are otherwise converted to metres.
Unsupported entities are counted, malformed structure or unsafe geometry rejects,
and no DXF directive executes code or loads sidecars. Native conversion and export
remain subsequent layers, with a deliberately disclosed 2D subset.

The [installed smoke](R077a-installed-smoke.json) verifies eight catalogs,
twenty-five contracts, existing STL/OBJ/glTF workflows, desktop/license bytes and
source/overwrite protection. [Source package](R077a-source-package.json): **171
installed inputs** match byte for byte; SHA-256 `3cb23fc409872adaa02f63e83b5bfaff5c859f1246814669c67ffa6cb250c3f6`.

Remote CI and ordered merges remain delivery gates; M7/M8 acceptance is not
claimed while prerequisite PRs remain open.
