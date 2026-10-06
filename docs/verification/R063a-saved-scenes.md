# R063.a — Saved scene records and persistence

2026-10-06, implementation commit `1148dcf`. This layer prepares selective scene
records, atomic editing/history and schema-18 persistence. Shared commands, view
recall and native controls are being implemented separately; R063 and M7 remain
undelivered while their remaining work and the M6 gate are pending.

Contract: [Immutable selective saved scenes](../decisions/0070-saved-scene-records.md).

Validation on Arch Linux, Qt 6.11.2 and GCC 16.2:

- All **111 CTest suites passed in 109.93 seconds**, including the real Blender
  smoke test. Build/test logs: `build/r063a-complete-{build,ctest}.log`.
- Seven targeted ASan/UBSan suites passed in **18.74 seconds**, with leak detection
  enabled and no suppressions: saved scenes, scene IO, model styles, style IO,
  persistence, staging and transaction coordination. Logs:
  `build/r063a-sanitize-{build,ctest}.log`.
- Core tests exercise immutable caller aliases, read/prepared snapshots, saved-state
  Undo/Redo, monotonic IDs across branches, exact scene order and delete/undo,
  no-op edits, failed edits, amendment scope, shared component boundaries, compound
  publication, invalid camera/section/visibility values, count and memory budgets.
- Persistence tests cover every one of the 15 nonempty selective-property masks,
  explicit section OFF, exact container/raw round trips, asynchronous save snapshots,
  recovery and retained missing geometry/tag references. Malformed records and
  mismatched container feature/encoding/version/allocator floors reject atomically.
- An actual version-17 styled model is retained unchanged. Migration adds only
  version 18, empty scenes and allocator 1, preserving its previous document fields.
- Installed CLI checks run without display variables, across three retained v16
  textured files and the v17 styled model. Each gets all 15 property masks, a
  section-OFF scene and a scene with missing typed references. IDs above 2^53,
  retired floors, ordering and all properties survive relocation exactly. Repeated
  serialization is byte stable; GLB output is unchanged; the retained v17 reader
  rejects the new format. [Installed evidence](R063a-installed-smoke.json).
- The source archive includes all **106 explicit install inputs byte-for-byte** and
  excludes build output and Git metadata. [Archive evidence](R063a-source-package.json).
- No compiler warnings were emitted by the complete or sanitizer builds.

All four native checks passed on Wayland at scale 2 in 15.428 seconds: style input,
selection input, assistant preview and native MCP. They use the isolated Weston
test container and software Mesa;
they do not change the user's running application, preferences or credentials.
[Native check results](R063a-native-matrix.json).
