# 0043 — Planar offset region contract

Status: kernel, shared command and native tool verified locally; CI/merges pending.

R052 starts with an immutable numerical adapter so collapse and splitting can be
verified before connecting it to document history. `offsetFaceRegion` accepts one
existing planar face (outer boundary and strictly contained, nonintersecting
holes) and a signed distance in local meters. Positive expands material and
contracts holes; negative erodes material and expands holes. Zero returns the
original coordinates and loop order exactly. The source surface and ID allocator
are never modified.

The adapter uses the already vendored, unmodified Clipper2 2.0.1, under its
existing Boost Software License. Closed polygon paths use miter joins with a
limit of four times the offset distance; sharper corners use squared joins.
This bounds acute spikes and is part of the geometry contract, not an undocumented
approximation to an unbounded miter. See the upstream
[ClipperOffset contract](https://angusj.com/clipper2/Docs/Units/Clipper.Offset/Classes/ClipperOffset/_Body.htm).

The result contains every surviving connected material region, with its outer
loop followed by holes, retaining the source face normal. A collapsed neck may
produce several regions. Empty regions explicitly mean complete collapse at
numerical precision; this result does not authorize deletion of the source face.
Input/output hole counts expose changes, including hole disappearance and merger;
they do not claim lineage for individual holes. No surviving island is selected
by size or discarded by the native adapter. Surviving sub-tolerance edges or
invalid native faces cause an actionable rejection of the entire result.

Coordinates are projected into an orthonormal face frame centered near its
bounding box, then rounded onto a 10 nm integer grid. Distances are passed at
that scale; projection and integer reconstruction have finite precision. Native
validation remains authoritative at its 100 nm modeling tolerance. Output is
validated again as native planar faces and must remain within the one-million-
meter coordinate range. This is not an exact-arithmetic construction. The
fixtures bound rectangle side error to 20 nm for the tested oblique orientation,
large world origin and 48 seeded continuous distances; this is evidence for those
fixtures, not a universal error bound for arbitrarily acute geometry.

Admission is bounded to 1,024 input vertices and 64 loops. Results allow at most
4,096 vertices, 256 regions and 64 levels of containment. Output limits are
checked after the library operation; they are not a hard memory cap inside
Clipper. Distances must be finite, at most one million meters in magnitude, and
either zero or at least the modeling tolerance. Canonical loop starts, hole
sorting and region sorting make results deterministic for equivalent path order.

Errors distinguish invalid distance/face, complexity limits, out-of-range output,
sub-tolerance boundaries and numerical offset failure. The command preserves
these distinctions and rejects complete collapse without changing the document.
The native tool shares that command, including read-only preview, selected-context
integration, topology lineage and one-entry Undo/Redo. Local verification does
not accept R052 ahead of its CI/dependencies or M6 ahead of M5.

The document integration draws the computed boundaries through the existing
planar arrangement. It retains original coverage and explicit holes, while
subdividing faces and forming exterior regions as ordinary drawing does. In
particular, an outward offset contracts hole outlines into the original void:
those outlines remain wires instead of silently filling a hole. This preserves
the source geometry as an offset drawing operation. A separate erase/heal edit
is required to change the original void. The command supports local and world
distance; world offsets operate on transformed loops before returning to local
coordinates, so nonuniform scales do not distort the requested distance.
