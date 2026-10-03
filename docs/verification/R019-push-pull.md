# R019: face push/pull and through openings

Date: 2026-10-03. Local checks passed; PR merge pending.

`geometry.push_pull` and the desktop P/Measurements workflow share one staged core
operation. Distances follow the selected face's local normal. Isolated profiles
produce a closed prism, including concave outlines and holes. An isolated face in
a context with other disconnected faces keeps its base too. Complete prismatic
caps move their existing vertices and preserve face/edge identities; surrounding
walls shorten or lengthen without overlapping duplicate faces.

A selected region in a larger planar face sweeps connecting walls and a cap.
Pushing to a single opposing face that covers the entire profile removes the cap
and subtracts its profile from that opposite face. Inner loops create tunnel walls
and preserve islands. The new opposite regions have fresh IDs and explicit lineage;
untouched faces retain their IDs. Exact connectivity returns on undo/redo, with
monotonic allocator floors.

Sweep checks reject intervening faces, interior walls, ambiguous destination faces,
non-manifold selected boundaries, coplanar overlap and collapsed geometry before
publication. A through cut must finish with two incident faces at every boundary.
This is bounded face modeling, not a general solid boolean: arbitrary nonprismatic
inward intersections, partial overlap at the destination and multi-face opposing
surfaces reject. The context limit is 1000 faces/10000 vertices, pair/boundary work
is budgeted, and distance must exceed twice the 1e-7 meter modeling tolerance.

`geometry.preview` accepts the same identity/revision-bound batch used for commit.
It returns prospective document metadata, topology and lineage from a private copy;
source bytes, revision, allocator state and history remain unchanged. Preview is
advisory, and commit still checks the expected revision. CLI `--preview --script`
exposes it without an output-save option. Durable transactions and interactive
pointer-drag previews remain later roadmap work.

Validation:

- All ten development CTest suites and seven ASan/UBSan suites pass. The final
  destination-contact check also passed the targeted sanitized push/pull suite.
- Box cap extension and retraction check volume and stable loops/IDs. Concave,
  holed and negative-distance profiles have closed radial incidence and expected
  oriented volumes.
- Recess, outward region sweep, rectangular through opening and annular through
  opening preserve expected volume and boundaries. The annulus retains its center
  pillar. Overruns, nonfinite/oversized values and a sloped obstruction reject
  atomically. Undo/redo restores exact topology.
- Command preview and commit agree at the same revision; stale previews reject.
  Container round trips retain committed records.
- Native Wayland interaction tests pick and resize an existing box cap through
  Measurements. The viewport regression suite passes at effective scale 1.6.
- `examples/through-opening.json` executes as one CLI batch and saves successfully.
  [Native capture](R019-opening.png) and [capture metadata](R019-opening.json) show
  the reopened result.

```sh
ctest --preset dev
ctest --preset sanitize
QT_QPA_PLATFORM=wayland build/dev/interaction_tests
QT_QPA_PLATFORM=wayland build/dev/viewport_tests
build/dev/sketchyup-cli --preview --script examples/through-opening.json
build/dev/sketchyup-cli --script examples/through-opening.json --output /tmp/opening.sketchyup
```
