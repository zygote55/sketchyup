# R078.e — Native measured export

2026-10-07. Native contract `404599a`; integration `5c869a1`.
Parent: [PR #187](https://github.com/zygote55/sketchyup/pull/187).
Contract: [0128](../decisions/0128-native-measured-export.md).

Integrated desktop/CLI and affected native targets build successfully. Final measured
CLI, projection, capture, serializer and independent Poppler suites pass **5/5 in 1.77 s**.
Four final native regressions pass on Wayland at 2×: measured export, existing raster
export, annotations and native MCP (**16.739 s** total).
The earlier dedicated measured-export [platform matrix](R078e-platform-matrix.json)
passes all eight normal/sanitized Wayland/X11 1×/2× combinations: **13.693 s normal**,
**17.587 s ASan/UBSan**, with leak detection and halt-on-error. Measured viewport,
dialog, annotation renderer and native fixture sources match the final contract.

File → Export measured PDF/SVG exposes explicit vector/raster content, physical
paper size, print scale, hidden-line choice and raster DPI. Scoped orthographic
capture preserves the editing camera, model bytes, selection and normal renderer
state. Tests measure real reference-image pixel extents, verify annotations and
vector omissions, reject temporary inspection overrides, exercise option/file
cancellation and publish through the native File workflow without overwriting a
source or existing output. The completion sheet was visually reviewed in the
dedicated native matrix and describes scale/content/omissions without protocol fields.

The [installed smoke](R078e-installed-smoke.json) exports both formats using the
installed example and checks source/output protection, eight catalogs, thirty-three
contracts and existing exchange workflows. [Source package](R078e-source-package.json):
**180 installed inputs** match byte for byte; SHA-256
`458d8ef2caf8febae11458bfddee98aaa1d94e516ba5c4f54521cec83de8df53`.
[Final native regressions](R078e-native-matrix.json) retain individual timings.

R078.a–e are locally implemented and verified. Complete remote CI, ordered merges
and M7/M8 milestone acceptance remain required before delivery is accepted.
