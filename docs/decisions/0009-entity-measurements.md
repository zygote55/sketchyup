# Entity measurements use explicit coordinate frames

R035.a, 2026-10-04.

`entity.inspect` reports a context, face, edge or guide without changing document
state. Whole contexts include descendants and hidden/locked content. World,
parent-coordinate and intrinsic entity-coordinate bounds are distinct. Each
frame reports axis-aligned bounds/dimensions, unique topological edge length,
triangulated face area and validated volume. Intrinsic coordinates remove the
selected owner's entire placement transform, including scale. Parent coordinates
include that placement, allowing editable dimensions to read back their targets.

`entity.position` and `entity.dimensions` accept world or parent coordinates in
meters. Position edits move the entity origin. Dimensions scale about the minimum
of that frame's bounding box; repeated entry of the same dimensions is a no-op.
Zero-sized axes remain zero; adding thickness requires push/pull, and collapsing
an existing dimension rejects. Existing transform, lock, component scope, world
coordinate and transaction rules remain authoritative.

Lengths count each stored topological edge once, including face boundaries and
loose edges. Area sums stored faces rather than attempting a boolean exterior
surface calculation. Group totals do not deduplicate separate records. Guides
are excluded from context geometry bounds; selected guide points have finite
point bounds, while infinite guides report null length and bounds with an
explicit infinite-length flag. Coordinates, lengths, areas and volumes use meters,
square meters and cubic meters in the named frame.

Volume is conservative. A context must contain one nonempty geometry record;
its surface must have no loose geometry, exactly two oppositely oriented faces
per edge, a connected face fan at every vertex, and one material component
(an outer shell with optional inward cavity boundaries). Triangle
intersection checks reject contacts away from shared topological boundaries,
including coplanar area overlaps. Predicates use the model tolerance, with a
four-tolerance allowance when matching computed contacts to shared boundaries.
Signed tetrahedral integration uses a nearby origin and a long-double sum;
absolute affine determinants convert volume between frames and preserve a
positive result under mirroring.

Multiple geometry records or disconnected material components, boundary defects, inconsistent winding,
self-intersections, degeneracy and exhausted analysis budgets have no volume.
The query returns a status and available offending entity IDs, with null frame
volumes. Multiple material components/records and budget exhaustion are unresolved analysis,
not proof that the model is invalid. Limits are 200,000 triangles, 200,000
shared-vertex face-pair contributions and 1,000,000 broad-phase triangle pair
visits, plus 64 shells and 4,000,000 containment point/triangle visits. Boolean
operations use the shared [adapter contract](0046-solid-booleans.md); explicit
repair remains later work.

`entity.properties` replaces the typed semantic map atomically. Booleans, finite
numbers and strings use the existing 128-property, 128-byte key and 2,048-byte
string limits. Placement properties remain local; canonical member properties
use explicit component edit scope. File schema remains 9 because these property
and transform records already persist exactly.

R056 extends the original single-shell restriction through the bounded
[shell-containment contract](0047-shell-containment.md). One outer boundary with
validated inward cavity boundaries is one material solid: measured volume
subtracts voids. Disconnected material roots or islands in one geometry record
still return no single-solid volume. Ambiguous containment is explicit; no void
is silently filled. Boolean outputs group cavities with their material body and
return independent islands as separate bodies.
