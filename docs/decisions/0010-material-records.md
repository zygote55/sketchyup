# Material identity and oriented face assignments

R036.a, 2026-10-04.

Materials are document-owned immutable swatches with stable monotonic IDs, a
unique nonempty name, numeric RGB components in [0, 1], and opacity in
[0, 1]. The RGB values preserve the existing color convention; color-management
and texture mapping are later work. At most 1,024 swatches and 1,024 bytes per
name are accepted. Material creation, replacement and deletion use the same
atomic edit/history machinery as geometry, definitions and tags. Undo never
reuses a retired ID. Publication copies caller-owned mutable records.

Each geometry record has a pair of default material IDs and sparse per-face
pairs. Front follows the oriented face boundary; back is its opposite. An absent
face override uses the record's pair. Within an explicit pair, zero means the
existing legacy face/body color at full opacity, rather than the other side's
material. This preserves older documents exactly without inventing swatches.
Assignments describe the record's own geometry; they do not introduce implicit
ancestor material inheritance.

`material.assign` accepts one face or all of a record's own faces, with a front,
back or both-side selector. A whole-record assignment changes that side of the
default and every explicit face pair while retaining the other side. Empty
redundant overrides are removed. `material.color` continues to replace the
record's own appearance, clearing named assignments along with legacy face
colors; undo restores all of them.

Face lineage transfers both sides through splits, extrusion, push/pull, raw
copies and grouping. Consolidation transfers the effective pairs before welding,
including mirrored coordinate frames. Removing a boundary between different
material pairs rejects instead of silently discarding one assignment. Material
identity matters even if two swatches currently have the same appearance.

Assignments inside component members belong to the shared definition. Explicit
shared edit scope publishes them to instances; make-unique isolates subsequent
assignment edits. The document swatch table is global, so creating/editing/deleting
swatches inside a component geometry scope rejects. Swatch edits intentionally
change that swatch's appearance throughout the document, including its uses in
locked geometry; assignment changes still obey entity/ancestor locks. Used
materials cannot be deleted, including uses in unplaced definitions.

JSON schema 10 adds the swatch table, its allocator floor and front/back pairs to
scene and canonical component records. The container requires `materials-v1`
and `json-v10`, and validates the material allocator against the payload. Earlier
schemas retain their exact color fallback. The historical version-9 tag fixture
was written by the previous reader/writer and is retained for migration testing.

Managed image assets, missing-asset records and packaged asset manifests follow
in R036.b. Native swatches, painting/sampling and front/back opacity rendering
follow in R036.c. R036 is incomplete until those layers pass their checks.
