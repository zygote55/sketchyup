# R032.a: persistent groups and hierarchy operations

Date: 2026-10-04. Merged in [PR #35](https://github.com/zygote55/sketchyup/pull/35); both CI jobs passed (16m33s and 12m17s). Merge `fd1ffb34101ca2541428181d3e7d9c665ef4d06a`.

Groups are explicit scene records. They contain existing geometry contexts and
nested groups, and can also own geometry drawn directly in the group's local
frame. Creating a group from sibling contexts retains their geometry IDs, names,
colors, guides, curves and transforms. This preserves the existing per-context
material model while later material work adds face-level assignments.

`group.create` accepts sibling context IDs; `group.explode` removes one group
boundary and promotes its direct children with composed transforms.
`scene.reparent` preserves world placement and rejects cycles or destinations
other than groups/the model. Exploding a group that owns raw geometry retains
that record and its stable subentity IDs as an ordinary geometry context.

`scene.state` stores explicit boolean visibility/lock flags. Authoritative edit
validation rejects changes to locked contents and indirect transform, reparent,
kind or deletion changes above a locked descendant. Changing only state flags
remains available for reveal/unlock. Undo/redo restores the complete operation.
Unlocking and then editing an initially locked entity requires separate committed
batches; a combined batch cannot bypass the original lock precondition.

Selection recognizes group boundaries and inherited visibility/locks. Outside
an active group, its raw contents cannot be selected as faces, edges or guides.
Nested groups retain their own boundary until entered. Older explicit raw-context
editing continues to isolate exactly that raw context.

Raw document schema 6 and the `groups-v1` container requirement preserve typed
records and state flags. Readers retain v1–v5 migration, and old contexts remain
ordinary geometry with no invented groups, locks or hidden state. Older writers
will reject the new required feature instead of silently losing grouping data.

This is the core/persistence layer of R032. Native Make Group/Explode actions,
raw subentity grouping, group picking, context-aware drawing/inference, dimming
and breadcrumb workflow validation remain R032.b work. No R032 gate is claimed.

Validation evidence:

- All 24 development suites passed; the command catalog has executable required-field,
  unknown-field, commit and undo coverage for all 35 public commands.
- All 19 ASan/UBSan suites passed. The final lock-policy refinement was rechecked
  under sanitizers in groups, selection, amendment, core and scene suites.
- Core fixtures cover nested boundaries, keyboard traversal, mirrored member
  geometry, rotated parent transforms, reparent/explode placement, curve/guide
  and color retention, atomic rejection, lock inheritance and one-step undo.
- Persistence tests reopen schema-6 groups exactly, reject malformed kind/state
  fields and migrate a committed schema-5 guide container plus v1–v4 fixtures.
- The full native X11 regression set passes. Selection tests at DPR 1 and 2 now
  verify persistent hide/lock refresh without camera movement, protected raw
  selection outside a group and restored selection after opening that group.
- The same selection tests pass on isolated Weston Wayland at DPR 1 and 2. The
  DPR-2 rendering smoke test reports `glError: 0`. CI repeats these checks.
- `examples/groups.json` creates, transforms, nests and locks a shelf assembly
  in one batch, saves it and reopens the same hierarchy and world placement.
- A separate public-command fixture rejects an unlock-and-edit batch targeting
  an originally locked group, leaving its serialized document unchanged.

These are implementation-agent checks on synthetic fixtures. Physical Hyprland
and independent human acceptance remain milestone-checkpoint work.
