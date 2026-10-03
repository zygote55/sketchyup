# ADR 0004: planar arrangements and topology identity

Date: 2026-10-03. R003.b. Decision: explicit loops with unrestricted radial
incidence, with Clipper2 behind a bounded planar adapter. Application geometry
remains double precision; triangulation and polygon clipping are derived work.

The executable experiment is `arrangement_tests`, linked to the headless core.
It partitions an isolated face by an infinite plane without mutating its source.
Face replacement has an explicit old-to-new map. An independent edge index proves
that endpoint identity can preserve an edge across changes in face incidence,
while split/merge maps relate retired edges to collinear overlapping replacements.
This is an architecture experiment, not the R015/R019 editing command.

## Representation decision

A manifold-only half-edge structure cannot directly represent our existing loose
wire and three-incident-face fixtures. Loops plus unrestricted edge-to-face lists
support them without converting the document into a closed solid. Store persistent
edge records in the eventual document; derive adjacency and triangulation. Do not
use triangle indices or endpoint positions as public entity identities.

The experiment's edge namespace is separate from the vertex/face namespace.
Production entity references must include document, editing context, entity kind
and ID. IDs are never recycled for unrelated entities. Undo restores the original
records and IDs, while a separate allocator high-water mark remains monotonic.
The current experimental file does not yet persist the edge index; production
integration and migration remain R015/R017 prerequisites.

Face split retires the original face and records all replacements. Edge split
retires only divided edges, preserving unaffected boundary edges. Merge retires
participating entities and maps all of them to the replacement; point contact
alone is not ancestry. Maps are command results and history data, not an invitation
to silently retarget a user's selection to an arbitrary descendant.

## Algorithms and measured fixture outcomes

The adapter projects onto a local orthonormal face basis, intersects each side of
a cutting plane with Clipper2 integer polygons, reconstructs outer/hole nesting
from PolyTree, and validates the resulting surfaces and conserved area. A tangent
or outside cut preserves the entire input and identity map unchanged.

`tests/arrangement_tests.cpp` commits the exact original fixture coordinates:

| Fixture | Outcome |
| --- | --- |
| 6 × 4 m ring with 0.2 m wall thickness, cut at x=3 | Two open outlines; total area 3.84 m²; old face maps to two new faces |
| Same ring cut at x=0.1 | The uncut hole remains attached to its containing face |
| Ring edge identities | Four unaffected edges retain IDs; four divided edges each map to two new edges; reverse merge maps eight children into four new edges |
| Plane z=x, 4 × 3 parameter-space outline | Two validated faces on the original tilted plane |
| Coordinates near (999990,999990,100) m | Two validated faces; area conserved |
| Tangent x=0 or outside x=10 | Exact no-op including all IDs |
| Cutter normal (1e-10,0,1) | Explicit near-parallel rejection; source unchanged |
| 100 deterministic cuts through ring interior | All partition into two valid faces |

Core tests separately cover loose wires, non-manifold junctions, invalid and
near-planar loops, and positive/negative isolated extrusion. Run both test targets;
the arrangement test alone is not the entire topology acceptance corpus.

## Precision and boundaries

Local-plane quantization is 10^-7 m; canonical vertices remain doubles. The
absolute coordinate bound is 10^6 m. Normalization rejects directions shorter
than 10^-7; nearly parallel cutters are rejected before unstable plane solving.
Area conservation tolerance is max(10^-8 m², area × 10^-8). These are recorded
spike limits, not promises of surveying-grade or arbitrary-scale modeling.

The isolated-face restriction prevents introduction of T-junctions into adjacent
faces. R015–R020 must propagate edge splits through every incident loop and wire,
handle coplanar overlap and arbitrary finite segments, and validate arrangements
before exposing a editing command. The edge ancestry experiment scans pairs of
edges and is not the production spatial index. Performance fixtures and granular
history are required before accepting large-mesh editing.

Curves will retain analytic provenance separately from tessellated topology:
curve kind, center/axes, radius or control points, parameter range, and ordered
segment references. Topology-changing operations preserve metadata only when the
analytic relationship remains valid; otherwise invalidate it explicitly. A smooth
rendering normal is not curve provenance. R023/R024 implement this contract.

Clipper2 remains an interchangeable BSL-1.0 adapter. No new third-party code or
license was introduced. Arbitrary 3D solid Boolean support is not inferred from
this planar experiment; Manifold and other candidates remain separate adapters.

## R015 persistent topology implementation

Body-owned edge records now persist alongside vertex/face loops. Each edge has a
context-scoped stable ID and canonical endpoint IDs; a separate monotonic edge
allocator survives undo and explicit persistence. Oriented uses and radial adjacency
derive from the persistent coverage. Shared-edge splits propagate to all incident
loops and wires, including non-manifold radial fans. The original EdgeIdentityIndex
remains a feasibility fixture, while production records use `geometry/topology.*`.
Finite coplanar arrangement/face formation remains R016; this does not promote the
infinite-plane partition experiment into a general editing tool.
