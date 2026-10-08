# R074.c — Native and standalone CLI glTF import

2026-10-07. Workflow `6b3c4b1`, final core/report corrections `30f3045`,
`a4037e1`, `3b03ce0`, contract clarification `6f6a6a2`; integration `301a382`.
Parent: [PR #170](https://github.com/zygote55/sketchyup/pull/170).
Contract: [0111](../decisions/0111-native-gltf-import-workflow.md).

The complete Debug build passes **147/147 CTest suites in 166.00 s**.
Five final native Wayland 2× integration checks, including native glTF import,
pass in **22.605 s**
([matrix](R074c-native-matrix.json)). Earlier dedicated native acceptance passes
Wayland/X11 at 1×/2× in normal and ASan/UBSan builds
([eight-run platform matrix](R074c-platform-matrix.json)). Final label corrections
also pass both normal and sanitized Wayland 2×
([matrix](R074c-final-label-matrix.json)). Leak detection and halt-on-error remain
enabled. The [final native report](R074c-import-report.png) was visually reviewed.

File → Import GLB/glTF privately validates and converts the source before asking
to replace dirty work. Cancel and malformed input preserve the current model.
Accepted imports use a fresh recovery context, become unsaved native documents,
fit the view and display explicit preservation/loss counts. Native Save selects a
new destination; source assets are not overwritten or linked for later updates.
Imported geometry remains editable with normal undo and native persistence.

Standalone `--import-gltf FILE [--output NEW]` uses the same converter. Without
output it validates and reports only; native publication verifies encode/decode
and uses atomic non-replacing file publication. Existing files, source destinations,
symlinks, invalid data and mixed command modes reject. CLI geometry and native
lifecycle fixtures cover these paths and source preservation. No provider access
or credentials are required for interchange.

The [installed smoke](R074c-installed-smoke.json) verifies import from an installed
GLB export, native validation, source/existing-file protection, mixed and truncated
input rejection, eight catalogs, sixteen contracts and exact desktop/license bytes.
[Source package](R074c-source-package.json): **162 installed inputs** match byte
for byte; SHA-256 `b8e66d5e3eefecd9f86d4b67393036f8d295ade0027c8f6fd4e60e418ad22ee2`.

R074.a–c are locally complete with explicit fidelity/limit contracts. Required
remote CI and ordered merges remain delivery gates. M7 acceptance is not claimed
while its prerequisite PRs remain open.
