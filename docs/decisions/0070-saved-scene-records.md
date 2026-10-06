# Immutable selective saved scenes

R063.a, 2026-10-06. This layer establishes records, history and persistence.
Shared authoring commands, scene recall and native controls follow in R063.

Each saved scene has a monotonic document-local ID, a unique case-sensitive name,
a contiguous zero-based order position, and a selective snapshot. At least one of
camera, visibility, style or section must be present. An omitted property is not
controlled by that scene. Names contain 1–1024 UTF-8 bytes, no ASCII control
characters and no leading/trailing ASCII spaces. Documents allow 256 scenes and
charge at most 8 MiB for their records and reference collections.

Camera records use the native Z-up orbit convention: target coordinates in metres,
yaw/pitch in degrees, distance in metres, vertical field of view in degrees and an
orthographic flag. Targets are finite within ±1e9, yaw within ±180, pitch within
±90, distance in [0.05,1e7], and field of view in [5,120]. This layer stores poses;
it does not change ordinary camera navigation or imply an active scene.

Visibility records capture intrinsic body and tag visibility separately from
editor-hidden entities and the editor's show-hidden flag. Inherited visibility is
not flattened into child flags. A hidden reference has a body ID and an explicit
body/face/edge/guide kind; only body references use a null subentity. Each hidden
reference belongs to a captured body visibility scope. Limits are 10,000 bodies,
1,024 tags and 50,000 editor-hidden references per scene. Locks, selection,
materials and component definitions are not captured. Unrecorded bodies/tags are
outside a scene's visibility scope.

Style uses the complete validated model-style value from decision 0068. Section
state is either an explicit disabled clip plane or four plane coefficients with
a unit normal. The equation is in world metres. Coefficients are finite within
±1e9 and squared normal length differs from 1 by no more than 1e-8. A present
section with a null plane means clipping OFF; omitting section leaves it alone.
R064 will add named, context-scoped section-plane entities separately.

New captures and changed snapshots require existing references in the resulting
candidate document. Later geometry/tag deletion does not rewrite saved snapshots.
Missing-reference queries report body, tag and typed geometry references. Renaming
or reordering an existing scene preserves its snapshot, including missing refs.
Updating it is an explicit new capture and revalidates all references. Undo can
restore deleted model references without losing any saved view information.

Create, rename, update, reorder and delete publish atomic labeled edits. Equal
operations are no-ops; reorder must supply every current ID once; delete compacts
remaining positions. Publication deep-copies caller-owned scene records. Undo,
Redo, read snapshots, prepared edits, compound metadata publication, amendment
scope checks and history memory accounting include scenes. Undo never rewinds the
scene allocator. Shared component drafts inherit scenes for context and cannot
modify them. Geometry-only site validation preserves the scene table.

JSON schema 18 adds required root `scenes` and `nextSceneId`. Each scene contains
exactly `id`, `name`, `position` and `snapshot`; snapshot contains only the selected
property objects. IDs are canonical nonzero uint64 decimal strings. Position is an
actual integer JSON number. Visibility flag lists reject duplicate IDs; hidden
lists reject duplicate typed references. All records reject missing/unknown fields,
wrong types and invalid numeric bounds before document publication.

Container manifests require `saved-scenes-v1`, encoding `json-v18`, payload version
18 and an agreeing `nextSceneId` allocator floor. Existing schemas 1–17 migrate to
an empty scene table and allocator 1. The retained `model-style-v17.sketchyup`
fixture is actual R062.b writer output. File save, captured asynchronous saves and
verified recovery preserve scenes, including unresolved references. Older readers
must reject the new required feature rather than discard saved views.

This persistence layer does not complete R063. Its shared command/query path,
selective recall, scene tabs and keyboard/native workflow verification must be
implemented before the roadmap entry is delivered. M7 still requires the M6 gate.
