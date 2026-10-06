# Scene authoring, inspection and native recall

R063.b, 2026-10-06. Saved scene records from decision 0070 now have a shared
command/query path and native controls. This contract separates saved content,
persistent model state and the editor's view navigation.

## Shared commands and queries

`saved_scene.create` accepts a name and selective `snapshot` and returns its stable
ID in the batch's `createdScenes` list. `saved_scene.rename`, `.update`, `.reorder`
and `.delete` use inspected IDs. Update replaces the complete property selection;
reorder supplies every current ID once. All use the same core validation and
atomic batch, staging, revision, history and recovery mechanisms as other edits.
The published schemas include complete property shapes, types and numeric bounds;
authoritative validation additionally checks existing references, unique IDs/names,
unit section normals and aggregate storage budgets. Shared component edit scopes
reject saved-scene operations explicitly.

`saved_scene.recall` restores only the scene's opted-in **persistent** model style
and intrinsic body/tag visibility. Decision 0075 adds named section activation to
this persistent recall. Missing IDs are skipped; newly added, uncaptured
entities/tags remain unchanged. Locks, geometry, materials, components and saved
scene records are preserved. All resulting model changes share one Undo step.
A camera-only or already-matching recall has no model changes and follows the
existing batch no-op policy. Native view navigation can still recall it.

Bounded inspection adds `saved_scenes.query` (ordered, paginated summaries),
`saved_scene.describe` (camera/style/section values and visibility counts), and
`saved_scene.visibility` (paginated body/tag flags and typed temporary-hidden
references). Each visibility row reports whether its reference is missing.
Summaries report missing counts and property ownership. Responses use the existing
revision-bound cursors, response budgets and immutable inspection snapshots;
large visibility maps are never embedded in an unbounded describe response.

## Native workflow and history

The Scenes panel supports New, Update, Rename, Recall, Delete and explicit earlier/
later ordering. New/Update let users choose camera, visibility, model style and
section clipping. Checked properties capture the current view; unchecked properties
are omitted. Rename preserves the entire stored snapshot, even when current view
state differs. Dialogs retain drafts and show validation errors; revision/session
stamps reject stale submissions. Identical rename/update values do not create a
history item. Deleting a scene is reversible with Undo.

Scene tabs appear at the viewport's bottom left, with scrolling and keyboard
navigation. Clicking an already-selected tab recalls it again after manual camera
movement. Tabs and panel rows retain stable scene identities across rename, reorder
and Undo. The HUD moves above the tab strip. No scene is implicitly recalled merely
by loading its document or refreshing the list.

Native recall uses the shared model command for persistent style/visibility, then
applies opted-in camera, editor-hidden entities, show-hidden and clip state as view
navigation. Temporary hiding is replaced only inside captured body scopes, preserves
uncaptured hiding and editor locks, and skips missing typed references. Selection
and editing context are pruned using their normal visibility rules. Section OFF is
applied explicitly; an omitted section leaves clipping untouched.

Ordinary navigation does not enter document history. Undoing a scene recall restores
its model changes; it does not rewind camera motion, temporary hiding or free clipping; named section activation is undoable.
Scene creation/update/delete/order/name edits themselves are fully undoable. The
panel states this distinction. Saving preserves authored scene snapshots, not an
implicit last-active view. Missing references remain visible as counts in the panel
and as typed rows in inspection; recall never silently rewrites the saved scene.

Camera recall uses a 160 ms eased transition with shortest-path yaw, logarithmic
distance and an exact final native pose. Manual camera navigation or cancellation
stops the transition. A changed document invalidates a pending transition. View →
Reduced camera motion persists a preference and applies scene cameras immediately.
Projection switches directly. Stored double precision values remain unchanged by
the viewport's existing float camera representation.

## Export boundary

The 160 ms transition is an editor navigation effect, not a saved animation track.
Current GLB/Blender export uses its explicit render settings and immutable model
snapshot; it does not implicitly select a saved scene or export the tab sequence
as camera keyframes. Camera values can be read through `saved_scene.describe` for
an explicit rendering request. Exported camera animation is not supported by this
layer; any future animation export must define timing, projection changes and
selective visibility/style/section transitions before emitting tracks.
