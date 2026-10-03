# R018: erase, explicit healing and scoped cleanup

Date: 2026-10-03. Local implementation checks passed; PR merge pending.

Face erase removes only the face and keeps unsupported boundary edges as wires.
Erasing a divider between exactly two coplanar faces joins their coverage into a
fresh face, with both old faces mapped to it. Existing collinear boundary vertices
survive so neighboring faces retain matching edge subdivisions. Overlapping or
ambiguous unions reject. Other edge erasures remove incident faces and retain the
remaining unsupported boundaries; non-manifold radial incidence is explicit.

Healing redraws an existing edge in a supplied plane. Only a closed missing region
that touches that edge can become a face, including an explicit hole fill. Warped
outlines and open chains reject without changing the document. Healing creates
fresh face IDs; it does not resurrect retired IDs.

Cleanup merges coincident vertex records within the selected body's editing
context, deduplicates edges, and reports vertex/edge lineage. It validates the
entire candidate and rejects a merge that collapses or invalidates a face. It does
not remove arbitrary unused vertices or modify another body. Coincidence uses the
existing 1e-7 meter tolerance and retains the lowest eligible vertex ID.

Four command catalog entries expose these operations. Atomic batches now compose
vertex, edge and face lineage across cleanup, splitting and healing. Document
history preserves exact connectivity on undo/redo while allocator floors remain
monotonic. Drawing-tool integration remains in M3.

Validation:

- Nine development CTest suites and six ASan/UBSan suites pass.
- Erase/heal fixtures cover holes, coplanar unions, subdivided prism neighbors,
  three-face non-manifold incidence, warped outlines and invalid IDs.
- Coincident seam cleanup restores shared radial incidence, leaves a neighboring
  body untouched, and rejects a valid outline whose near-touching vertices would
  merge into an invalid face.
- Command fixtures execute every published handler, validate rollback and undo,
  and compose cleanup followed by an edge split into the final two descendants.
  Container save/reopen preserves that result.

```sh
cmake --build --preset dev --parallel 4
ctest --preset dev
cmake --build --preset sanitize --parallel 4
ctest --preset sanitize
```

These are bounded planar operations, not general 3D solid intersection. Union
boundary matching has a one-million candidate budget; planar healing retains the
R016 segment/region limits. Failures leave the authoritative document unchanged.
