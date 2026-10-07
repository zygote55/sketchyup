# R076.a — Bounded ASCII and binary STL parsing

2026-10-07. Parser and dimensionally correct facet-normal check `e2e673b`;
final integration `df43f04`. Parent: [PR #175](https://github.com/zygote55/sketchyup/pull/175).
Contract: [0116](../decisions/0116-bounded-stl-source-parsing.md).

The complete Debug build passes **154/154 CTest suites in 171.22 s**, including
actual Blender interchange. Five final native Wayland 2× integration checks pass
in **22.622 s** ([matrix](R076a-native-matrix.json)).
Dedicated STL parser checks pass **1/1 in 0.09 s**; ASan/UBSan passes **1/1 in
0.16 s**, with leak detection and halt-on-error enabled.

Both ASCII and binary STL are parsed with explicit coordinate units and Y/Z up
conversion. Input is bounded to 64 MiB and 100,000 facets, with finite coordinates,
strict record/count/termination checks and deterministic encoding detection.
Normals are advisory and diagnosed against geometric facet normals using an
area-dimensional tolerance. The reader does not silently weld vertices, drop
facets or infer units; repair and native conversion follow in R076.b.

The [installed smoke](R076a-installed-smoke.json) verifies eight catalogs,
twenty-one contracts, existing OBJ/glTF CLI workflows, desktop/license bytes and
source/overwrite protection. [Source package](R076a-source-package.json): **167
installed inputs** match byte for byte; SHA-256 `22e6acc837e7c2be84eb0e666622d19110200b09def388d62896dba5d105666c`.

Native STL conversion, export and File-menu/CLI workflows remain subsequent layers.
Remote CI and ordered merges remain delivery gates; M7 acceptance is not claimed
while prerequisite PRs remain open.
