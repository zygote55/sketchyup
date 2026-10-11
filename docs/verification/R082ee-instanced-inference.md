# R082.ee: Instanced inference indexing

2026-10-10. The inference index used to store every scene body's primitives in
world space. For component instances, that meant one copy of the definition's
vertices, edges and faces per placement. A private 10,000-instance run indexed
3.6M primitives. This layer changes only inference indexing. Persistence
(R082.cc), the viewport, construction and limits are left to other layers.

## Scheme

Each distinct body geometry is indexed once, in local space. That is one
`Local` record per definition member, shared by every instance. Each record has
a local BVH, local primitives and vertex-to-edge adjacency. Every scene body becomes a
**placement**: its record, its composed world transform (`Document::worldTransform`,
the same double-precision composition as before) and a pointer to the shared
local index. A top-level BVH covers placement world bounds.

During a query, each primitive that reaches a local BVH leaf is converted to world
space with the placement's own `Transform::point`. Midpoints are taken between world
endpoints, as before. The resulting world box is tested exactly as the expanded
index tested its stored box. Snapped points, pixels and depths are therefore
bit-identical to the expanded index. Local node boxes are tested through a
conservative world AABB (Arvo transform plus a 1e-12 relative rounding margin). This
keeps every primitive the expanded index would visit, which includes mirrored,
non-uniformly scaled and rotated placements. `visitedPrimitives` stays identical.
Guides stay per placement in world space, because bounded guide lines are not affine.

Sharing is resolved through the instance binding (`ComponentInstance::members`,
scene record to definition member). It applies only when inference-relevant geometry
is equal: vertices, faces, wires, edge records and curves. Allocator floors are
ignored, as permitted by `matchesComponentProjection`. Any other record gets a
private local index, so sharing never changes results. Candidates still report
the scene body ID. Hidden, locked, tag and nested-context filtering is unchanged:
`visible`, `eligible`, `pointVisible` and `context` still receive scene IDs, and
the R082.g self-occlusion ray is untouched.

**Incremental updates.** If a moved placement keeps its record, its local index is
reused. If its record changes but the geometry is equal, the local index is also
reused. Only its placement entry is rebuilt. A definition edit builds the new member
index once for all instances. `bodyBuilds()` keeps its meaning (placement entries
rebuilt), so every existing assertion, including the benchmark's, is unchanged.
`localBuilds()`, `indexedPrimitiveCount()` and `indexBytes()` are new.

**Traversal-order independence.** The old index reported an edge/edge intersection
with whichever edge its world BVH visited first. It also kept the first 256 edges
and 4,096 candidates in that same order. Shared local trees cannot reproduce that
order, so the order is now canonical. Edges are paired by `(body, kind, entity)`.
Truncation keeps the best items by that order, or by candidate priority with
trailing point coordinates. Memory stays bounded by pruning at twice each limit.
These are the only behavioral differences. They occur only where the previous
result depended on traversal accident.

## Equivalence oracle

`tests/inference_equivalence_tests.cpp` (ctest `inference_equivalence`) builds two
fixed-seed scenes. One is near the origin and one is offset by (900000, −900000,
900000), following R082.e. Each scene has a leaf component with crossing wires,
placed 11 times with rotated, mirrored, non-uniformly scaled and mirrored+scaled
transforms. It also has a nested assembly (leaf plus a curve body) placed 7 times,
two levels of transformed group nesting (including a z-mirror), unique bodies and three
guides. The scenes also include a hidden tag, a document-hidden instance, a locked
body, temporarily hidden faces and edges, a locked instance and an entered nested context.

Random perspective and orthographic cameras are used, half of them with float-rounded
matrices. Queries are placed near projected vertices, midpoints and triangle centers,
with radii of 4–24 px. Modes include an exact `context`, a drawing plane, guides
excluded, both viewport filter policies, a clip `pointVisible` and pixel centers.
In every query the shared index must equal **exactly** a brute-force oracle: every
scene body expanded with the pre-R082.ee code and visited linearly in shuffled
order. All candidate fields are compared, plus `visitedPrimitives`,
`intersectionPairs` and `truncated`. Queries also run after a placement move, an
added placement, a shared definition edit and its undo.

