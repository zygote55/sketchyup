# R067.c — Shared sun study workflows

2026-10-06. Implementation `5742b99`, integrated acceptance head `70628dd`.
Parent: PR #154. Contract: [0089](../decisions/0089-solar-workflows.md).

The complete Debug build and **128/128 CTest suites pass** in **117.48 s**,
including actual Blender. Five shared command/inspection/staging/dispatch suites
pass ASan/UBSan with leak detection and halt-on-error in **166.78 s**.

`document.solar` requires a complete explicit study. `solar.describe` returns the
saved inputs, resolved UTC instant, deterministic sun position and daylight state
without changing the document. Tests exercise isolated preview, private staging,
one solar change row, revision-guarded publication, Undo/Redo, no-change rejection,
later-command rollback, invalid civil dates and solar-only scene recall.

[Independent JSON Schema validation](R067c-schema-validation.json) checks 12 valid
and 87 invalid command requests across three catalogs, plus three valid and three
invalid inspection requests. Native and headless catalogs agree with the executable
interfaces. [Four native Wayland 2× regressions](R067c-native-matrix.json) pass in
**14.640 s**: desktop inspection, native MCP, render input and history input.

[Installed acceptance](R067c-installed-smoke.json) creates the three-study example
with the same saved model content (new document identity/revision excluded), inspects
exact settings/UTC without mutation, rejects invalid inputs and later batch failures
atomically, checks night lighting state, and recalls morning/noon settings exactly.
Geometry remains unchanged. A relocated standalone CLI authors the same content
without a display or helper. Render inputs and the pending Blender-lighting transfer
report remain exact. Seven catalogs, both examples, contract and desktop match.

[Source-package verification](R067c-source-package.json): **134 installed inputs**
match byte for byte; build and Git artifacts are excluded. Archive SHA-256:
`9a7c7967e9735f54d2f93121880392edf8878b8259b1e14e9e05abe168e73c50`.

Native sun/shadow controls are the next layer. Remote CI and ordered dependency
merges remain delivery gates; Blender lighting conversion belongs to R069.
