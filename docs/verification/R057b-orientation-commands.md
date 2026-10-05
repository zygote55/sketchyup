# R057.b — Orientation commands preserve physical material sides

Date: 2026-10-05 UTC. Depends on R057.a. Native orientation controls and exporter
front/back handling follow; this does not complete R057 or M6.
[Command contract](../decisions/0049-face-orientation.md).

`geometry.reverse_faces` and `geometry.orient_faces` publish through the shared
command catalog, CLI/session/MCP schemas and assistant command policy. Reverse
accepts explicit face references in one context; Orient follows an explicit
reference face. Changed faces swap front/back assignments so their physical sides
keep their original appearance, including material-zero color fallback. Default
body appearance stays intact and redundant face overrides are removed.

`orientation_command_tests` passes normally and with ASan, UBSan and leak detection:

- Preview preserves document bytes, allocators and history, predicts exact change
  maps, and publishes one Undo item. Stable selected face IDs survive reversal.
- Independent inverse-transpose world normals identify changed physical sides
  under oblique, reflected and nonuniform placements. Material ID, color and
  opacity on each physical side stay unchanged, including legacy fallback.
- Vertices, topology/allocators, body defaults, per-face colors, placements, tags,
  properties and parent records stay intact. A circular face retains exact
  analytic curve bindings after reversal.
- Orient repairs a reversed face from an unchanged reference and restores the
  exact original body and solid classification. Consistent components reject as
  no-change without creating history.
- Missing faces, duplicate references, wrong contexts, locked bodies, stale
  revisions and late failures reject without partial publication.
- Multi-body reversal is one edit and one Undo. Save/reopen, Undo and Redo retain
  face winding and all side assignments.
- A reflected unique-component edit preserves the sibling definition. Scoped
  Orient resolves the reference face, repairs the intended member and survives
  persistence and Undo/Redo.

The full build and all 76 enabled CTest suites pass in 43.57 s (77 entries; the
opt-in real-Blender suite is skipped). Native assistant panel acceptance passes
on X11 DPR 1. The all-published-command fixture exercises both new handlers. Published schemas
match runtime capabilities. A temporary-prefix installation executes the
installed orientation example with the installed CLI and reopens the saved
six-face body. CI runs that example and the new command sanitizer suite.

The existing GLB back-material loss remains explicit pending its R057 export
layer; these tests establish native document material semantics. No native
orientation UI or live-provider acceptance is claimed here.
