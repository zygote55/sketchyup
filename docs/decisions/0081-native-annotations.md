# Native associative dimensions and labels

R065.d, 2026-10-06. View → Dimensions and labels opens the Organization tray's
Annotations tab. It lists persistent records and explicit missing/ambiguous states.
Dimension, Label, Edit / rebind, Delete and Frame are keyboard-accessible actions.
Every authoring change uses the shared annotation commands and document history.

A dimension on one selected edge attaches to its two stable endpoints. A label on
an edge attaches to its midpoint. A face attachment chooses the centroid of its
first native triangulation triangle, guaranteeing an interior surface point even
for holed/concave faces. Two selected edges/faces create a distance between these
attachment points. The editor also supports explicit fixed world points, which do
not follow geometry. These attachment rules are stated in the editor. Body/group
selection is insufficient: enter its context and select the edge or face.

Editing defaults to keeping current attachments, including broken references.
Rebinding to selected geometry or fixed points is explicit. Names, multiline text,
leader/extension lines, RGB color, logical-pixel text size and world-space text
offset are editable. Coordinates and offsets accept document units or explicit
unit suffixes. Unchanged fields preserve original floating-point values. Invalid
input remains in the dialog with an inline error; stale drafts cannot overwrite a
changed document. Closing/canceling is nonmutating; each Save is one undoable edit.

The viewport draws screen-facing dimension text, dimension lines, arrowheads,
extensions and label leaders over the model. Font sizes and strokes use logical
pixels. Distance formatting follows document display units and evaluates world
geometry, including nonuniform scale/reflection. Broken references draw red crosses
and explicit state text without a stale numeric distance. The tray remains usable
for locating and rebinding missing geometry. Frame includes attachment and text
positions. Annotations are overlays, do not become pickable model faces, and are
omitted from surface-only GLB/Blender output with the existing explicit loss report.

Resolved attachments to hidden or section-clipped geometry suppress their overlay;
broken attachments remain visible at their last known positions. Ordinary solid
occlusion does not hide annotation overlays. Behind-camera/near/far-clipped points
are suppressed. Evaluation is cached per document snapshot; camera movement only
reprojects the cached measurement. Annotation edits do not change native meshes.
