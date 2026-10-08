# R076.c — Bounded STL export and new-file publication

2026-10-07. Export `6d4122c`, independent Blender consumer `7dfa7cc`,
contract `7352b3d`; final integration `b7734c5`.
Parent: [PR #177](https://github.com/zygote55/sketchyup/pull/177).
Contract: [0118](../decisions/0118-bounded-stl-export.md).

The complete Debug build passes **158/158 CTest suites in 177.55 s**, including
actual Blender interchange. Five final native Wayland 2× integration checks pass
in **23.079 s** ([matrix](R076c-native-matrix.json)).
Dedicated export/native-format and actual Blender producer/consumer checks pass
**4/4 in 2.42 s**. ASan/UBSan export plus native-format regressions pass **2/2 in
19.35 s**, with leak detection and halt-on-error enabled.

Export traverses complete world-space surface geometry, including hidden bodies,
and preserves reflected winding. Units, up axis and encoding are explicit.
Binary output rejects excessive float32 error, collapsed facets and reversed
orientation; ASCII retains double precision. Both paths have bounded facet/file
budgets and report format losses. A shared publication helper writes and synchronizes
a temporary file, publishes without replacing an existing destination, and syncs
the file and directory. Native-format publication regressions remain covered.
The real Blender consumer checks exported geometry independently.

The [installed smoke](R076c-installed-smoke.json) verifies eight catalogs,
twenty-three contracts, existing OBJ/glTF CLI workflows, desktop/license bytes and
source/overwrite protection. [Source package](R076c-source-package.json): **169
installed inputs** match byte for byte; SHA-256 `c23def8c9c6284cfa6d16b49f909457566d009f2fe2b458865c48ce713833f62`.

Native File-menu and CLI STL workflows follow in R076.d. Remote CI and ordered
merges remain delivery gates; M7 acceptance is not claimed while prerequisites
remain open.
