# R033.a: canonical component records, transactions and persistence

Date: 2026-10-04. Merged in [PR #38](https://github.com/zygote55/sketchyup/pull/38);
both CI jobs passed. No component UX gate claimed.
Requires merged R032.c, PR #37. [Record decision](../decisions/0007-component-records.md).

Definitions own canonical immutable geometry in one rooted member hierarchy.
The root is an empty placement frame; member records own the geometry, so
replacement can retain the root ID without reusing unrelated geometry IDs.
Instances bind canonical member IDs to stable resolved scene IDs. A nested
reference is a leaf group node with its own instance binding; canonical reference
cycles, missing nodes, duplicate bindings, unbound children and divergent resolved
geometry reject. Root placement, label, visibility/lock and properties belong to
the placement. Geometry, colors, curves and guides come from the definition.

Definition and instance changes participate in `Document::apply`, undo/redo,
guarded amendment, immutable snapshots and restore. Caller-owned records are
frozen before publication. Geometry/definition/member allocator floors survive
undo, and fresh edits cannot reuse retired identities. Locked instance bindings
and locked descendants cannot be restructured indirectly. Shared geometry edits
must supply a matching definition plus every affected resolved record in one
transaction; an independent edit cannot silently detach or corrupt one instance.

Existing whole-context copies and arrays retain component bindings, including
nested bindings. Compound command/array publication includes component records.
Array count amendment retains its one-undo behavior and shared definition identity.
`document.describe` publishes definition summaries and instance member bindings;
capabilities explicitly reports the remaining shared-edit/UI limitation.
This layer does not yet expose component creation, make-unique, replacement,
local-axis operations or shared-edit commands; those are R033.b. Native editing
scope and the persistent definition banner are R033.c. The R033 gate remains open.

Schema 8 and container feature `components-v1` persist definitions, nested
references, placed member bindings and allocator floors. Versions 1–7 migrate with
empty component tables. A historical v7 colored-consolidation file covers the
latest migration; prior fixtures remain in the persistence suite. Container
metadata must agree with canonical definition/member allocator floors.

The current resolved scene stores materialized immutable Body records, checked
against canonical definitions. This duplicates mesh data; it is not the later
instance-rendering optimization. Canonical stored geometry and expanded scene
geometry each obey the existing editing budgets. Definitions are capped at 1024;
reference expansion is preflighted against 10,000 records, 100,000 vertices/faces/
wires, 300,000 edges, 10,000 curves/guides and 128 hierarchy levels. Existing file
and history byte limits still apply.

## Validation

- Development: 27/27 suites passed. ASan/UBSan: 22/22 suites passed.
- Final binding-only amendment fix: component records, amendment, command and
  persistence suites passed; component/amendment sanitizer checks passed.
- Core coverage includes mirrored/nonuniform instances, nested bindings, cycles,
  exponential/deep expansion rejection, snapshot freezing, atomic divergence
  rejection, locked binding protection, shared commit/undo/redo, retired IDs,
  component copies/arrays, count amendment and binding-only amendment.
- Persistence covers exact component container round trips, historical v7
  migration, malformed tables, duplicate/dangling references, mismatched geometry
  and allocator manifest disagreement. The colored-explode recipe saves/reopens.
- Public command tests exercise component array preview, publication, undo and
  read-only definition/binding inspection through the existing command/query API.
- Full native X11 regression passes: groups, selection, arrays, transforms,
  navigation, guides, constraints, inference, curves, drawing, numeric entry,
  tool lifecycle, interaction, viewport, dialogs, responsive shell and smoke.
- Isolated Weston Wayland group/array/smoke checks pass at DPR 2, with zero GL
  errors. These verify existing native workflows against the record foundation;
  they do not claim the pending native component editing workflow.
- No independent human component acceptance is claimed.