The test also compares against a verbatim copy of the old index
(`tests/legacy_inference_index.hpp`, test-only). It does this for every query where
neither index truncated: 359 of 480 queries in the final run. Candidates must match
exactly, except that an intersecting pair's orientation is normalized. Its point,
which is evaluated from the first edge, is compared within the engine tolerance.
Incremental assertions: a placement move builds no local index and rebuilds exactly
that instance's placements; an added placement builds none; a definition edit, and
its undo, each build exactly one. As a mutation check, shrinking node bounds by 10%
fails the first query. `inference_instanced_io` reopens an instanced R082.cc
document and checks that sharing and every query result are preserved.

## Measurements (software, headless; not hardware evidence)

`inference_index_benchmark` (Release, this host, three trials each) runs the
real_model_benchmark fixture families. Each family is constructed in memory, without
the encode/decode round trip. Textures matches repeated geometry. Queries use a
fixed 1920×1080 perspective camera and 8 px at 50 placements. Updates are 100
transforms of one placement. [Raw rows](R082ee-index-benchmark.jsonl).

| 1,000 placements | Stored primitives (before → after) | Index bytes (before → after) | Build ms | Query p95 ms | Update p95 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| repeated | 360,000 → 360 | 89.0 MB → 0.85 MB | 279–307 → 9.8–11.7 | 0.38–0.47 → 0.58–0.72 | 2.5–4.3 → 1.7–2.4 |
| unique | 360,000 → 360,000 | 88.6 MB → 47.7 MB | 284–314 → 278–282 | 0.42 → 0.70–0.77 | 1.2–1.3 → 0.87–0.92 |
| deep | 360,000 → 360 | 89.0 MB → 0.87 MB | 278–285 → 18.5–19.6 | 0.57–0.78 → 0.58–0.65 | 9.5–12.8 → 8.7–11.4 |
| far | 360,000 → 360 | 89.0 MB → 0.85 MB | 323–353 → 12–34 | 0.48–0.54 → 0.67–1.65 | 3.2–4.7 → 2.0–2.4 |

Index bytes are the index's own estimate. The measured malloc-held delta at sync
agrees within 3% (89.6 MB → 0.88 MB for repeated). Logical primitives, which the
index presents to queries, stay at 360,000, and the maximum number of visited
primitives is unchanged. Queries cost about 0.2–0.35 ms more at p95, because each
visited primitive is now transformed on demand. That is the accepted trade for a
~100× smaller index and a 25× faster build.

The native `real_model_benchmark` (Debug, xcb under xvfb, one CPU, software GL, in
the container) passes 25- and 1,000-placement runs for all five scenarios with its
assertions unchanged. That includes "rebuilds only the changed placement" and the
viewport-picked geometry check. [Inference fields](R082ee-native-benchmark-software.json).
Those Debug timings are not comparable with earlier Release hardware reports.

## Regression matrix

[Logs](R082ee-regressions.txt), [native](R082ee-native-matrix.txt). The source is the
R082.ee branch merged with `origin/main` at R082.cc (`c04aea04`).

- Dev: 18 focused suites pass. They cover inference, `inference_equivalence`,
  `inference_instanced_io`, `instanced_storage`, constraints, guides, drawing,
  measured drawing, selection, transform selection, components, component records,
  scope, placement, glue, glue IO, hosted and hosted IO.
- Full headless ctest: 60/60.
- ASan/UBSan (`sanitize` preset): 9/9 inference, equivalence, constraint, guide,
  drawing, selection and component suites.
- Native, in the container: inference, guide, constraint, drawing, selection,
  component and curve input tests on X11 at 1× and 2× and Wayland at 1× and 2×,
  using the CI invocations and `scripts/test-wayland.sh`. 25 of 28 passed on the
  first pass. The three failures were guide, constraint and curve on **Wayland at
  2×**, none of which runs on Wayland in CI. They failed at "viewport exposed",
  before any inference runs, and passed on an immediate rerun. Over three more
  repeats they failed intermittently with the same exposure message.
  `inference_input_tests` passed every time. This is recorded as a pre-existing
  harness flake on the 1-CPU headless Weston at 2×. It is not attributed to this change.

## Open

Native measurements from Release builds on hardware, the 10,000-instance run, and
any change to viewport, construction or limit behavior (R082.dd, R082.ff, R082.gg)
remain open. No public limits or budgets change.
