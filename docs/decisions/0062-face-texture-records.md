# Face texture projections are immutable, independent side assignments

R061.b, 2026-10-06. Builds on the
[affine projection kernel](0061-affine-texture-mapping.md).

Each geometry record may store a sparse mapping from face identity to independently
optional front/back projections. A present side is an explicit body-local affine
projection. An absent side requests implicit mapping; it does not borrow the other
side's projection. Empty pairs are omitted. The projection can be authored before
an image is bound and remains independent of material identity. Changing a swatch
or resolving its missing image retains the face placement. Legacy color painting
clears named assignments and projections together; Undo restores both.

All records participate in body equality, immutable snapshots, shared component
definitions, atomic edits and history. Mapping-only edits report changed face IDs.
Snapshot/history byte estimates include dynamic projection records. Publication
validates existing face references, nonempty pairs and the kernel's finite/bounded
numeric rules, including unplaced canonical component definitions. No invalid
projection can be silently discarded to make an edit succeed.

Face lineage carries both sides through splits and prunes deleted faces. Healing
a boundary between unequal mappings rejects, including an explicit-versus-implicit
pair. Copied geometry remaps face IDs. Grouping preserves member-local mappings;
consolidation re-expresses source projections in destination coordinates with the
inverse transpose. Complete-face raw transforms preserve UV at the transformed
points, including shear and reflection. A partial vertex edit keeps the projection
fixed in the body's local frame, so the changed point samples that projection.
Body/instance placement and component-axis changes retain member-local records.

Explicit face reversal swaps front/back projections with the material sides.
Booleans use each result face's source operand/face and reversal provenance, then
convert its projection into the output body's frame. Hosted regeneration retains
the current face's projection, gives newly created reveals the entry projection,
and removes mappings for retired reveals. Push/pull, extrusion and profile sweep
carry the driving projection to generated faces. These are projected mappings:
a generated wall parallel to the projection direction collapses in UV. This layer
does not invent an unwrap around a sweep or extrusion; callers can assign a new
projection to those walls when the authoring controls are connected.

JSON schema 16 adds required `faceTextureMappings` objects to scene and canonical
body records. Each face key stores a two-element array of nullable front/back
objects containing `origin`, `uGradient`, `vGradient` and `offset`. All numeric
values preserve doubles. Unknown fields, malformed pairs, missing fields, bad
identities and invalid gradients reject. Container manifests require
`texture-mapping-v1` and `json-v16`; older feature/schema combinations retain their
strict decoding. Schemas 1–15 migrate with no explicit mappings. The v15 hosted
container fixture retains actual prior-writer bytes.

The existing managed asset table continues to own image bytes and explicit missing
states. No external image path is loaded by a document. Relocating a packaged
document preserves asset bytes, swatch opacity and both mappings together.

This layer adds storage and modeling semantics. Shared texture-authoring commands,
native controls, bounded image decoding, textured rendering and GLB texture export
remain separate R061 work. M7 delivery still requires the M6 gate to close.
