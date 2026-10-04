# R025: indexed point and surface inference

Date: 2026-10-03. Local verification and both CI jobs passed.
PR #25 merged as `5312110`.

A two-level world-space BVH indexes per-body immutable geometry and context bounds.
Endpoint, midpoint, analytic curve center, on-edge and on-face primitives are
queried through a narrow camera frustum. Near-pointer edge pairs produce true
3D intersections; overlapping screen projections alone never intersect. An edge
crossing a locked construction plane supplies a plane-intersection candidate.
Acquisition is 8 logical pixels; geometry tolerance remains 1e-7 m. Perspective
edge interpolation corrects for endpoint clip W. Face visibility tests remove
occluded candidates. Camera movement changes only the query, not index geometry.

Candidates rank by kind (endpoint, intersection, midpoint, center, edge, face),
then pixel distance, depth and stable IDs. Native feedback uses the UX marker
shapes and labels. Tab cycles alternatives without transferring keyboard focus.
Entity type accompanies IDs so vertex and edge namespaces cannot be confused.
Automatic first-point acquisition adopts the candidate's body and an incident
face plane, or a horizontal plane through loose geometry. Explicit planes remain
fixed and reject off-plane candidates. Grid fallback remains 0.1 m; freehand
continues raw sampling without point inference.

Changed body caches rebuild while unchanged caches share storage. Index preparation
runs on immutable snapshots in a Qt thread-pool task; pending requests coalesce.
The UI accepts only the current document session/state/revision, including reopen
and divergent copies with identical identity/revision numbers. A preparation hint
appears while inference is unavailable. Small scenes retain face-plane picking
while preparation runs; above 5,000 faces automatic first-point drawing waits for
the index instead of scanning the whole scene. Explicit-plane drawing and camera
navigation remain available. Closing the viewport cancels publication and pending
work; the worker owns no widget and never mutates the model.

Queries cap collection at 4,096 candidates, 256 nearby edges for pair intersections,
128 visibility checks and 32 alternatives. Truncation is reported, including a `+`
on the native alternatives count. This is bounded local acquisition, not an
exhaustive all-intersections operation. Selection's existing general picking path
is separate; R028/M9 still cover selection behavior and broader performance gates.

`geometry.infer` is a read-only query accepting column-major clip/inverse matrices,
logical viewport dimensions, pointer coordinates, optional radius/context/plane.
`--query-file` exposes full query objects in the local CLI. Results include point,
kind, typed entity/context references, pixel distance, depth and truncation status.

Continuous on-face acquisition exposed an existing area-check defect: separately
rounded render tessellations could disagree after partitioning one face. `Surface::area`
now validates with triangulation but integrates the authoritative loop coordinates,
subtracting holes. Coverage tolerance is unchanged. Continuous-coordinate subdivision
fixtures and the formerly failing native hovered-face construction cover the fix.

Final debug benchmark on the development host: 1,000 independently transformed
bodies, 100,000 triangles, 458,000 indexed primitives; initial preparation 2.25 s,
200 pointer queries p95 2.73 ms, mean 949 primitive visits. The target is <50 ms p95.
These bodies approximate the workload before component instancing exists; this is
not acceptance of the later instancing or million-triangle rendering milestones.
[Benchmark artifact](R025-benchmark.json) records the measurements.

Validation:

- All 18 development suites and 13 ASan/UBSan suites pass. Inference was rerun
  under sanitizers after the final typed-identity ordering change.
- Core fixtures cover logical-pixel radius at multiple zooms, midpoint/center/
  edge/face/intersection acquisition, false screen crossings, locked planes,
  perspective interpolation, occlusion, undo, incremental context updates and
  divergent snapshots with the same document identity/revision.
- Native inference tests pass on Wayland at device scale 1.6 and pinned Arch/Xvfb
  at scales 1 and 2. Tests cover exact non-grid endpoints, context adoption,
  alternatives, Tab focus ownership, plane filtering, background request
  coalescing, revision/session invalidation and a rendered marker pixel check.
- Drawing, curve, numeric and lifecycle regressions pass on Wayland; drawing and
  curve regressions also pass in pinned Xvfb. Viewport pixel/depth/transparency/
  clipping/context-recreation tests pass on both platforms.
- The CLI query recipe reports the saved analytic circle center and face without
  changing the input file. Existing document-save/error tests pass.

The [marker capture](R025-marker.png) uses `grabFramebuffer()` to include the
OpenGL overlay; generic QWidget capture omitted it. The renderer now encloses
raw GL calls in QPainter's native-painting handoff, following the
[Qt QOpenGLWidget contract](https://doc.qt.io/qt-6/qopenglwidget.html). The marker
pixel assertion and existing viewport tests cover visible feedback and GL state.
One subsequent Wayland capture run was denied window activation; this is the
known host-focus fixture limitation, not an inference-state failure.

```sh
ctest --preset dev
ctest --preset sanitize
build/dev/inference_tests --benchmark
QT_QPA_PLATFORM=wayland build/dev/inference_input_tests
build/dev/sketchyup-cli --input /tmp/curves.sketchyup --query-file examples/inference-query.json
```
