# R024.a: persistent curve parameters and commands

Date: 2026-10-03. Local verification passed; CI/merge pending. This is the first
child of R024. Native arc/pie interaction remains R024.b; R024 is not complete.

The core stores circle, arc and pie records alongside editable planar topology.
A record contains center, affine parameter axes, radius, start/sweep angle in
radians, requested segment count and ordered oriented edge references. Center
arcs accept signed sweeps shorter than a full turn. Two-point arcs use endpoints,
plane normal and signed midpoint bulge; three-point arcs solve a circumcircle and
choose the sweep through the middle point, including major arcs. Pie sectors add
two radial edges. Circle counts are 3–256, arc counts 1–256 and pie counts 2–256.
Every derived edge must exceed modeling tolerance. Collinear/coincident constraints,
singular frames and out-of-bounds construction reject before publication.

`geometry.circle`, `geometry.arc_center`, `geometry.arc_two_points`,
`geometry.arc_three_points` and `geometry.pie` share preview/batch/undo semantics.
Optional world space converts the parameter frame into the context's local space.
Radius is a construction parameter: nonuniform transforms are carried by the axes,
so it is not falsely interpreted as Euclidean local radius. `geometry.inspect`
returns records and change reports include created/deleted/modified curve IDs.
Curve IDs share the surface allocator and cannot collide with vertices/faces or
reuse retired IDs.

Binding checks the complete sampled outline against current topology. Splitting
a chord preserves the curve and updates its ordered references. Breaking the
outline retires metadata with a deletion report; undo restores it. Extra geometry
does not alter the curve. Generic extrusion does not invent analytic provenance
for new copies of an outline. Analytic arcs describe provenance; authoritative
editing geometry remains sampled straight segments. Association work is bounded
to one million chord/edge comparisons per validation, with 1,024 curves per body
and 10,000 per document. Large-scene indexing remains later roadmap work.

Schema 4 / `json-v4` requires `curves-v1`, preserving existing envelope durability
and allocation floors. Raw v1–v3 and original v2/v3 containers still migrate in
memory. A real pre-change v3 tilted-plane document is retained as binary and raw
fixtures. Migration preserves topology and does not invent curves from polygons.
Malformed curve identities, parameters, counts, kinds and associations reject.

Validation:

- All 17 development CTest suites pass (desktop legacy downgrade fixture updated
  for schema 4, then its suite and command suite rerun).
- All 12 ASan/UBSan core suites pass.
- Curve fixtures cover tilted/reversed planes, 3/6/24/256 segments, signed sweeps,
  major/minor three-point arcs, positive/negative small/large bulges, tangent
  orthogonality, pie area, split associations, metadata retirement, undo/redo and
  atomic invalid-constraint/ID rejection.
- All 22 catalog commands have executable required/unknown-field and undo cases.
  World-space circle radius survives a nonuniform transform. Preview matches
  commit; amendment reserves retired curve IDs and remains one undo item.
- Native Wayland drawing regression passes; the five-command curve recipe saves
  and reopens through the CLI with inspectable curve records.
- Exact current-container round trips and historical v1/v2/v3 migrations pass.
  Malformed metadata rejects. Existing save interruption/error fixtures pass.

```sh
ctest --preset dev
ctest --preset sanitize
build/dev/sketchyup-cli --script examples/curves.json --output /tmp/curves.sketchyup
build/dev/sketchyup-cli --input /tmp/curves.sketchyup --query geometry.inspect --context 1
```
