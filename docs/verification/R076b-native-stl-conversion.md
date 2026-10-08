# R076.b — Native STL conversion and explicit repair

2026-10-07. Conversion `7a7e534`, independent Blender producer `fffea1a`,
contract `682b5a4`; final integration `a1e28d9`.
Parent: [PR #176](https://github.com/zygote55/sketchyup/pull/176).
Contract: [0117](../decisions/0117-native-stl-conversion.md).

The complete Debug build passes **156/156 CTest suites in 188.46 s**, including
actual Blender interchange. Five final native Wayland 2× integration checks pass
in **22.373 s** ([matrix](R076b-native-matrix.json)).
Dedicated native conversion and actual Blender producer checks pass **2/2 in
1.27 s**; ASan/UBSan conversion passes **1/1 in 0.25 s**, with leak detection and
halt-on-error enabled.

Conversion creates editable native bodies per source solid in one undoable edit.
Units and up axis are explicit. Vertex welding is an explicit choice of none,
exact or bounded tolerance; degenerate-facet removal is separately opt-in. Repair
scope and resulting open, inconsistently oriented or volumetric topology are
reported. Source geometry and history remain unchanged by rejected conversion.
The real Blender producer verifies both ASCII and binary encodings, transformed
bounds and reflected orientation/volume behavior.

The [installed smoke](R076b-installed-smoke.json) verifies eight catalogs,
twenty-two contracts, existing OBJ/glTF CLI workflows, desktop/license bytes and
source/overwrite protection. [Source package](R076b-source-package.json): **168
installed inputs** match byte for byte; SHA-256 `04436bf09c1aa11e8a49b0cae02c4b684e69d0340678d7bf56040b2c02b69b91`.

STL export and File-menu/CLI workflows remain subsequent layers. Remote CI and
ordered merges remain delivery gates; M7 acceptance is not claimed while
prerequisite PRs remain open.
