# 0052 — Smooth shading derives bounded corner normals

Status: R057.g local kernel, native display and Blender acceptance pass. Native hide/soften/reveal
controls and explicit edge display follow in a subsequent R057 layer.

`ShadingNormals` derives area-weighted normals from a body's surface, persistent
edge topology and independent smooth flags. It never modifies positions, face
loops, edge IDs, tessellation, allocators or solid classification. Hidden and
soft flags alone do not change normals. Hidden faces contribute to the same
geometric fan as visible faces; viewport and export use the same policy.

## Fans and bounds

An explicitly smooth edge joins its two endpoint corners only when it has exactly
two different incident faces traversing the edge in opposite directions. Boundary,
non-manifold and inconsistently oriented edges remain hard. A disjoint-set joins
these corners transitively at each authoritative vertex. Equal coordinates with
different vertex identities cannot join, and an unrelated face at the same vertex
retains its own corner. A hard seam can separate different fans at a vertex.

Each face contributes its unit normal weighted by net planar area: outer outline
minus holes, independent of hole winding. Origin-relative area vectors avoid
large-coordinate cancellation. Fan sums use long double; a cancelling sum below
1e-12 of its total weight falls back to each incident face's original normal.
Normalization uses the vector's magnitude, so small valid faces are not rejected
by an unrelated linear tolerance.

Construction checks at most 100,000 faces, 300,000 edges, 10,000 vertices per loop
and two million total corner references before adjacency allocation. Storage and
traversal are bounded by those counts; map lookups and disjoint-set joins avoid a
quadratic search through all triangle vertices.

## Tessellation and rendering

The lookup reproduces the existing triangulator's face basis and 1e7 integer
projection. It maps quantized triangle corners back to normals inside that face;
there is no nearest-coordinate search across faces or welding. Ambiguous projected
aliases and non-corner tessellation points use the original flat face normal.
Positions and triangulation remain unchanged.

The viewport transforms corner normals by inverse transpose and computes its
existing simple lighting separately at each corner. Negative-determinant placements
still preserve the physical front/back appearance. Edge-appearance changes invalidate
appearance uploads while retaining cached local/world geometry. The viewport's
interpolated lighting is an interactive approximation; Cycles keeps its physical
material and lighting model.

GLB emits these same local corner normals with the existing placement matrices.
Paired back primitives reverse the corner order and negate the corresponding
normal. Mesh hashing includes the normal bytes, so equal component meshes remain
shared and differently shaded meshes remain distinct. The existing fixed Cycles
adapter validates paired normals, removes only the redundant back primitives and
retains imported split normals when constructing the front/back material shader.
