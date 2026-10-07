# R081.d — Integrated exchange acceptance study

2026-10-07. Integration `506840a`; study implementation `f93c6a7`.
Parent: [PR #200](https://github.com/zygote55/sketchyup/pull/200).
This is integration evidence and the installed [gate procedure](../M8_ACCEPTANCE.md),
not an accepted M8 milestone.

The final study, discovery, shipped recipes and reference checks pass **4/4 in
15.14 s**. The study also executes with retained artifacts. It verifies the same
2 × 3 × 4 m component through native storage, templates, component bundles, glTF,
OBJ and both STL encodings: 12 triangles, 52 m² surface area and 24 m³ closed-solid
volume after explicit exact STL welding. DXF retains a 10 m footprint perimeter;
orthographic PDF/SVG measures 40 × 60 mm at 1:50. The actual extension worker adds
a 6 m² panel through public commands; undo restores template content, allowing
monotonic revision and allocator counters. Source bytes and history are preserved.

Dedicated ASan/UBSan study evidence remains **1/1 in 1.46 s** with leak detection
and halt-on-error. The final retained PDF was rasterized using Poppler and visually
reviewed: the centered orthographic rectangular outline is intact. Exact synthetic
artifact sizes/hashes are in [this manifest](R081d-study-artifacts.json).
Format-specific independent Blender/ezdxf/Poppler oracles and native matrices remain
required alongside this combined study.

The [installed smoke](R081d-installed-smoke.json) checks the gate procedure, ten
catalogs, forty-five contracts, helpers and exchange workflows. The
[source package](R081d-source-package.json) contains **199 byte-exact installed
inputs**, SHA-256 `68c188da4aee31276f11efac787fd143800ea7ddbdd50c094f7e9d210e91c053`.
The build-configuration correction and full final regression follow. Complete
remote CI, ordered merges, M7 acceptance and all M8 requirements remain necessary.
