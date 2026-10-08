# R077.c — Planar DXF export

2026-10-07. Export/reference contract `37a8f21`; integration `1b97d9f`.
Parent: [PR #181](https://github.com/zygote55/sketchyup/pull/181).
Contract: [0122](../decisions/0122-planar-dxf-export.md).

The complete Debug build passes **162/162 CTest suites in 190.75 s**, including
actual Blender interchange and the independent ezdxf export consumer. Dedicated
export/reference checks pass **2/2 in 0.85 s** and ASan/UBSan export **1/1 in
0.32 s**, with leak detection and halt-on-error. Five final native Wayland 2×
checks pass in **22.745 s** ([matrix](R077c-native-matrix.json)).

The exporter produces bounded AC1032 ASCII DXF with explicit units and flat layers.
World-XY lines and connected edges become lines/polylines; conformally transformed
native circular curves retain analytic arcs/circles. Reflected arcs preserve DXF
orientation; noncircular transforms fall back to disclosed chords. Nonplanar input
rejects. Faces become unfilled contours, hidden geometry is included and sections
are not applied. Native source bytes/history and existing destinations are protected.

The independent ezdxf 1.4.4 consumer verifies inventory, drawing units, layers,
hidden state, 20/180-degree arcs, circle radius and full drawing extents. CI installs
its pinned test-only dependencies in a build venv; they are not runtime dependencies.

The [installed smoke](R077c-installed-smoke.json) verifies eight catalogs,
twenty-seven contracts and existing STL/OBJ/glTF workflows. DXF export is exercised
by its dedicated core/reference suites; the native/CLI workflow follows in R077.d.
[Source package](R077c-source-package.json): **173 installed inputs** match byte for
byte; SHA-256 `1f37b725bb957ec271b209b9842140d06ce760989ff96ad28aef8fe088ad0885`.

Remote CI and ordered merges remain delivery gates; M7/M8 acceptance is not claimed.
