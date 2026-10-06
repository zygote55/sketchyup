# R067.b — Sun study persistence

2026-10-06. Implementation `8c61007`, integrated acceptance head `4abd0ed`.
Parent: PR #153. Contract: [0088](../decisions/0088-solar-study-records.md).

The complete Debug build succeeds. All **127 CTest suites pass** across the full
run (**119.07 s**) and a **0.42 s** rerun of three corrected legacy migration
fixtures. Those fixtures now omit solar when declaring a pre-solar schema; the
remaining 124 suites passed unchanged, including actual Blender. Twelve related
suites pass ASan/UBSan with leak detection and halt-on-error in **11.98 s**.

Typed settings participate in immutable snapshots, atomic history, Undo/Redo,
prepared edits, durable recovery, revision guards and component scope checks.
Solar-only saved scenes recall exact settings and Undo restores the prior study.
Schema 23 requires `solar-study-v1`; an actual schema-22 editable-text file migrates
without changing geometry or source records. Strict codecs reject missing/extra
fields, invalid dates, numeric types and out-of-range values before restoration.

[Four native Wayland 2× regressions](R067b-native-matrix.json) pass in **14.741 s**:
desktop inspection, native MCP, render input and history input.
[Installed acceptance](R067b-installed-smoke.json) verifies exact migration,
settings and scene round trips, standalone CLI relocation, units preservation,
older-reader rejection, seven catalogs and exact installed desktop/contract files.
Render snapshots freeze the explicit inputs and computed UTC position. Blender's
unapplied lighting is reported explicitly pending R069.

[Source-package verification](R067b-source-package.json): **131 installed inputs**
match byte for byte; build and Git artifacts are excluded. Archive SHA-256:
`34d42d39971edfc95692284cc058b8edb04cbf16ab3b1bbf8e7fd4cc737b8afe`.

Shared authoring and native sunlight/shadow controls follow in the next layers.
Remote CI and ordered dependency merges remain delivery gates.
