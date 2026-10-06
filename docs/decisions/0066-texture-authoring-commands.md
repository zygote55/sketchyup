# Explicit, scoped texture projection commands

R061.f, 2026-10-06. Extends the shared command catalog with
`material.map_texture`. It writes the existing schema-16 face records; no native
format or managed-image representation changes. Native authoring controls remain
the next R061 layer.

Every request explicitly supplies `body`, `face`, `side` (`front`, `back`, `both`),
`space` (`local`, `world`) and `projection`. IDs are canonical nonzero decimal
strings. Local coordinates refer to the geometry body's own frame, not its parent
group or component root. World coordinates include all enclosing placements.
Sides refer to the physical face sides, independently of the camera direction.

| Projection | Required fields | Meaning |
| --- | --- | --- |
| `null` | None | Restore implicit mapping on the chosen side(s). |
| `type: "planar"` | `origin`, `normal`, `tangent`, `width`, `height` | Orthogonal projection with signed repeat sizes in metres. Optional `rotationRadians` and `offset` default to zero. |
| `type: "pins"` | Three `points` and three UV `coordinates` | General affine projection through three non-collinear pins. |
| `type: "affine"` | `origin`, `uGradient`, `vGradient`, `offset` | Explicit repeat gradients in repeats/metre, including shear. |

UV coordinates and offsets are unwrapped repeats with image origin at top left
and positive V downward. Negative repeat size mirrors that image axis. Rotation
uses radians. Planar repeat magnitudes range from 1 micrometre to 1 million metres;
pins, origins, gradients and offsets retain the
[affine kernel bounds](0061-affine-texture-mapping.md). Projected mapping can span
multiple faces by assigning the same world projection to each face in one batch.
The projection plane does not have to coincide with the target face plane.

World projections are constructed in the requested world frame and then converted
to body-local covectors using the inverse placement. This preserves UV evaluation
through reflections, nonuniform scale and shear. An explicit `component.edit`
scope resolves scene body IDs to canonical members while retaining the initiating
instance's world frame. The resulting local projection propagates to the shared
placements. `component.edit_instance` makes the addressed placement unique before
applying the projection. Direct edits to materialized shared members remain
invalid; targeting outside the declared component scope rejects atomically.

Projection records are independent of material and asset identity. A face may be
mapped before an image is assigned, and changing a swatch or replacing its image
does not reset its mapping. No image file, decoder, provider or renderer is invoked
by this command. Existing `asset.import`/`asset.replace` operations manage the
packaged image bytes and resolve missing resources.

`material.sample` with a face ID and bounded `entity.describe` for a face now
return `textureMapping`. It explicitly identifies local metres, gradient units and
UV convention. Each side contains `stored` (an affine record or null) and
`effective` (the authored or derived projection). The bounded entity description
already provides the placement matrices needed to interpret the local frame.
Clients copying a stored/effective record into the command choose `type: "affine"`
explicitly. Body-level material sampling does not invent a face projection.

Normal batch preconditions, immutable preview, modified-face receipts and one-item
Undo apply. A batch with no committed changes retains the existing explicit
rejection behavior and creates no history item. Unknown fields, incomplete or
wrongly typed projections, unstable pins/gradients and out-of-range values reject
without partial publication, including after an earlier valid command in the
same batch.

The command also uses the existing durable transaction and native assistant paths.
Native editor locks reject a mapping-only preview or a commit locked after preview;
retrying an accepted commit does not duplicate history. The bounded schema validator
now checks the JSON Schema `null` type explicitly, so a valid object projection
matches exactly its own branch of the nullable `oneOf`, and other values cannot
masquerade as null. Geometry validation remains authoritative beyond the schema's
structural and numeric checks.

[The packaged example](../../examples/texture-mapping.json) creates a managed RGBA
checker and independently maps its two sides. The existing
[viewport](0065-viewport-textures.md) and [GLB/Blender](0064-textured-glb-export.md)
consumers use the resulting records directly, with their documented preview and
export precision/resource policies.
