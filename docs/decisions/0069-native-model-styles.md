# Native display styles preserve model records and selection policy

R062.b, 2026-10-06. Uses the document-owned
[model style record](0068-model-styles.md).

Textured mode draws material tint and image RGB. Shaded mode draws material tint
without image RGB. Monochrome substitutes the style's independent front/back
colors while preserving physical sides under mirrored placement. Shaded and
monochrome retain material opacity and texture alpha cutouts. X-ray draws material
tint with opacity multiplied by the style opacity, retaining image alpha. It uses
the existing transparent-triangle sorting path. Wireframe omits face fills and
keeps eligible edges visible even when the general Edges control is off.

Display modes do not mutate geometry, materials, image assets, face projections,
selection, hidden/locked state or component ownership. CPU and GPU selection retain
the same face-depth and alpha-cutout policy in every mode, including wireframe and
X-ray. A display mode is not permission to select through opaque faces or locked
contexts. Style changes rebuild appearance buffers without retriangulating bodies.
Image loading, precision fallback and GPU context ownership retain their existing
contracts. Model style does not change GLB/Blender material export.

The model background determines canvas and annotation contrast colors, independent
of application theme. Grid and world axes have independent visibility controls.
Ground is a finite visual reference plane surrounding the camera target at the
stored metric height. It writes no depth, creates no geometry and never blocks
selection. Its size follows camera distance. It is not a physical ground object.

Profiles mark open face boundaries and view-facing transitions across incident
faces, including nonmanifold incidence. Loose wires remain ordinary edges.
Softened edges may form profiles; explicit edge/body/tag hiding remains respected.
Invisible material sides do not contribute profile incidence. Profiles outline
geometric boundaries, not individual image-alpha cutout pixels. Clipped segments
expand into screen-space triangles with the requested logical-pixel width, so they
do not depend on implementation-specific OpenGL line widths. Camera/projection,
style, document and context changes invalidate the appropriate profile buffer.
Profile and grid/ground buffers belong to the current GL context. Selection adds
black/white borders around its colored edge overlay for contrast against arbitrary
model colors. Benchmark/smoke JSON records the complete model style and visible
profile-edge count; synthetic triangle throughput still identifies itself as such.

View → Model styles opens a Styles tab in the model panel. Mode and visibility
controls publish individual atomic edits. Colors and details opens a native modal
draft for mode, five colors, ground height, profile width and X-ray opacity. Colors
use the Qt color chooser; Cancel discards the draft. Ground height accepts document
units and explicit length suffixes. Scalar values use the locale. Untouched fields
retain original doubles instead of writing rounded display text back to the model.
Validation errors remain inline with the draft intact. Publication requires the
same document state and revision as when the dialog opened, then commits one Undo
entry. Reset is one undoable edit, and an already-default reset is a no-op.

Application-theme actions affect chrome independently of model style. Native
controls follow document Undo/Redo and replacement through normal window refresh.
No credentials or remote provider connection is involved in style editing.
