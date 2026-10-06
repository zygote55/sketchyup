# Typed reference image entities and persistence

R068.b, 2026-10-06. A `reference_image` body owns the record from contract 0091
and an ordinary affine placement. It is a whole selectable entity, distinct from
modeled geometry: no surface vertices/faces/wires, topology, curves, guides,
face materials or editable text may coexist with its image record. It has no
editable geometry context and cannot own children. Names, properties, tags,
visibility, locks and group ancestry retain their ordinary entity semantics.

Bounds include its four placed corners, including when measuring a containing
group. Geometry counts, surface area and solid measurements remain empty. Whole
copies and component placements preserve the typed record. Component normalization
moves a reference root into an image child beneath a separate placement frame;
shared definition edits propagate that child's settings to each placement.

The image identifies an existing managed asset. Original embedded bytes share the
existing per-asset and document budgets and move with native files. Missing payloads
remain explicit records and retain image dimensions/placement; there is no implicit
filesystem or network resolution. Deleting a referenced asset rejects, including
references retained by component definitions. IO remains responsible for decoding
pixels and reporting unsupported or invalid image content.

Creation, settings changes and anchored calibration use the ordinary atomic edit
path. Calibration publishes the new dimensions and compensated translation together.
Immutable snapshots, prepared edits, stale guards, locks, Undo/Redo, durable recovery,
grouping and component edits retain image state. Display units do not rescale it.
All placed corners obey the document's world-coordinate bounds.

Schema 24 stores an optional strict four-field `referenceImage` body record and an
explicit `reference_image` kind. They must occur together. The container requires
`reference-images-v1` with `json-v24`, so older readers reject rather than silently
dropping the reference. Versions 1–23 migrate without invented image records; an
actual schema-23 sun-study fixture retains its solar settings and scenes.

Surface-only GLB handoff omits image planes and reports `referenceImagesOmitted`.
A reference-only model has no surface geometry to export through that route.
Shared image authoring, native display/calibration and raster export follow in
subsequent R068 layers.
