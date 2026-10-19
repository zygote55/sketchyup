# Local patches to Clipper2 2.0.1

The vendored source is Clipper2 2.0.1 (`21ebba05db8894f0c7217ad35ea518080f324946`) with the
patch below. Every changed hunk carries a `// SketchyUp patch:` comment.

## D-042: bounded, exact triangulation

Files: `src/clipper.triangulation.cpp`, `include/clipper2/clipper.triangulation.h`.

SketchyUp tessellates faces and section caps with `Triangulate` on a 1e-7 m integer grid.
2.0.1's `SegsIntersect` multiplied the int64 coordinates in double. Past about 2^53 (faces
larger than about 10 m) a segment pair sharing an end-point could read as a proper crossing,
and `FixupEdgeIntersects` → `RemoveIntersection` → `CreateEdge` then added edges until the
process ran out of memory.

1. **Exact `SegsIntersect`.** Segments sharing an end-point are skipped first, as upstream
   commit [6a36be1c8b](https://github.com/AngusJohnson/Clipper2/commit/6a36be1c8b)
   ("fixed bugs in new triangulation module (#1055, #1056)", after 2.0.1) does; the patch also
   covers the `s1a == s2b` pairing upstream leaves out. Upstream still evaluates the rest in
   double; here the parallel test uses `ProductsAreEqual` and the crossing test uses
   `CrossProductSign`, both exact over the full int64 range (`__int128`, or the 128-bit
   emulation in `clipper.core.h`). In exact arithmetic the result is 2.0.1's rule: a proper
   crossing needs each segment's end-points strictly on opposite sides of the other's line.
2. **Hard work bounds.** `Delaunay::Execute` sets, from the input vertex count V, at most
   8V + 64 edges, 4V + 64 triangles and V(64 + √V) + 1024 Delaunay legalisation steps
   (`InCircleTest` is still double). Exceeding one throws an internal `WorkLimitExceeded`,
   which `Triangulate` returns as `TriangulateResult::fail`; SketchyUp then refuses the face
   ("Intersecting or degenerate face outline") or leaves the section cap unfilled.
   Measured on valid faces of up to 10,000 vertices: edges stay under 2.75V, triangles under
   1.5V and legalisation under 20V.
3. **Test hook.** `SetTriangulateWorkBoundScale(double)` scales the bounds on the calling
   thread so tests can exercise the refusal; production never calls it.

Upstream status (checked 2026-10-18 against `main`): only the shared end-point check in item 1
exists upstream. When updating Clipper2, keep the exact predicate and the bounds unless upstream
has made the triangulation predicates exact and bounded its loops.
