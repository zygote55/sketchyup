# R034.a: tag records, visibility and persistence

Date: 2026-10-04. Merged in [PR #41](https://github.com/zygote55/sketchyup/pull/41).
Both CI jobs passed for `d51a75a7a2b0e863d9967f1f46f73f5624f7afe8`.
Requires merged [PR #40](https://github.com/zygote55/sketchyup/pull/40).

The document now owns immutable tags and folder records independently of scene
parentage. Untagged is ID zero. Tag/folder edits, assignments and entity renames
use the public command transaction path and one-step undo. Visibility includes
folder and scene ancestry, without replacing geometry records. The viewport
invalidates presentation when tags change, while retaining mesh caches.

Component placement tags are local; canonical member tags propagate through
explicit shared editing scope. Tag-table changes inside shared scope reject.
Merge partitions by inherited tag sets so consolidation cannot discard distinct
visibility assignments. The [record decision](../decisions/0008-tag-records.md)
defines identity, limits, locking, deletion, explode and migration behavior.

Public commands: `tag.create`, `tag.edit`, `tag.delete`, `tag.assign`, and
`scene.rename`. `tags.describe` reports tag hierarchy and effective visibility;
`document.describe` also reports each entity's tag and effective hidden state.
The catalog now has 49 commands. `examples/tags.json` creates a shared panel,
assigns local and shared tags, hides a folder, and saves with schema 9. CI runs
that recipe and queries the reopened model.

Schema 9 adds tags, assignments and the tag allocator. The container retains its
version-2 envelope with required feature `tags-v1` and matching manifest floors.
A checked-in historical schema-8 component container verifies migration without
invented tags or lost component records. Earlier schema fixtures remain intact.

## Validation

- Development: 30/30 suites passed. ASan/UBSan: 24/24 suites passed.
- Tag amendment preserves one-step undo and rejects unrelated tag changes.
  Locked entities reject reassignment while tag visibility preserves their records.

- Core tests cover visibility without geometry/ownership changes, folder reparenting,
  cycle and depth/count limits, duplicate names, used/nonempty deletion, folder
  assignment rejection, immutable snapshots, stale edits and monotonic IDs.
- Component tests cover local placement tags, shared member assignments and scope
  rejection. Merge tests retain separate tag partitions and typed transfers.
- Public-command tests cover discovery, required fields, atomic rollback, read-only
  queries, single-step undo and effective visibility after consolidation.
- Persistence checks cover exact tagged-component round trips, legacy migration,
  missing/duplicate/cyclic records, malformed types and allocator mismatch.
- Native component tests pass on X11 and isolated Weston at DPR 1 and DPR 2.
  Hiding a folder removes its placement from picking, preserves Body pointers,
  and does not rebuild body meshes; showing it restores picking.
- Full native X11 regression passes groups, selection, arrays, transforms,
  navigation, guides, constraints, inference, curves, drawing, numeric entry,
  lifecycle, interaction, viewport, dialogs, responsive shell and application smoke.
- The public tag recipe saves and reopens successfully.

R034 is still open: searchable native hierarchy, tag controls and pointer/keyboard
organization operations follow in R034.b. No independent human acceptance or
physical Hyprland/output-scale acceptance is claimed here.

One initial CI run failed the existing Wayland DPR-2 pointer-scale assertion.
The same test passed four consecutive local runs and the failed CI job passed
on rerun; the other CI job passed without rerun.
