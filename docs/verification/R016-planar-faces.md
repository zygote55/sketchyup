# R016: finite planar edge arrangements

Date: 2026-10-03. Local implementation checks passed; PR merge pending.

`geometry.insert_edges` inserts finite segments in an explicit body-local editing
plane. Body `"0"` creates a new context. The command splits intersections and
collinear overlaps, coalesces duplicate spans, walks bounded regions and forms
faces. Ordered outer/inner loops and stable IDs remain the authoritative result.
The operation stages a complete candidate and publishes once after validation.

Policy and boundaries:

- Inserted endpoints within 1e-7 m of the plane snap onto it; farther points reject
  with `NON_PLANAR_INPUT`. Ambiguous nearly parallel intersections reject with
  `UNSTABLE_INTERSECTION`. Other invalid topology/resource failures carry codes.
- Existing explicit face holes remain void. Nested newly drawn closed outlines
  produce an inner face and an outer face with a hole. A closed wire network
  disconnected from the inserted edges is not implicitly filled.
- Open branches/bridges remain wires. A wire promoted into a face boundary no longer
  needs a loose-wire record. Reversed and partial coincident spans coalesce.
- Every split of an existing coplanar boundary propagates through all incident
  face loops, including adjacent noncoplanar faces. This is not a general 3D
  intersection operation for edges crossing the plane away from their endpoints.
- Unchanged faces retain identity, including boundary-only subdivisions. A face
  split into new regions retires its old ID and maps it to every descendant.
  Batch composition carries those mappings through multiple staged arrangements.
- Work is bounded to 1024 source-plus-inserted planar segments, 16384 split pieces
  and 1024 bounded regions. The R015 ancestry comparison limit still applies.
  Large-model acceleration and tolerance broadening remain measured later work.

All eight CTest targets and five ASan/UBSan core targets pass. Fixtures cover closed
and nested outlines, crossing cuts, partial/reversed overlaps, open tails, a bridge
between nested cycles, one/multiple holes, tilted/nearly planar input, site-scale
coordinates and 24 deterministic segment-order/direction permutations. Cutting a
prism's base produces seven faces while every shell edge still has two incident
faces: side loops receive the boundary splits, avoiding T junctions.

Command tests verify composed face ancestry across two staged subdivisions, one
revision/undo item, exact persistence and undo/redo connectivity, and complete
rollback when the second insertion is nonplanar. CLI rejection returns structured
JSON with `code: NON_PLANAR_INPUT`. The saved `examples/planar-grid.json` result has
four 4 m² faces and reopens in the native Wayland viewport at effective scale 1.6;
renderer, font coverage and GL checks pass. New face/edge picking associations and
more detailed tessellation pixels are the next R017 verification slice.

```sh
cmake --build --preset dev --parallel 4
ctest --preset dev
cmake --build --preset sanitize --parallel 4
ctest --preset sanitize
build/dev/sketchyup-cli --script examples/planar-grid.json --output /tmp/grid.sketchyup
build/dev/sketchyup-cli --input /tmp/grid.sketchyup --query geometry.inspect --context 1
# Isolate preferences when automating native captures.
XDG_CONFIG_HOME=/tmp/sketchyup-capture-config build/dev/sketchyup /tmp/grid.sketchyup --capture /tmp/grid.png
```
