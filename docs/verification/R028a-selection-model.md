# R028.a: typed selection state and atomic deletion

Date: 2026-10-03. Selection tests pass in development and ASan/UBSan builds; CI/merge pending.

The selection model distinguishes whole editing contexts, faces, edges and
guides. Replace, add and toggle all use the same existence, visibility, lock and
active-context predicate. Whole-context selections subsume their subentities and
descendants, but cannot include a locked descendant indirectly. Inside a context,
selection addresses its geometry; the container itself is selected from outside.

Face expansion includes every outer and hole boundary. Connected traversal visits
vertices, edges and incident faces once, follows all loops of a face, and excludes
separate islands and guides. Filtering occurs on the final entities, so hidden
geometry cannot leak into the result through connectivity. Keyboard enumeration
uses the same predicate and deterministic typed ID order. Summaries identify the
number and type of selected entities.

Selection, temporary hiding/locking, hidden-geometry mode and the active context
are editor-session state. They do not mutate document bytes, revision, undo or
dirty state and reset on reopen, even when the saved document ID is identical.
Persistent organization/visibility and formal groups/components remain R032 work;
this slice does not claim those features or a native selection UI.

Typed deletion stages all changes and publishes one edit. Whole-context deletion
includes descendants atomically, avoiding missing-parent intermediate states.
Faces are removed before retained boundary edges; edge healing composes sparse
face/edge/vertex lineage back to original identities. Deleting guides preserves
model topology. A deletion is limited to 100 selected subentities, consistent with
the existing bounded command batch; whole-context deletion supports bulk geometry.
Exceeding the subentity limit rejects before mutation. Existing document resource
and history limits still apply.

Fixtures cover typed ID collisions, alias-safe replacement, add/toggle, whole
contexts, holes, disconnected wires, guides, hidden filtering, parent/child locks,
active contexts, keyboard order, stale ID pruning, reopen session reset, unchanged
document state, mixed deletion and exact one-step undo, face-healing lineage,
parent/child deletion, and bounded rejection. Native hover, box/crossing, click
expansion, keyboard feedback and highlighting are R028.b.

```sh
cmake --build --preset dev --target selection_tests
ctest --preset dev -R '^selection$'
cmake --build --preset sanitize --target selection_tests
ctest --preset sanitize -R '^selection$'
```
