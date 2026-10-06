# R064.d — Saved named section activation

2026-10-06, implementation `c0eb098`, integrated head `29030b5`.
[Contract](../decisions/0075-section-scenes.md). A scene captures named activation
as document state; recall restores it with style/visibility in one Undo step.
Free clipping, temporary hiding and camera motion retain view-navigation behavior.

All **116 CTest suites passed in 113.98 seconds**, including real Blender. Five
ASan/UBSan suites passed with leak detection in **3.91 seconds**: saved scene
records/commands/persistence and section records/persistence. Development and
sanitizer builds emitted no warnings. Logs:
`build/r064d-complete-{build,ctest}.log` and
`build/r064d-sanitize-{build,ctest}.log`.

Tests cover selective ownership, empty activation turning cuts off, joint
style/section Undo/Redo, unchanged native geometry, equal recall, immutable saved
IDs, deleted/relocated plane diagnostics, invalid capture, strict decoding and
exact migration from an actual schema-19 writer fixture. Old-version snapshots
reject the new nested activation field.

The native scene workflow passed ASan/UBSan at scales 1 and 2 on both Wayland and
X11: **four checks in 23.827 seconds**. Capture stores active plane IDs, recall
restores them, and one Undo restores prior model properties. Wayland checks use
the isolated client reference fix documented in [the dependency investigation](R062b-wayland-proxy.md);
X11 uses system libraries. Leak detection remains enabled without suppressions.
[Native results](R064d-native-sanitize.json).

Independent Draft 2020-12 validation passed **102 valid commands and 192 invalid
requests** across three command catalogs, plus nine valid and nine invalid scene
queries. The optional activation array is typed and bounded while `plane`
remains required. [Schema results](R064d-schema-validation.json).

Installed checks confirm exact schema-19 migration, named/on/off recall, preserved
snapshots with deleted or relocated references, rejection of invalid new captures,
unchanged geometry, byte-exact relocation and explicit rejection by the installed
schema-19 reader. All seven installed catalogs, contract and desktop binary match
their sources/build outputs. [Installed results](R064d-installed-smoke.json).

All **113 explicit install inputs** match the source archive byte-for-byte, with
no build output or Git metadata. [Package results](R064d-source-package.json).
Remote CI and dependency merges remain required. Native section drawing,
authoring controls and export integration are subsequent R064 work.
