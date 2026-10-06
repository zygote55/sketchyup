# R056.d — Trim, Split and Outer Shell commands

Date: 2026-10-05 UTC. Depends on R056.c. Native controls follow in R056.e;
CI/dependency merges remain pending. [Publication contract](../decisions/0048-split-outer-shell.md).

The shared catalog, CLI/session/MCP schemas and native assistant command policy
expose `geometry.trim`, `geometry.split` and `geometry.outer_shell`. Their
`solidOperations` receipts identify every generated region and source face.
Explicit retention is required; Trim always retains its cutting tool.

`solid_command_tests` checks:

- Overlapping 2 m cubes: Trim returns 4 m³ and preserves the exact cutter;
  Split returns 4/4/4 m³ target/tool/overlap regions; Outer Shell returns 12 m³.
  Both retain and replace policies publish exactly one history item.
- Preview preserves document bytes, identities and history, and predicts the
  exact compact committed receipt. Stale and late-command failures roll back.
- Empty Trim with a replaced target deletes only that target. Retaining it
  rejects as no change. Undo restores consumed operands.
- Missing retention, locked operands and wrong context reject atomically.
- Later face erasure and whole-part deletion prune generated receipts.
- Split inherits each region owner's placement, tag and appearance. All four
  independent operand reflection combinations under an oblique, reflected,
  nonuniform parent produce 5.4 m³ regions. Inverse-transpose world normals
  independently verify every face's physical material-side reversal and source
  color, including the tool-only region.
- Outer Shell fills a 7 m³ hollow target to 8 m³, removes its covered island, and
  retains exactly six exterior source-face records.
- Unique component edits for all three operations preserve the sibling definition,
  resolve consumed source and generated body identities to scene IDs, retain
  canonical component receipts, and keep outputs in the instance context.
- Save/reopen preserves all ordinary and component results exactly; Undo/Redo
  restores operands and all regions together.

The full build and all 74 enabled CTest suites pass in 42.82 s (75 entries; the
opt-in real-Blender test is skipped). The new command suite and existing Boolean
command suite pass ASan, UBSan and
leak detection. The native assistant panel regression passes on X11 DPR 1.
All three published API schemas equal the executable's capabilities.

A temporary-prefix installation executes each installed example using the
installed CLI and reopens the saved native document: Trim leaves two bodies with
one 4 m³ result, Split leaves three 4 m³ results, and Outer Shell leaves one filled
8 m³ result. The latter starts with an enclosed cavity and a covered material
island. CI runs all three examples and the new command sanitizer suite.

No native solid-tool or live-provider acceptance is claimed by this command slice.
