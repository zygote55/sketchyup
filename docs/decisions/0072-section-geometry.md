# Derived section geometry

R064.a, 2026-10-06. This layer supplies the geometry shared by the forthcoming
section document, viewport and export adapters. It does not yet expose native
section authoring or persist active planes.

An oriented plane retains `dot(normal, point) + offset >= 0`. Its normal is unit
length; coefficients are finite and bounded. Construction from a point/direction
normalizes the direction. Context placement uses the full inverse transpose,
including nested rotation, shear, nonuniform scale and reflection. Mirroring does
not invert which local half-space is retained.

`sectionMesh` takes an immutable triangle snapshot and at most eight ordered,
uniquely identified planes. Successive half-space clipping also clips earlier
caps and cut edges. Native triangles retain their face ID, original triangle
index and barycentric weights through every cut. These weights let adapters
interpolate existing texture coordinates and normals without inventing a native
face. Generated fill instead carries its producing section ID, no native face
and no source triangle.

For each cut, retained polygon edges on the plane form a directed boundary graph.
Opposing shared edges cancel. Closed, nonbranching contours are triangulated with
an even/odd nesting rule, preserving cavities, islands and disconnected material.
Cap normals face the removed half-space. Coincident existing faces receive no
duplicate cap. Open, branching, overlapping or intersecting contours produce no
fill for that cut and report its section ID; valid cut edges remain available.
No operation silently repairs or changes authoritative model geometry.

Contour connectivity uses the existing 0.1 micrometre tolerance grid. The fill
triangulator uses that projection for topology, but output vertices reuse the
**exact original boundary positions**. Reconstructing those positions from the
rounded grid can open a seam when a subsequent oblique plane cuts a cap and its
adjacent source face; deterministic multi-plane fixtures guard that failure.
Sub-tolerance coplanarity uses the same retained-side rule for triangles, segment
clipping and point containment. Derived cap triangles never acquire selectable
native face IDs.

The adapter bounds input at 32,768 triangles, output at 262,144 triangles, contours
at 65,536 vertices per cut and clipping/nesting work at four million units.
Malformed planes, duplicate/zero IDs, nonfinite/out-of-bounds geometry and budget
exhaustion reject explicitly. Open/ambiguous fill is a reported geometric result,
not a budget or validation success fallback. Empty and fully removed inputs are
valid. Degenerate triangles have no rendered area and are omitted.

Tests compare analytic cube/tetrahedron/cavity volumes and cap areas, nested
islands, disconnected contours, open/nonmanifold boundaries, transformed and
reflected planes, millimetre features and coordinates near the model bound.
Eighty deterministic oblique-plane samples check complementary volume conservation
and plane-order independence. Provenance checks reconstruct every clipped native
vertex from its source triangle, independently of the triangulation's diagonals.
