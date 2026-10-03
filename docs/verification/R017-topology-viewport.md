# R017: tessellation and persistent picking associations

Date: 2026-10-03. Local implementation checks passed; PR merge pending.

Per-body snapshots retain face IDs on derived triangles and persistent edge IDs
on local/world edge segments. World bounds derive from authoritative vertices and
update through nested/mirrored transforms. Triangle normals come from transformed
geometry, including winding changes. None of these caches enter the document file.

Face picking uses conservative body bounds before scanning triangles. Edge picking
uses logical-pixel proximity with perspective-correct world interpolation, view and
section clipping, opacity and nearest-face occlusion. Queries before a queued
repaint inspect the current immutable document, so a retired edge cannot be returned
from an old cache. Coincident opaque faces/edges use the renderer's last-draw order.
Per-triangle and per-edge scans remain linear; this is not the M9 spatial index.

Reference-grid lines now render without writing depth, before modeled surfaces;
they no longer shine through coplanar opaque faces. Selection clears retired faces
and is tied to document identity, preventing a later unrelated body with the same
numeric ID from inheriting the previous document's selection.

`viewport_tests` includes the new `viewport_topology.cpp` fixtures. Native Wayland
(effective scale 1.6) and X11 runs pass, alongside the existing depth/transparency,
clipping, cache reuse and context-recreation suite. Tests verify:

- A concave face with two holes has correct pixels and picks the underlying body
  through both holes and the concavity; opaque ground faces conceal the grid.
- Hole boundaries and loose wires return persistent edge IDs. Hidden/clipped or
  zero-opacity edges cannot intercept selection.
- Mirrored/transformed bounds and picks update before repaint. Reversed loops and
  cache rebuilds preserve face/edge IDs.
- Edge subdivision never returns the retired edge, and face subdivision picks
  distinct new region IDs while clearing the retired selected face.
- Repeated cache rebuilds do not mutate serialized records. Document replacement
  resets bounds and selection; coincident opaque picks agree with visible draw order.

```sh
cmake --build --preset dev --parallel 4
QT_QPA_PLATFORM=wayland timeout 40s build/dev/viewport_tests
QT_QPA_PLATFORM=xcb timeout 40s build/dev/viewport_tests
QT_QPA_PLATFORM=wayland timeout 40s build/dev/interaction_tests
ctest --preset dev
```

Core topology/serialization is unchanged from R016. Direct edge editing tools and
inference consume these associations in later drawing entries; the API does not
claim a completed edge-selection UX.
