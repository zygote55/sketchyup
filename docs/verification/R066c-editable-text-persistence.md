# R066.c — Editable text persistence

2026-10-06. Implementation `cea48eb`, integrated acceptance head `f63cef8`.
Parent: PR #149. Contract: [0084](../decisions/0084-editable-text-persistence.md).

The complete Debug build and **124/124 CTest suites pass** in **122.94 s**,
including actual Blender. Nine persistence/worker regression suites pass ASan/UBSan
with leak detection and halt-on-error in **10.09 s**.

Typed source records retain Unicode text, requested and resolved font settings,
font fingerprints and a geometry digest alongside native cached geometry. Dedicated
fixtures cover strict record validation, immutable snapshots, Undo/Redo, recovery,
component normalization and placement, whole-body copies and baked partial extraction.
An actual schema-21 file migrates with its existing annotations intact. Schema 22
requires `editable-text-v1`; older readers reject it instead of dropping source.

[Four native Wayland 2× regressions](R066c-native-matrix.json) pass in **16.471 s**:
desktop inspection, native MCP, render input and history input.
[Installed acceptance](R066c-installed-smoke.json) verifies exact migration,
multiline Unicode round trips, retained cached geometry without the original font
or helper, relocation, units changes, older-reader rejection and seven catalogs.
Surface exports retain geometry and report omitted editable source metadata.

[Source-package verification](R066c-source-package.json): **125 installed inputs**
match byte for byte; build and Git artifacts are excluded. Archive SHA-256:
`e1529b06d76cf23e48cf9bcf08adbe5b149741822e20a4a09892f24e42336e72`.

A subsequent parent-history reconciliation changed no files. Shared authoring
commands and the native editor follow in separate layers. Remote CI and ordered
dependency merges remain delivery gates.
