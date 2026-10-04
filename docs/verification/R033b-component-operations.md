# R033.b: component operations and explicit shared mutation

Date: 2026-10-04. Merged in [PR #39](https://github.com/zygote55/sketchyup/pull/39);
both CI jobs passed. Requires merged
[PR #38](https://github.com/zygote55/sketchyup/pull/38). The R033 native UX gate
remains open until R033.c.

## Command contract

- `component.create` accepts a geometry or group `body` and optional definition
  `name`. It preserves the placement root ID and pose, moving root-owned geometry
  into a fresh member context. Typed `transfers` report that move.
- `component.instance` accepts `definition`, a 16-number column-major affine
  `matrix`, optional group `parent` and optional placement `name`. The matrix is
  local to the parent; mirrored and nonuniform placements are supported.
- `component.make_unique` accepts an instance `body`. It preserves every scene
  body ID and geometry value, cloning the selected definition and any ancestor
  definitions necessary to isolate a nested placement. Other references within
  those definitions retain sharing. Peer placements remain unchanged.
- `component.replace` accepts instance `body` and target `definition`. It retains
  the root ID and placement state while retiring the previous member contexts and
  allocating fresh ones. Nested replacement isolates the ancestor ownership path.
- `component.axes` accepts `definition` and a local affine `matrix`. It compensates
  member transforms and all referring placements, preserving world geometry.
- `component.edit` accepts `definition` and 1–100 ordinary public `commands`.
  Those commands run privately with canonical member IDs and definition-local
  coordinates. Their `world` space means this definition's coordinate frame.
  Existing nested references may be moved, replaced, exploded or deleted; their
  internal geometry requires its own explicit definition edit. Recursive
  `component.edit` and nested `component.axes` are rejected. Creating new root
  records attaches them to this definition; existing members cannot escape it.
- `component.inspect` is a read-only query accepting `definition`; it returns
  canonical member records, references and the affected resolved instance roots.
  `document.describe` provides each placement's canonical-to-scene member map.

Definition IDs, canonical member IDs and scene body IDs are separate namespaces.
All identifiers remain canonical decimal strings. Component command results add
`createdDefinitions` and `componentOperations`; shared edits report any root
geometry normalization through `normalizedMembers` using canonical member IDs.
The ordinary `changes` report describes resolved scene geometry. Split lineage
is propagated to each affected placement. Existing definition root metadata and
placement visibility/lock state are outside the shared geometry edit scope.

Shared publication validates the complete definition DAG and preflights expanded
scene budgets before allocating placements. A locked affected instance or locked
member rejects the entire edit. Mutating resolved member geometry without an
explicit shared scope rejects. Preview and commit use the same implementation,
and each public batch produces one atomic undo item for canonical and placed data.
A failed inner command publishes nothing. Whole-instance copy/array retains
sharing. Explode removes the selected instance binding and promotes its geometry;
typed recursive deletion removes all deleted bindings. Unused definitions remain
available for later placement; automatic purging is not introduced here.

The resolved scene continues to materialize geometry per placement. This layer
adds no instanced-rendering optimization and does not change schema 8. Native
component creation/editing controls and persistent scope feedback are R033.c.

## Validation

- Development: 28/28 suites passed. ASan/UBSan: 23/23 suites passed.
- Final shared numeric-amendment command regression passed; amendment updates
  both instances and retains one-undo behavior.
- Native X11 group, array, selection and transform regressions passed at DPR 1;
  the application smoke test passed with zero GL errors.
- Isolated Weston Wayland group regression passed at DPR 2. This verifies existing
  group interaction against component support, not the pending component UI.
- CLI component recipe saved and reopened successfully.

Targeted component and public command suites pass. Coverage includes creation,
mirrored/nonuniform placements, stable IDs, nested make-unique/replacement,
shared topology lineage, locked peers, shared insertion/explode/deletion,
world-preserving axes, cycles, command preview, undo/redo and container round trips.
`examples/components.json` exercises creation, repeated placement, shared
push/pull, make-unique, independent appearance, axes and replacement through the
public CLI; CI saves and reopens the resulting document.
