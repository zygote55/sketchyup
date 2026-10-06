# R055.b — Shared solid Boolean command

Date: 2026-10-05 UTC. Depends on R055.a; native controls remain outstanding.
[Command, numerical and material contract](../decisions/0046-solid-booleans.md).

`boolean_command_tests` verifies union/subtraction/intersection, explicit operand
retention/consumption, independent native solid classification and analytical
world volume. Preview leaves document bytes, allocator and history unchanged;
its generated geometry/provenance matches commit. Publication creates one history
item. Source colors and front/back materials follow each output face, including
reversed tool faces. Retained source records and selection remain intact;
consumption prunes source selection and Undo restores the exact records.

Stale revisions, late-batch failure, missing retention choice, locked/wrong-context
operands and invalid open solids reject without mutation. Invalid-solid diagnostics
include the operand body and native defect IDs. Empty intersections explicitly
consume both sources only when requested; retained empty results report no change.
Later face deletion prunes generated provenance. Save/reopen and Undo/Redo retain
native topology and appearance.

Nested rotated, mirrored and nonuniform placements produce the expected 5.4 m³
world result in the target's group. A unique component-instance subtraction keeps
the original definition and sibling, consumes only the selected instance's sources,
and maps generated bodies and consumed source-face identities back to scene IDs.
Its preview, native persistence and Undo/Redo pass.

The full development build and all 71 enabled CTest suites pass (72 entries,
41.07 s; opt-in real Blender test skipped). The Boolean command suite also passes
ASan, UBSan and leak detection. The native assistant panel fixture passes on X11
at DPR 1, including setup/consent, preview/direct Undo, clarification, staleness,
layouts and reconciliation. No live-provider quality trial is claimed here.

Published transaction/session/MCP schemas match the live registry. The executable
`examples/solid-boolean.json` subtracts two overlapping 2 m cubes while retaining
them, saves/reopens three bodies, and reports a 4 m³ result with six native polygon
faces and reversed tool-face provenance. CI includes that example and the command
sanitizer suite. Native R055.c interaction and the remaining milestone gate are
still required.
