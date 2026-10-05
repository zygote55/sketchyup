# 0047 — Bounded native shell containment

Status: local fixture and sanitizer acceptance passes; CI/dependency merges pending.
Prerequisite for R056 solid operations.

Native surfaces keep polygon loops and authoritative topology. `analyzeSolidShells`
returns either a complete hierarchy with `validated_shells` and material volume,
or a classified `SolidReport` failure with no hierarchy/volume. Inputs must have
valid native surface and topology records. The analysis does not mutate geometry,
reverse faces, weld contacts or repair defects.

The existing boundary, manifold edge/vertex-fan, consistent face winding and
triangle-intersection checks run across every shell. Shells are connected through
authoritative face-edge adjacency and ordered by their first stable face ID.
Every shell records its sorted source face IDs, signed enclosed volume, immediate
parent and nesting depth. Each shell's volume is evaluated relative to one of its
own vertices; geometrically degenerate volume rejects before containment.

Containment uses the oriented triangle solid-angle sum described by
[Jacobson, Kavan and Sorkine-Hornung](https://igl.ethz.ch/projects/winding-number/).
It runs only after closed, disjoint manifold boundaries have been established;
this implementation does not accept the paper's broader imperfect-mesh inputs.
Two separated points from the inner boundary must agree about each candidate
container. Near-boundary points within four native tolerances, nonintegral winding
numbers or disagreement reject as `ambiguous_containment`. Angles use normalized
vectors, extended-precision intermediate products and compensated accumulation.
A magnitude within 1e-6 of winding zero/one classifies outside/inside. A bounding
box alone never establishes containment, including for through-hole geometry.

The nearest containing shell is the parent. Ancestor chains must exactly match
the containment relation, and parent enclosed volume must exceed child volume.
Face orientation alternates at each parent-child boundary: outer material,
inward cavity, outward island, and so on. A wholly reversed hierarchy is valid;
independent roots may have different global winding. Same-oriented nested shells
reject with both source-shell face identities. Material volume adds absolute
even-depth shell volume and subtracts odd-depth shell volume.

The hierarchy is limited to 64 shells. Existing 200,000 vertex-fan pair and
1,000,000 broad-phase triangle-pair work budgets remain, with at most 200,000
triangles. Containment adds a 4,000,000 point/triangle budget. Limits reject
without exposing a partial result. The analysis is bounded floating-point
classification, not an exact-predicate containment guarantee.

R056.a initially retained single-shell editing acceptance. R056.b enables
`inspectSolid` for exactly one even-depth material component: an outer shell with
zero or more direct inward cavities. Multiple roots and islands still report
`multiple_shells` without a single-body volume. Boolean reconstruction emits
those islands as separate material parts. Measurement/Info therefore reports the
outer volume minus enclosed voids, including after transformed placements.
Invalid multiple-shell inputs identify their actual boundary/winding/containment
defect. The Boolean adapter must preserve source-face provenance and independently
verify each reconstructed material body and aggregate volume; see
[0046](0046-solid-booleans.md). Trim/split/outer-shell semantics remain later R056 work.
